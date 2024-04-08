/*
 * usb_jiggler.h
 *
 * Copyright (c) 2023 Jan Rusnak <jan@rusnak.sk>
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

#ifndef USB_JIGGLER_H
#define USB_JIGGLER_H

struct mouse_report {
	uint8_t bm;
	int8_t x;
	int8_t y;
	int8_t w;
} __attribute__ ((__packed__));

#if USB_JIG_KEYB_IFACE == 1
#define KEYB_REPORT_KEY_ARY_SIZE 6

struct keyb_report {
	uint8_t mod;
	uint8_t res;
	uint8_t keys[KEYB_REPORT_KEY_ARY_SIZE];
} __attribute__ ((__packed__));

struct keyb_led_report {
	uint8_t leds;
} __attribute__ ((__packed__));
#endif

#define USB_CTL_REQ_STP_EVENT_TYPE 10

struct usb_ctl_req_stp_event {
	int8_t type;
	struct usb_stp_pkt stp_pkt;
        void (*fmt)(struct usb_ctl_req_stp_event *);
};

#define USB_CTL_REQ_CMD_EVENT_TYPE 11

struct usb_ctl_req_cmd_event {
	int8_t type;
        int8_t ctl_req_type;
	int8_t ctl_req_code;
        const char *txt;
        void (*fmt)(struct usb_ctl_req_cmd_event *);
};

struct usb_jiggler_stats {
        unsigned short stp_err_cnt;
	unsigned short stp_rej_cnt;
};

extern struct mouse_report mouse_report;
#if USB_JIG_KEYB_IFACE == 1
extern struct keyb_report keyb_report;
#if LOG_KEYB_LEDS == 1
extern QueueSetHandle_t keyb_led_rep_que;
#endif
#endif
extern QueueSetHandle_t jig_ctl_qset;

/**
 * init_usb_jiggler
 */
void init_usb_jiggler(void);

/**
 * get_usb_jiggler_stats
 */
struct usb_jiggler_stats *get_usb_jiggler_stats(void);

#if TERMOUT == 1
/**
 * log_usb_jiggler_stats
 */
void log_usb_jiggler_stats(void);
#endif

#endif
