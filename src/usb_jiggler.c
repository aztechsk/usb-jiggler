/*
 * usb_jiggler.c
 *
 * Copyright (c) 2024 Jan Rusnak <jan@rusnak.sk>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>
#include <gentyp.h>
#include "sysconf.h"
#include "criterr.h"
#include "msgconf.h"
#include "udp.h"
#include "usb_std_def.h"
#include "usb_hid_def.h"
#include "usb_ctl_req.h"
#include "usb_log.h"
#include "tools.h"
#include "usb_jiggler.h"

struct mouse_report mouse_report;
#if USB_JIG_KEYB_IFACE == 1
struct keyb_report keyb_report;
#if LOG_KEYB_LEDS == 1
#define KEYB_LED_REPORT_QUE_SIZE 2
QueueSetHandle_t keyb_led_rep_que;
#endif
#endif
QueueSetHandle_t jig_ctl_qset;

struct jig_conf_descs {
    struct usb_conf_desc conf_desc;
    struct usb_iface_desc hid_iface_m;
    struct usb_hid_desc hid_desc_m;
    struct usb_endp_desc rep_in_m;
#if USB_JIG_KEYB_IFACE == 1
    struct usb_iface_desc hid_iface_k;
    struct usb_hid_desc hid_desc_k;
    struct usb_endp_desc rep_in_k;
#endif
} __attribute__ ((packed));

static const uint8_t m_rep_desc[] = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,                    // USAGE (Mouse)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x09, 0x01,                    //   USAGE (Pointer)
    0xa1, 0x00,                    //   COLLECTION (Physical)
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
    0x29, 0x03,                    //     USAGE_MAXIMUM (Button 3)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x95, 0x03,                    //     REPORT_COUNT (3)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x75, 0x05,                    //     REPORT_SIZE (5)
    0x81, 0x03,                    //     INPUT (Cnst,Var,Abs)
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                    //     USAGE (X)
    0x09, 0x31,                    //     USAGE (Y)
    0x15, 0x81,                    //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,                    //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x02,                    //     REPORT_COUNT (2)
    0x81, 0x06,                    //     INPUT (Data,Var,Rel)
    0x09, 0x38,                    //     USAGE (Wheel)
    0x15, 0x81,                    //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,                    //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x81, 0x06,                    //     INPUT (Data,Var,Rel)
    0xc0,                          //   END_COLLECTION
    0xc0                           // END_COLLECTION
};

#if USB_JIG_KEYB_IFACE == 1
static const uint8_t k_rep_desc[] = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x06,                    // USAGE (Keyboard)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x05, 0x07,                    //   USAGE_PAGE (Keyboard)
    0x19, 0xe0,                    //   USAGE_MINIMUM (Keyboard LeftControl)
    0x29, 0xe7,                    //   USAGE_MAXIMUM (Keyboard Right GUI)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //   REPORT_SIZE (1)
    0x95, 0x08,                    //   REPORT_COUNT (8)
    0x81, 0x02,                    //   INPUT (Data,Var,Abs)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x75, 0x08,                    //   REPORT_SIZE (8)
    0x81, 0x03,                    //   INPUT (Cnst,Var,Abs)
    0x95, 0x05,                    //   REPORT_COUNT (5)
    0x75, 0x01,                    //   REPORT_SIZE (1)
    0x05, 0x08,                    //   USAGE_PAGE (LEDs)
    0x19, 0x01,                    //   USAGE_MINIMUM (Num Lock)
    0x29, 0x05,                    //   USAGE_MAXIMUM (Kana)
    0x91, 0x02,                    //   OUTPUT (Data,Var,Abs)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x75, 0x03,                    //   REPORT_SIZE (3)
    0x91, 0x03,                    //   OUTPUT (Cnst,Var,Abs)
    0x95, 0x06,                    //   REPORT_COUNT (6)
    0x75, 0x08,                    //   REPORT_SIZE (8)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x25, 0x65,                    //   LOGICAL_MAXIMUM (101)
    0x05, 0x07,                    //   USAGE_PAGE (Keyboard)
    0x19, 0x00,                    //   USAGE_MINIMUM (Reserved (no event indicated))
    0x29, 0x65,                    //   USAGE_MAXIMUM (Keyboard Application)
    0x81, 0x00,                    //   INPUT (Data,Ary,Abs)
    0xc0                           // END_COLLECTION
};
#endif

static const struct usb_dev_desc dev_desc = {
	.size = sizeof(struct usb_dev_desc),
	.type = USB_DEV_DESC,
	.bcd_usb = USB_STD_USB2_00_VER_BCD,
	.b_device_class = 0,
	.b_device_subclass = 0,
	.b_device_protocol = 0,
	.b_max_packet_size0 = 64,
	.id_vendor = USB_JIG_VENDORID,
	.id_product = USB_JIG_PRODUCTID,
	.bcd_device = 0x0100,
	.i_manufacturer = 1,
	.i_product = 2,
	.i_serial_number = 3,
	.b_num_configurations = 1
};

static const struct jig_conf_descs conf_descs = {
	{
	.size = sizeof(struct usb_conf_desc),
        .type = USB_CONF_DESC,
        .w_total_size = sizeof(struct jig_conf_descs),
#if USB_JIG_KEYB_IFACE == 1
        .b_num_interfaces = 2,
#else
	.b_num_interfaces = 1,
#endif
        .b_configuration_value = 1,
        .i_configuration = 0,
        .bm_attributes = USB_STD_BUS_POWER_NO_RWAKE,
        .b_max_power = usb_std_max_power_mamp(100)},
	{
        .size = sizeof(struct usb_iface_desc),
        .type = USB_IFACE_DESC,
        .b_interface_number = 0,
        .b_alternate_setting = 0,
        .b_num_endpoints = 1,
        .b_interface_class = USB_HID_CLASS,
        .b_interface_subclass = USB_HID_SUBCLASS_NO_BOOT,
	.b_interface_protocol = 0,
        .i_interface = 0},
	{
        .size = sizeof(struct usb_hid_desc),
        .type = USB_HID_DESC,
        .bcd_hid = USB_HID_REL_1_11_VER_BCD,
        .country_code = 0,
        .num_descs = 1,
        .rep_desc_type = USB_HID_REPORT_DESC,
        .rep_desc_size = sizeof(m_rep_desc)},
        {
	.size = sizeof(struct usb_endp_desc),
        .type = USB_ENDP_DESC,
        .b_endpoint_address = usb_std_endp_addr(USB_JIG_IN_M_ENDP_NUM, USB_STD_IN_ENDP),
        .bm_attributes = USB_STD_TRANS_INTERRUPT,
        .w_max_packet_size = USB_JIG_IN_M_ENDP_MAX_PKT_SIZE,
        .b_interval = USB_JIG_IN_M_ENDP_POLLED_MS},
#if USB_JIG_KEYB_IFACE == 1
	{
        .size = sizeof(struct usb_iface_desc),
        .type = USB_IFACE_DESC,
        .b_interface_number = 1,
        .b_alternate_setting = 0,
        .b_num_endpoints = 1,
        .b_interface_class = USB_HID_CLASS,
        .b_interface_subclass = USB_HID_SUBCLASS_NO_BOOT,
	.b_interface_protocol = 0,
        .i_interface = 0},
	{
        .size = sizeof(struct usb_hid_desc),
        .type = USB_HID_DESC,
        .bcd_hid = USB_HID_REL_1_11_VER_BCD,
        .country_code = 0,
        .num_descs = 1,
        .rep_desc_type = USB_HID_REPORT_DESC,
        .rep_desc_size = sizeof(k_rep_desc)},
        {
	.size = sizeof(struct usb_endp_desc),
        .type = USB_ENDP_DESC,
        .b_endpoint_address = usb_std_endp_addr(USB_JIG_IN_K_ENDP_NUM, USB_STD_IN_ENDP),
        .bm_attributes = USB_STD_TRANS_INTERRUPT,
        .w_max_packet_size = USB_JIG_IN_K_ENDP_MAX_PKT_SIZE,
        .b_interval = USB_JIG_IN_K_ENDP_POLLED_MS}
#endif
};

static const uint8_t lang_str_desc[] = {
	usb_std_str_desc_size(1),
        USB_STR_DESC,
        USB_STD_EN_US_CODE
};

static const uint8_t manufacturer_str_desc[] = {
	usb_std_str_desc_size(6),
        USB_STR_DESC,
        usb_std_unicode('A'),
        usb_std_unicode('Z'),
        usb_std_unicode('T'),
        usb_std_unicode('e'),
        usb_std_unicode('c'),
        usb_std_unicode('h')
};

static const uint8_t product_str_desc[] = {
	usb_std_str_desc_size(11),
        USB_STR_DESC,
        usb_std_unicode('S'),
        usb_std_unicode('A'),
        usb_std_unicode('M'),
        usb_std_unicode('_'),
        usb_std_unicode('J'),
        usb_std_unicode('I'),
        usb_std_unicode('G'),
        usb_std_unicode('G'),
        usb_std_unicode('L'),
        usb_std_unicode('E'),
        usb_std_unicode('R')
};

static const uint8_t serial_str_desc[] = {
	usb_std_str_desc_size(10),
        USB_STR_DESC,
        usb_std_unicode('0'),
        usb_std_unicode('1'),
        usb_std_unicode('2'),
        usb_std_unicode('3'),
        usb_std_unicode('4'),
        usb_std_unicode('5'),
        usb_std_unicode('6'),
        usb_std_unicode('7'),
        usb_std_unicode('8'),
        usb_std_unicode('9')
};

static const uint8_t *str_desc_arry[] = {
	lang_str_desc,
	manufacturer_str_desc,
	product_str_desc,
	serial_str_desc
};

static boolean_t is_self_powered(void);
static struct usb_ctl_req std_stp(struct usb_stp_pkt *sp);
static void std_in_req_ack_clbk(void);
static boolean_t std_out_req_rec_clbk(void);
static void std_out_req_ack_clbk(void);
static struct usb_ctl_req cls_stp(struct usb_stp_pkt *sp);
static void cls_in_req_ack_clbk(void);
static boolean_t cls_out_req_rec_clbk(void);
static void cls_out_req_ack_clbk(void);
static struct usb_ctl_req vnd_stp(struct usb_stp_pkt *sp);
static void vnd_in_req_ack_clbk(void);
static boolean_t vnd_out_req_rec_clbk(void);
static void vnd_out_req_ack_clbk(void);
static void std_get_desc_dev(struct usb_ctl_req *ucr);
static void std_get_desc_ifc(struct usb_ctl_req *ucr);
static boolean_t check_lng_code(uint16_t code);
static void std_set_addr(struct usb_ctl_req *ucr);
static void std_set_conf(struct usb_ctl_req *ucr);
static void std_get_conf(struct usb_ctl_req *ucr);
static void std_set_desc(struct usb_ctl_req *ucr);
static void std_set_iface(struct usb_ctl_req *ucr);
static void std_get_iface(struct usb_ctl_req *ucr);
static void std_synch_frm(struct usb_ctl_req *ucr);
static void std_get_dev_stat(struct usb_ctl_req *ucr);
static void std_get_iface_stat(struct usb_ctl_req *ucr);
static void std_get_endp_stat(struct usb_ctl_req *ucr);
static void std_clr_dev_feat(struct usb_ctl_req *ucr);
static void std_set_dev_feat(struct usb_ctl_req *ucr);
static void std_clr_set_iface_feat(struct usb_ctl_req *ucr);
static void std_clr_set_endp_feat(struct usb_ctl_req *ucr);
static void cls_get_report(struct usb_ctl_req *ucr);
static void cls_get_idle(struct usb_ctl_req *ucr);
static void cls_get_protocol(struct usb_ctl_req *ucr);
static void cls_set_report(struct usb_ctl_req *ucr);
static void cls_set_idle(struct usb_ctl_req *ucr);
static void cls_set_protocol(struct usb_ctl_req *ucr);
static boolean_t is_endp_index_valid(int w_index);
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
static void log_std_cmd_event(const char *txt);
static void log_cls_cmd_event(const char *txt);
#endif
#if USB_LOG_CTL_REQ_STP_EVENTS == 1
static void log_stp_event(struct usb_stp_pkt *stp);
#endif

static struct usb_ctl_req_clbks std_ctl_req_clbks = {
	.stp_clbk = std_stp,
	.in_req_ack_clbk = std_in_req_ack_clbk,
	.out_req_rec_clbk = std_out_req_rec_clbk,
	.out_req_ack_clbk = std_out_req_ack_clbk
};

static struct usb_ctl_req_clbks cls_ctl_req_clbks = {
	.stp_clbk = cls_stp,
	.in_req_ack_clbk = cls_in_req_ack_clbk,
	.out_req_rec_clbk = cls_out_req_rec_clbk,
	.out_req_ack_clbk = cls_out_req_ack_clbk
};

static struct usb_ctl_req_clbks vnd_ctl_req_clbks = {
	.stp_clbk = vnd_stp,
	.in_req_ack_clbk = vnd_in_req_ack_clbk,
	.out_req_rec_clbk = vnd_out_req_rec_clbk,
	.out_req_ack_clbk = vnd_out_req_ack_clbk
};

#if USB_JIG_KEYB_IFACE == 1
static struct keyb_led_report keyb_led_report;
#endif
static struct usb_jiggler_stats stats;
static struct usb_stp_pkt *stp_pkt;
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
static const char *req_err_str = "error";
static const char *req_rej_str = "reject";
static const char *req_done_str = "done";
#endif

static union {
	uint8_t conf;
	uint16_t stat;
        uint8_t alt_iface;
} ctl_rpl;

#if USB_LOG_CTL_REQ_STP_EVENTS == 1 || USB_LOG_CTL_REQ_CMD_EVENTS == 1
static logger_t usb_logger;
static BaseType_t dmy;
#endif

/**
 * init_usb_jiggler
 */
void init_usb_jiggler(void)
{
	const struct usb_endp_desc *ed;
	int ep;

#if USB_JIG_KEYB_IFACE == 1 && LOG_KEYB_LEDS == 1
	keyb_led_rep_que = xQueueCreate(KEYB_LED_REPORT_QUE_SIZE, sizeof(struct keyb_led_report));
	if (keyb_led_rep_que == NULL) {
		crit_err_exit(MALLOC_ERROR);
	}
#endif
	jig_ctl_qset = xQueueCreateSet(UDP_EVNT_QUE_SIZE + JIGBTN_EVNT_QUE_SIZE);
	if (jig_ctl_qset == NULL) {
		crit_err_exit(MALLOC_ERROR);
	}
	add_usb_ctl_req_std_clbks(&std_ctl_req_clbks);
	add_usb_ctl_req_cls_clbks(&cls_ctl_req_clbks);
        add_usb_ctl_req_vnd_clbks(&vnd_ctl_req_clbks);
#if UDP_LOG_INTR_EVENTS == 1 || UDP_LOG_STATE_EVENTS == 1 ||\
    UDP_LOG_ENDP_EVENTS == 1 || UDP_LOG_OUT_IRP_EVENTS == 1 ||\
    UDP_LOG_ERR_EVENTS == 1 || USB_LOG_CTL_REQ_EVENTS == 1 ||\
    USB_LOG_CTL_REQ_STP_EVENTS == 1 || USB_LOG_CTL_REQ_CMD_EVENTS == 1
        logger_t *logger = init_usb_log();
#endif
#if USB_LOG_CTL_REQ_STP_EVENTS == 1 || USB_LOG_CTL_REQ_CMD_EVENTS == 1
	usb_logger = *logger;
#endif
#if USB_LOG_CTL_REQ_EVENTS == 1
        init_usb_ctl_req(logger);
#else
	init_usb_ctl_req(NULL);
#endif
	while (TRUE) {
		if (!(ed = find_usb_endp_desc(&conf_descs, sizeof(conf_descs)))) {
			break;
		}
		if ((ep = ed->b_endpoint_address & 0x0F) < UDP_EP_NMB) {
			init_udp_endp_que(ep);
		} else {
			crit_err_exit(BAD_PARAMETER);
		}
	}
	add_udp_evnt_que_to_qset(jig_ctl_qset);
#if UDP_LOG_INTR_EVENTS == 1 || UDP_LOG_STATE_EVENTS == 1 || UDP_LOG_ENDP_EVENTS == 1 ||\
    UDP_LOG_OUT_IRP_EVENTS == 1 || UDP_LOG_ERR_EVENTS == 1
	init_udp(logger);
#else
	init_udp(NULL);
#endif
}

/**
 * is_self_powered
 */
static boolean_t is_self_powered(void)
{
	return (FALSE);
}

/**
 * std_stp
 */
static struct usb_ctl_req std_stp(struct usb_stp_pkt *sp)
{
	struct usb_ctl_req ucr = {.valid = FALSE};
	enum usb_ctl_req_recp recp;
#if USB_LOG_CTL_REQ_STP_EVENTS == 1
	log_stp_event(sp);
#endif
	stp_pkt = sp;
	recp = stp_pkt->bm_request_type & 0x1F;
        if (stp_pkt->b_request == USB_SET_DESCRIPTOR && recp == USB_DEVICE_RECIPIENT) {
		std_set_desc(&ucr);
	} else if (stp_pkt->b_request == USB_GET_DESCRIPTOR && recp == USB_DEVICE_RECIPIENT) {
		std_get_desc_dev(&ucr);
	} else if (stp_pkt->b_request == USB_GET_DESCRIPTOR && recp == USB_IFACE_RECIPIENT) {
		std_get_desc_ifc(&ucr);
	} else if (stp_pkt->b_request == USB_SET_ADDRESS && recp == USB_DEVICE_RECIPIENT) {
		std_set_addr(&ucr);
	} else if (stp_pkt->b_request == USB_SET_CONFIGURATION && recp == USB_DEVICE_RECIPIENT) {
		std_set_conf(&ucr);
	} else if (stp_pkt->b_request == USB_GET_CONFIGURATION && recp == USB_DEVICE_RECIPIENT) {
		std_get_conf(&ucr);
	} else if (stp_pkt->b_request == USB_SET_INTERFACE && recp == USB_IFACE_RECIPIENT) {
		std_set_iface(&ucr);
	} else if (stp_pkt->b_request == USB_GET_INTERFACE && recp == USB_IFACE_RECIPIENT) {
		std_get_iface(&ucr);
	} else if (stp_pkt->b_request == USB_SYNCH_FRAME && recp == USB_ENDP_RECIPIENT) {
		std_synch_frm(&ucr);
	} else if (stp_pkt->b_request == USB_GET_STATUS && recp == USB_DEVICE_RECIPIENT) {
		std_get_dev_stat(&ucr);
	} else if (stp_pkt->b_request == USB_GET_STATUS && recp == USB_IFACE_RECIPIENT) {
		std_get_iface_stat(&ucr);
	} else if (stp_pkt->b_request == USB_GET_STATUS && recp == USB_ENDP_RECIPIENT) {
		std_get_endp_stat(&ucr);
	} else if (stp_pkt->b_request == USB_CLEAR_FEATURE && recp == USB_DEVICE_RECIPIENT) {
		std_clr_dev_feat(&ucr);
	} else if (stp_pkt->b_request == USB_CLEAR_FEATURE && recp == USB_IFACE_RECIPIENT) {
		std_clr_set_iface_feat(&ucr);
	} else if (stp_pkt->b_request == USB_CLEAR_FEATURE && recp == USB_ENDP_RECIPIENT) {
		std_clr_set_endp_feat(&ucr);
	} else if (stp_pkt->b_request == USB_SET_FEATURE && recp == USB_DEVICE_RECIPIENT) {
		std_set_dev_feat(&ucr);
	} else if (stp_pkt->b_request == USB_SET_FEATURE && recp == USB_IFACE_RECIPIENT) {
		std_clr_set_iface_feat(&ucr);
	} else if (stp_pkt->b_request == USB_SET_FEATURE && recp == USB_ENDP_RECIPIENT) {
		std_clr_set_endp_feat(&ucr);
	} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		log_std_cmd_event(req_err_str);
#endif
		stats.stp_err_cnt++;
	}
	return (ucr);
}

/**
 * std_get_desc_dev
 */
static void std_get_desc_dev(struct usb_ctl_req *ucr)
{
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	const char *txt;
#endif
	switch (stp_pkt->w_value >> 8) {
	case USB_DEV_DESC :
		if ((stp_pkt->w_value & 0xFF) == 0 && stp_pkt->w_index == 0) {
			ucr->valid = TRUE;
			ucr->buf = (uint8_t *) &dev_desc;
			if (stp_pkt->w_length > sizeof(dev_desc)) {
				ucr->nmb = sizeof(dev_desc);
			} else {
				ucr->nmb = stp_pkt->w_length;
			}
			ucr->trans_nmb = stp_pkt->w_length;
			ucr->trans_dir = UDP_CTL_TRANS_IN;
			return;
		} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
			txt = req_err_str;
#endif
			stats.stp_err_cnt++;
		}
		break;
	case USB_DEV_QUAL_DESC :
		if ((stp_pkt->w_value & 0xFF) != 0 || stp_pkt->w_index != 0) {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
			txt = req_err_str;
#endif
			stats.stp_err_cnt++;
		} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
			txt = "dev_qual_desc unsupported";
#endif
			stats.stp_rej_cnt++;
		}
		break;
	case USB_CONF_DESC :
		if ((stp_pkt->w_value & 0xFF) == 0 && stp_pkt->w_index == 0) {
			ucr->valid = TRUE;
			ucr->buf = (uint8_t *) &conf_descs;
			if (stp_pkt->w_length > sizeof(conf_descs)) {
				ucr->nmb = sizeof(conf_descs);
			} else {
				ucr->nmb = stp_pkt->w_length;
			}
			ucr->trans_nmb = stp_pkt->w_length;
			ucr->trans_dir = UDP_CTL_TRANS_IN;
			return;
		} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
			txt = req_err_str;
#endif
			stats.stp_err_cnt++;
		}
		break;
	case USB_ALT_SPEED_CONF_DESC :
		if ((stp_pkt->w_value & 0xFF) != 0 || stp_pkt->w_index != 0) {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
			txt = req_err_str;
#endif
			stats.stp_err_cnt++;
		} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
			txt = "alt_speed_conf_desc unsupported";
#endif
			stats.stp_rej_cnt++;
		}
		break;
	case USB_STR_DESC :
		if ((stp_pkt->w_value & 0xFF) == 0) {
			if (stp_pkt->w_index == 0) {
				ucr->valid = TRUE;
				ucr->buf = (uint8_t *) str_desc_arry[0];
				if (stp_pkt->w_length > *str_desc_arry[0]) {
					ucr->nmb = *str_desc_arry[0];
				} else {
					ucr->nmb = stp_pkt->w_length;
				}
				ucr->trans_nmb = stp_pkt->w_length;
				ucr->trans_dir = UDP_CTL_TRANS_IN;
				return;
			}
		} else {
			uint8_t idx = stp_pkt->w_value;
			if (idx < sizeof(str_desc_arry) / sizeof(const uint8_t *) &&
			    check_lng_code(stp_pkt->w_index)) {
				ucr->valid = TRUE;
				ucr->buf = (uint8_t *) str_desc_arry[idx];
				if (stp_pkt->w_length > *str_desc_arry[idx]) {
					ucr->nmb = *str_desc_arry[idx];
				} else {
					ucr->nmb = stp_pkt->w_length;
				}
				ucr->trans_nmb = stp_pkt->w_length;
				ucr->trans_dir = UDP_CTL_TRANS_IN;
				return;
			}
		}
                /* FALLTHRU */
	default :
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		txt = req_err_str;
#endif
		stats.stp_err_cnt++;
		break;
	}
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_std_cmd_event(txt);
#endif
}

/**
 * std_get_desc_ifc
 */
static void std_get_desc_ifc(struct usb_ctl_req *ucr)
{
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	const char *txt;
#endif
	switch (stp_pkt->w_value >> 8) {
	case USB_HID_REPORT_DESC :
		if ((stp_pkt->w_value & 0xFF) == 0 && stp_pkt->w_index == 0) {
			ucr->valid = TRUE;
			ucr->buf = (uint8_t *) m_rep_desc;
			if (stp_pkt->w_length > sizeof(m_rep_desc)) {
				ucr->nmb = sizeof(m_rep_desc);
			} else {
				ucr->nmb = stp_pkt->w_length;
			}
			ucr->trans_nmb = stp_pkt->w_length;
			ucr->trans_dir = UDP_CTL_TRANS_IN;
			return;
#if USB_JIG_KEYB_IFACE == 1
		} else if ((stp_pkt->w_value & 0xFF) == 0 && stp_pkt->w_index == 1) {
			ucr->valid = TRUE;
			ucr->buf = (uint8_t *) k_rep_desc;
			if (stp_pkt->w_length > sizeof(k_rep_desc)) {
				ucr->nmb = sizeof(k_rep_desc);
			} else {
				ucr->nmb = stp_pkt->w_length;
			}
			ucr->trans_nmb = stp_pkt->w_length;
			ucr->trans_dir = UDP_CTL_TRANS_IN;
			return;
#endif
		} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
			txt = req_err_str;
#endif
			stats.stp_err_cnt++;
		}
		break;
	case USB_HID_PHYSICAL_DESC :
#if USB_JIG_KEYB_IFACE == 1
		if (stp_pkt->w_index != 0 && stp_pkt->w_index != 1) {
#else
		if (stp_pkt->w_index != 0) {
#endif
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
			txt = req_err_str;
#endif
			stats.stp_err_cnt++;
		} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
			txt = "hid_physical_desc unsupported";
#endif
			stats.stp_rej_cnt++;
		}
		break;
	default :
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		txt = req_err_str;
#endif
		stats.stp_err_cnt++;
		break;
	}
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_std_cmd_event(txt);
#endif
}

/**
 * check_lng_code
 */
static boolean_t check_lng_code(uint16_t code)
{
	union {
		uint8_t lc_str[2];
		uint16_t uint;
	} lc;

	lc.lc_str[0] = lang_str_desc[2];
	lc.lc_str[1] = lang_str_desc[3];
	if (lc.uint == code) {
		return (TRUE);
	} else {
		return (FALSE);
	}
}

/**
 * std_set_addr
 */
static void std_set_addr(struct usb_ctl_req *ucr)
{
	enum udp_state us;

	us = get_udp_state();
	if (stp_pkt->w_value <= 127 && stp_pkt->w_index == 0 && stp_pkt->w_length == 0 &&
	    (us == UDP_STATE_DEFAULT || us == UDP_STATE_ADDRESSED)) {
		ucr->valid = TRUE;
		ucr->trans_dir = UDP_CTL_TRANS_OUT;
	} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		log_std_cmd_event(req_err_str);
#endif
		stats.stp_err_cnt++;
	}
}

/**
 * std_set_conf
 */
static void std_set_conf(struct usb_ctl_req *ucr)
{
	enum udp_state us;
        const struct usb_endp_desc *ed;

	us = get_udp_state();
	ucr->valid = TRUE;
	ucr->trans_dir = UDP_CTL_TRANS_OUT;
        if (stp_pkt->w_index != 0 || stp_pkt->w_length != 0) {
		goto err_exit;
	}
	if (us == UDP_STATE_ADDRESSED && stp_pkt->w_value == 0) {
		return;
	} else if (us == UDP_STATE_ADDRESSED && stp_pkt->w_value == 1) {
		while (TRUE) {
			if (!(ed = find_usb_endp_desc(&conf_descs, sizeof(conf_descs)))) {
				break;
			}
			enable_udp_endp(ed->b_endpoint_address & 0x0F, usb_endp_desc_get_ep_type(ed));
		}
		set_udp_confg(TRUE);
		return;
	} else if (us == UDP_STATE_CONFIGURED && stp_pkt->w_value == 0) {
		while (TRUE) {
			if (!(ed = find_usb_endp_desc(&conf_descs, sizeof(conf_descs)))) {
				break;
			}
			disable_udp_endp(ed->b_endpoint_address & 0x0F);
		}
		set_udp_confg(FALSE);
		return;
	} else if (us == UDP_STATE_CONFIGURED && stp_pkt->w_value == 1) {
		while (TRUE) {
			if (!(ed = find_usb_endp_desc(&conf_descs, sizeof(conf_descs)))) {
				break;
			}
			int ep = ed->b_endpoint_address & 0x0F;
			if (is_udp_endp_enabled(ep)) {
				un_halt_udp_endp(ep);
			}
		}
		return;
	}
err_exit:
        ucr->valid = FALSE;
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_std_cmd_event(req_err_str);
#endif
	stats.stp_err_cnt++;
}

/**
 * std_get_conf
 */
static void std_get_conf(struct usb_ctl_req *ucr)
{
	enum udp_state us;

	us = get_udp_state();
	if (stp_pkt->w_value == 0 && stp_pkt->w_index == 0 && stp_pkt->w_length == 1 &&
	    (us == UDP_STATE_ADDRESSED || us == UDP_STATE_CONFIGURED)) {
		if (us == UDP_STATE_CONFIGURED) {
			ctl_rpl.conf = 1;
		} else {
                	ctl_rpl.conf = 0;
		}
		ucr->valid = TRUE;
		ucr->buf = (uint8_t *) &ctl_rpl;
		ucr->nmb = 1;
		ucr->trans_nmb = 1;
		ucr->trans_dir = UDP_CTL_TRANS_IN;
	} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		log_std_cmd_event(req_err_str);
#endif
		stats.stp_err_cnt++;
	}
}

/**
 * std_set_desc
 */
static void std_set_desc(struct usb_ctl_req *ucr)
{
	enum udp_state us;
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	const char *txt;
#endif
	us = get_udp_state();
        if (us == UDP_STATE_ADDRESSED || us == UDP_STATE_CONFIGURED) {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		txt = req_rej_str;
#endif
		stats.stp_rej_cnt++;
	} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		txt = req_err_str;
#endif
		stats.stp_err_cnt++;
	}
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_std_cmd_event(txt);
#endif
}

/**
 * std_set_iface
 */
static void std_set_iface(struct usb_ctl_req *ucr)
{
	enum udp_state us;
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	const char *txt;
#endif
	us = get_udp_state();
        if (us == UDP_STATE_CONFIGURED && stp_pkt->w_length == 0) {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		txt = req_rej_str;
#endif
		stats.stp_rej_cnt++;
	} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		txt = req_err_str;
#endif
		stats.stp_err_cnt++;
	}
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_std_cmd_event(txt);
#endif
}

/**
 * std_get_iface
 */
static void std_get_iface(struct usb_ctl_req *ucr)
{
	enum udp_state us;

	us = get_udp_state();
        if (stp_pkt->w_value == 0 && stp_pkt->w_length == 1 &&
	    stp_pkt->w_index < conf_descs.conf_desc.b_num_interfaces &&
	    us == UDP_STATE_CONFIGURED) {
		ctl_rpl.alt_iface = 0;
		ucr->valid = TRUE;
		ucr->buf = (uint8_t *) &ctl_rpl;
		ucr->nmb = 1;
		ucr->trans_nmb = 1;
		ucr->trans_dir = UDP_CTL_TRANS_IN;
	} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		log_std_cmd_event(req_err_str);
#endif
		stats.stp_err_cnt++;
	}
}

/**
 * std_synch_frm
 */
static void std_synch_frm(struct usb_ctl_req *ucr)
{
	enum udp_state us;
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	const char *txt;
#endif
	us = get_udp_state();
        if (stp_pkt->w_value == 0 && stp_pkt->w_length == 2 && us == UDP_STATE_CONFIGURED) {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		txt = req_rej_str;
#endif
		stats.stp_rej_cnt++;
	} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		txt = req_err_str;
#endif
		stats.stp_err_cnt++;
	}
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_std_cmd_event(txt);
#endif
}

/**
 * std_get_dev_stat
 */
static void std_get_dev_stat(struct usb_ctl_req *ucr)
{
	enum udp_state us;

	us = get_udp_state();
        if (stp_pkt->w_value == 0 && stp_pkt->w_index == 0 && stp_pkt->w_length == 2 &&
	    (us == UDP_STATE_ADDRESSED || us == UDP_STATE_CONFIGURED)) {
		ctl_rpl.stat = 0;
		if (is_self_powered()) {
			ctl_rpl.stat = 1;
		}
		if (get_rmt_wkup_feat()) {
			ctl_rpl.stat |= 2;
		}
		ucr->valid = TRUE;
		ucr->buf = (uint8_t *) &ctl_rpl;
		ucr->nmb = 2;
		ucr->trans_nmb = 2;
		ucr->trans_dir = UDP_CTL_TRANS_IN;
	} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		log_std_cmd_event(req_err_str);
#endif
		stats.stp_err_cnt++;
	}
}

/**
 * std_get_iface_stat
 */
static void std_get_iface_stat(struct usb_ctl_req *ucr)
{
	enum udp_state us;

	us = get_udp_state();
        if (stp_pkt->w_value == 0 && stp_pkt->w_length == 2 &&
	    stp_pkt->w_index < conf_descs.conf_desc.b_num_interfaces &&
	    us == UDP_STATE_CONFIGURED) {
		ctl_rpl.stat = 0;
		ucr->valid = TRUE;
		ucr->buf = (uint8_t *) &ctl_rpl;
		ucr->nmb = 2;
		ucr->trans_nmb = 2;
		ucr->trans_dir = UDP_CTL_TRANS_IN;
	} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		log_std_cmd_event(req_err_str);
#endif
		stats.stp_err_cnt++;
	}
}

/**
 * std_get_endp_stat
 */
static void std_get_endp_stat(struct usb_ctl_req *ucr)
{
	enum udp_state us;
	int ep;

	us = get_udp_state();
        ep = stp_pkt->w_index & 0x0F;
        if (stp_pkt->w_value == 0 && stp_pkt->w_length == 2) {
		if ((us == UDP_STATE_ADDRESSED && ep == 0) ||
		     us == UDP_STATE_CONFIGURED) {
			if (is_endp_index_valid(stp_pkt->w_index)) {
				if (is_udp_endp_halted(ep)) {
					ctl_rpl.stat = 1;
				} else {
					ctl_rpl.stat = 0;
				}
				ucr->valid = TRUE;
				ucr->buf = (uint8_t *) &ctl_rpl;
				ucr->nmb = 2;
				ucr->trans_nmb = 2;
				ucr->trans_dir = UDP_CTL_TRANS_IN;
                                return;
			}
		}
	}
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_std_cmd_event(req_err_str);
#endif
	stats.stp_err_cnt++;
}

/**
 * std_clr_dev_feat
 */
static void std_clr_dev_feat(struct usb_ctl_req *ucr)
{
	enum udp_state us;

	us = get_udp_state();
	if (stp_pkt->w_value == USB_DEV_REM_WKUP_FEAT &&
	    stp_pkt->w_index == 0 && stp_pkt->w_length == 0 &&
	    (us == UDP_STATE_ADDRESSED || us == UDP_STATE_CONFIGURED)) {
		ucr->valid = TRUE;
		ucr->trans_dir = UDP_CTL_TRANS_OUT;
                set_rmt_wkup_feat(FALSE);
	} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		log_std_cmd_event(req_err_str);
#endif
		stats.stp_err_cnt++;
	}
}

/**
 * std_set_dev_feat
 */
static void std_set_dev_feat(struct usb_ctl_req *ucr)
{
	enum udp_state us;
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	const char *txt;
#endif
	us = get_udp_state();
	if (stp_pkt->w_index == 0 && stp_pkt->w_length == 0) {
		if (stp_pkt->w_value == USB_DEV_REM_WKUP_FEAT &&
		    (us == UDP_STATE_ADDRESSED || us == UDP_STATE_CONFIGURED)) {
			ucr->valid = TRUE;
			ucr->trans_dir = UDP_CTL_TRANS_OUT;
                        set_rmt_wkup_feat(TRUE);
			return;
		} else if (stp_pkt->w_value == USB_TEST_MODE_FEAT) {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
			txt = req_rej_str;
#endif
                        stats.stp_rej_cnt++;
		} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
			txt = req_err_str;
#endif
                        stats.stp_err_cnt++;
		}
	} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		txt = req_err_str;
#endif
		stats.stp_err_cnt++;
	}
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_std_cmd_event(txt);
#endif
}

/**
 * std_clr_set_iface_feat
 */
static void std_clr_set_iface_feat(struct usb_ctl_req *ucr)
{
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_std_cmd_event(req_rej_str);
#endif
	stats.stp_rej_cnt++;
}

/**
 * std_clr_set_endp_feat
 */
static void std_clr_set_endp_feat(struct usb_ctl_req *ucr)
{
	enum udp_state us;
	int ep;

	us = get_udp_state();
	ep = stp_pkt->w_index & 0x0F;
	if (stp_pkt->w_value == USB_ENDP_HALT_FEAT &&
	    is_endp_index_valid(stp_pkt->w_index) && ep != 0 &&
	    stp_pkt->w_length == 0 && us == UDP_STATE_CONFIGURED) {
		ucr->valid = TRUE;
		ucr->trans_dir = UDP_CTL_TRANS_OUT;
		if (stp_pkt->b_request == USB_CLEAR_FEATURE) {
			un_halt_udp_endp(ep);
		} else {
			halt_udp_endp(ep);
		}
	} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		log_std_cmd_event(req_err_str);
#endif
		stats.stp_err_cnt++;
	}
}

/**
 * std_in_req_ack_clbk
 */
static void std_in_req_ack_clbk(void)
{
	switch (stp_pkt->b_request) {
	case USB_GET_DESCRIPTOR :
		/* FALLTHRU */
	case USB_GET_STATUS :
		/* FALLTHRU */
        case USB_GET_CONFIGURATION :
		/* FALLTHRU */
        case USB_GET_INTERFACE :
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		log_std_cmd_event(req_done_str);
#endif
		break;
	}
}

/**
 * std_out_req_rec_clbk
 */
static boolean_t std_out_req_rec_clbk(void)
{
	return (FALSE);
}

/**
 * std_out_req_ack_clbk
 */
static void std_out_req_ack_clbk(void)
{
	switch (stp_pkt->b_request) {
	case USB_SET_ADDRESS :
		set_udp_addr(stp_pkt->w_value);
		break;
        case USB_SET_CONFIGURATION :
		/* FALLTHRU */
	case USB_CLEAR_FEATURE :
                /* FALLTHRU */
	case USB_SET_FEATURE :
		break;
	default :
		return;
	}
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_std_cmd_event(req_done_str);
#endif
}

/**
 * cls_stp
 */
static struct usb_ctl_req cls_stp(struct usb_stp_pkt *sp)
{
	struct usb_ctl_req ucr = {.valid = FALSE};
        enum usb_ctl_req_recp recp;

#if USB_LOG_CTL_REQ_STP_EVENTS == 1
	log_stp_event(sp);
#endif
	stp_pkt = sp;
	recp = stp_pkt->bm_request_type & 0x1F;
	if (stp_pkt->b_request == USB_HID_GET_REPORT && recp == USB_IFACE_RECIPIENT) {
		cls_get_report(&ucr);
	} else if (stp_pkt->b_request == USB_HID_GET_IDLE && recp == USB_IFACE_RECIPIENT) {
		cls_get_idle(&ucr);
	} else if (stp_pkt->b_request == USB_HID_GET_PROTOCOL && recp == USB_IFACE_RECIPIENT) {
		cls_get_protocol(&ucr);
	} else if (stp_pkt->b_request ==  USB_HID_SET_REPORT && recp == USB_IFACE_RECIPIENT) {
		cls_set_report(&ucr);
	} else if (stp_pkt->b_request ==  USB_HID_SET_IDLE && recp == USB_IFACE_RECIPIENT) {
		cls_set_idle(&ucr);
	} else if (stp_pkt->b_request ==  USB_HID_SET_PROTOCOL && recp == USB_IFACE_RECIPIENT) {
		cls_set_protocol(&ucr);
	} else {
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	        log_cls_cmd_event(req_err_str);
#endif
		stats.stp_err_cnt++;
	}
	return (ucr);
}

/**
 * cls_get_report
 */
static void cls_get_report(struct usb_ctl_req *ucr)
{
	enum udp_state us;

	us = get_udp_state();
	if ((stp_pkt->w_value >> 8) == USB_HID_REPORT_IN && (stp_pkt->w_value & 0xFF) == 0 &&
	    us == UDP_STATE_CONFIGURED) {
		if (stp_pkt->w_index == 0) {
			ucr->valid = TRUE;
			ucr->buf = (uint8_t *) &mouse_report;
			if (stp_pkt->w_length > sizeof(struct mouse_report)) {
				ucr->nmb = sizeof(struct mouse_report);
			} else {
				ucr->nmb = stp_pkt->w_length;
			}
			ucr->trans_nmb = stp_pkt->w_length;
			ucr->trans_dir = UDP_CTL_TRANS_IN;
			return;
#if USB_JIG_KEYB_IFACE == 1
		} else if (stp_pkt->w_index == 1) {
			ucr->valid = TRUE;
			ucr->buf = (uint8_t *) &keyb_report;
			if (stp_pkt->w_length > sizeof(struct keyb_report)) {
				ucr->nmb = sizeof(struct keyb_report);
			} else {
				ucr->nmb = stp_pkt->w_length;
			}
			ucr->trans_nmb = stp_pkt->w_length;
			ucr->trans_dir = UDP_CTL_TRANS_IN;
			return;
#endif
		}
	}
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_cls_cmd_event(req_err_str);
#endif
	stats.stp_err_cnt++;
}

/**
 * cls_get_idle
 */
static void cls_get_idle(struct usb_ctl_req *ucr)
{
	enum udp_state us;
	static uint8_t rpl;

	us = get_udp_state();
	if (stp_pkt->w_value == 0 && stp_pkt->w_length == 1 && us == UDP_STATE_CONFIGURED) {
		ucr->buf = &rpl;
		ucr->nmb = 1;
		ucr->trans_nmb = 1;
		ucr->trans_dir = UDP_CTL_TRANS_IN;
		if (stp_pkt->w_index == 0) {
			rpl = 0;
			ucr->valid = TRUE;
                        return;
#if USB_JIG_KEYB_IFACE == 1
		} else if (stp_pkt->w_index == 1) {
			rpl = 0;
			ucr->valid = TRUE;
                        return;
#endif
		}
	}
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_cls_cmd_event(req_err_str);
#endif
	stats.stp_err_cnt++;
}

/**
 * cls_get_protocol
 */
static void cls_get_protocol(struct usb_ctl_req *ucr)
{
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_cls_cmd_event(req_rej_str);
#endif
	stats.stp_rej_cnt++;
}

/**
 * cls_set_report
 */
static void cls_set_report(struct usb_ctl_req *ucr)
{
#if USB_JIG_KEYB_IFACE == 1
	enum udp_state us;

	us = get_udp_state();
	if ((stp_pkt->w_value >> 8) == USB_HID_REPORT_OUT && (stp_pkt->w_value & 0xFF) == 0 &&
	    stp_pkt->w_index == 1 && stp_pkt->w_length == sizeof(struct keyb_led_report) &&
	    us == UDP_STATE_CONFIGURED) {
		ucr->valid = TRUE;
		ucr->buf = (uint8_t *) &keyb_led_report;
		ucr->nmb = stp_pkt->w_length;
		ucr->trans_dir = UDP_CTL_TRANS_OUT;
		return;
	}
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_cls_cmd_event(req_err_str);
#endif
	stats.stp_err_cnt++;
#else
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_cls_cmd_event(req_rej_str);
#endif
	stats.stp_rej_cnt++;
#endif
}

/**
 * cls_set_idle
 */
static void cls_set_idle(struct usb_ctl_req *ucr)
{
	enum udp_state us;

	us = get_udp_state();
	if ((stp_pkt->w_value == 0 && stp_pkt->w_length == 0 && us == UDP_STATE_CONFIGURED)) {
		if (stp_pkt->w_index == 0) {
			ucr->valid = TRUE;
			ucr->trans_dir = UDP_CTL_TRANS_OUT;
                        return;
#if USB_JIG_KEYB_IFACE == 1
		} else if (stp_pkt->w_index == 1) {
			ucr->valid = TRUE;
			ucr->trans_dir = UDP_CTL_TRANS_OUT;
                        return;
#endif
		}
	}
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_cls_cmd_event(req_err_str);
#endif
	stats.stp_err_cnt++;
}

/**
 * cls_set_protocol
 */
static void cls_set_protocol(struct usb_ctl_req *ucr)
{
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
	log_cls_cmd_event(req_rej_str);
#endif
	stats.stp_rej_cnt++;
}

/**
 * cls_in_req_ack_clbk
 */
static void cls_in_req_ack_clbk(void)
{
	switch (stp_pkt->b_request) {
	case USB_HID_GET_REPORT :
		/* FALLTHRU */
	case USB_HID_GET_IDLE :
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		log_cls_cmd_event(req_done_str);
#endif
		break;
	}
}

/**
 * cls_out_req_rec_clbk
 */
static boolean_t cls_out_req_rec_clbk(void)
{
	switch (stp_pkt->b_request) {
#if USB_JIG_KEYB_IFACE == 1
	case USB_HID_SET_REPORT :
#if LOG_KEYB_LEDS == 1
		xQueueSendFromISR(keyb_led_rep_que, &keyb_led_report, NULL);
#endif
		return (TRUE);
#endif
	default :
		return (FALSE);
	}
}

/**
 * cls_out_req_ack_clbk
 */
static void cls_out_req_ack_clbk(void)
{
	switch (stp_pkt->b_request) {
#if USB_JIG_KEYB_IFACE == 1
	case USB_HID_SET_REPORT :
		/* FALLTHRU */
	case USB_HID_SET_IDLE :
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		log_cls_cmd_event(req_done_str);
#endif
		break;
#endif
	default :
		break;
	}
}

/**
 * vnd_stp
 */
static struct usb_ctl_req vnd_stp(struct usb_stp_pkt *sp)
{
	return ((struct usb_ctl_req){.valid = FALSE});
}

/**
 * vnd_in_req_ack_clbk
 */
static void vnd_in_req_ack_clbk(void)
{
}

/**
 * vnd_out_req_rec_clbk
 */
static boolean_t vnd_out_req_rec_clbk(void)
{
	return (FALSE);
}

/**
 * vnd_out_req_ack_clbk
 */
static void vnd_out_req_ack_clbk(void)
{
}

/**
 * is_endp_index_valid
 */
static boolean_t is_endp_index_valid(int w_index)
{
	int n;

	n = w_index & 0x0F;
	if (is_udp_endp_enabled(n)) {
		if (!n) {
			return (TRUE);
		}
		if (w_index & 0x80) {
			if (get_udp_endp_dir(n) == UDP_ENDP_DIR_IN) {
				return (TRUE);
			}
		} else {
			if (get_udp_endp_dir(n) == UDP_ENDP_DIR_OUT) {
				return (TRUE);
			}
		}
	}
	return (FALSE);
}

#if USB_LOG_CTL_REQ_STP_EVENTS == 1 || USB_LOG_CTL_REQ_CMD_EVENTS == 1
static const struct txt_item std_ctl_req_code_str_arry[] = {
	{USB_GET_STATUS, "get_stat"},
	{USB_CLEAR_FEATURE, "clr_feat"},
	{USB_SET_FEATURE, "set_feat"},
	{USB_SET_ADDRESS, "set_addr"},
	{USB_GET_DESCRIPTOR, "get_desc"},
	{USB_SET_DESCRIPTOR, "set_desc"},
	{USB_GET_CONFIGURATION, "get_conf"},
	{USB_SET_CONFIGURATION, "set_conf"},
	{USB_GET_INTERFACE, "get_iface"},
	{USB_SET_INTERFACE, "set_iface"},
	{USB_SYNCH_FRAME, "sync_frm"},
	{0, NULL}
};

static const struct txt_item cls_ctl_req_code_str_arry[] = {
	{USB_HID_GET_REPORT, "hid_get_report"},
	{USB_HID_GET_IDLE, "hid_get_idle"},
	{USB_HID_GET_PROTOCOL, "hid_get_protocol"},
	{USB_HID_SET_REPORT, "hid_set_report"},
	{USB_HID_SET_IDLE, "hid_set_idle"},
	{USB_HID_SET_PROTOCOL, "hid_set_protocol"},
	{0, NULL}
};

static const struct txt_item ctl_req_recp_str_arry[] = {
	{USB_DEVICE_RECIPIENT, "dev"},
        {USB_IFACE_RECIPIENT, "ifc"},
        {USB_ENDP_RECIPIENT, "edp"},
        {USB_OTHER_RECIPIENT, "oth"},
	{0, NULL}
};
#endif

#if USB_LOG_CTL_REQ_STP_EVENTS == 1
/**
 * fmt_usb_ctl_req_stp_event
 */
static void fmt_usb_ctl_req_stp_event(struct usb_ctl_req_stp_event *p);
static void fmt_usb_ctl_req_stp_event(struct usb_ctl_req_stp_event *p)
{
	if (((p->stp_pkt.bm_request_type >> 5) & 3) == USB_STANDARD_REQUEST) {
		msg(INF, "usb_jiggler.c: %sstd[%s] rcp=%s val=0x%.4hX ind=0x%.4hX len=%hu\n",
		    (p->stp_pkt.bm_request_type & (1 << 7)) ? ">" : "<",
		    find_txt_item(p->stp_pkt.b_request, std_ctl_req_code_str_arry, "undef"),
		    find_txt_item(p->stp_pkt.bm_request_type & 0x1F, ctl_req_recp_str_arry, "undef"),
		    p->stp_pkt.w_value, p->stp_pkt.w_index, p->stp_pkt.w_length);
	} else if (((p->stp_pkt.bm_request_type >> 5) & 3) == USB_CLASS_REQUEST) {
		msg(INF, "usb_jiggler.c: %scls[%s] rcp=%s val=0x%.4hX ind=0x%.4hX len=%hu\n",
		    (p->stp_pkt.bm_request_type & (1 << 7)) ? ">" : "<",
		    find_txt_item(p->stp_pkt.b_request, cls_ctl_req_code_str_arry, "undef"),
		    find_txt_item(p->stp_pkt.bm_request_type & 0x1F, ctl_req_recp_str_arry, "undef"),
		    p->stp_pkt.w_value, p->stp_pkt.w_index, p->stp_pkt.w_length);
	} else {
		msg(INF, "usb_jiggler.c: %svnd req=%hhu val=0x%.4hX ind=0x%.4hX len=%hu\n",
		    (p->stp_pkt.bm_request_type & (1 << 7)) ? ">" : "<",
		    p->stp_pkt.b_request, p->stp_pkt.w_value, p->stp_pkt.w_index,
		    p->stp_pkt.w_length);
	}
}

static struct usb_ctl_req_stp_event ucrse = {
	.type = USB_CTL_REQ_STP_EVENT_TYPE,
	.fmt = fmt_usb_ctl_req_stp_event
};

/**
 * log_stp_event
 */
static void log_stp_event(struct usb_stp_pkt *sp)
{
	ucrse.stp_pkt = *sp;
	if (pdTRUE != xQueueSendFromISR(usb_logger.que, &ucrse, &dmy)) {
		usb_logger.que_err();
	}
}
#endif

#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
/**
 * fmt_usb_ctl_req_cmd_event
 */
static void fmt_usb_ctl_req_cmd_event(struct usb_ctl_req_cmd_event *p);
static void fmt_usb_ctl_req_cmd_event(struct usb_ctl_req_cmd_event *p)
{
	if (p->ctl_req_type == USB_STANDARD_REQUEST) {
		msg(INF, "usb_jiggler.c: [%s]=%s\n",
		    find_txt_item(p->ctl_req_code, std_ctl_req_code_str_arry, "undef"),
 	            p->txt);
	} else if (p->ctl_req_type == USB_CLASS_REQUEST) {
		msg(INF, "usb_jiggler.c: [%s]=%s\n",
		    find_txt_item(p->ctl_req_code, cls_ctl_req_code_str_arry, "undef"),
 	            p->txt);
	} else {
		msg(INF, "usb_jiggler.c: %s\n", p->txt);
	}
}

static struct usb_ctl_req_cmd_event ucree = {
	.type = USB_CTL_REQ_CMD_EVENT_TYPE,
	.fmt = fmt_usb_ctl_req_cmd_event
};

/**
 * log_std_cmd_event
 */
static void log_std_cmd_event(const char *txt)
{
	ucree.ctl_req_type = USB_STANDARD_REQUEST;
	ucree.ctl_req_code = stp_pkt->b_request;
	ucree.txt = txt;
	if (pdTRUE != xQueueSendFromISR(usb_logger.que, &ucree, &dmy)) {
		usb_logger.que_err();
	}
}

/**
 * log_cls_cmd_event
 */
static void log_cls_cmd_event(const char *txt)
{
	ucree.ctl_req_type = USB_CLASS_REQUEST;
	ucree.ctl_req_code = stp_pkt->b_request;
	ucree.txt = txt;
	if (pdTRUE != xQueueSendFromISR(usb_logger.que, &ucree, &dmy)) {
		usb_logger.que_err();
	}
}
#endif

/**
 * get_usb_jiggler_stats
 */
struct usb_jiggler_stats *get_usb_jiggler_stats(void)
{
	return (&stats);
}

#if TERMOUT == 1
/**
 * log_usb_jiggler_stats
 */
void log_usb_jiggler_stats(void)
{
	if (stats.stp_err_cnt) {
		msg(INF, "usb_jiggler.c: stp_err=%hu\n", stats.stp_err_cnt);
	}
	if (stats.stp_rej_cnt) {
		msg(INF, "usb_jiggler.c: stp_rej=%hu\n", stats.stp_rej_cnt);
	}
}
#endif
