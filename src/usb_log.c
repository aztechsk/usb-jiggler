/*
 * usb_log.c
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

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>
#include <gentyp.h>
#include "sysconf.h"
#include "msgconf.h"
#include "criterr.h"
#include "udp.h"
#include "usb_ctl_req.h"
#include "usb_cdc_def.h"
#include "usb_jiggler.h"
#include "usb_log.h"

#if UDP_LOG_INTR_EVENTS == 1 || UDP_LOG_STATE_EVENTS == 1 ||\
    UDP_LOG_ENDP_EVENTS == 1 || UDP_LOG_OUT_IRP_EVENTS == 1 ||\
    UDP_LOG_ERR_EVENTS == 1 || USB_LOG_CTL_REQ_EVENTS == 1 ||\
    USB_LOG_CTL_REQ_STP_EVENTS == 1 || USB_LOG_CTL_REQ_CMD_EVENTS == 1

static TaskHandle_t hndl;
static const char *nm = "USBLOG";

static union log_entry {
	int8_t type;
	struct udp_intr_event udp_intr_event;
	struct udp_state_event udp_state_event;
        struct udp_endp_event udp_endp_event;
	struct udp_out_irp_event udp_out_irp_event;
        struct udp_err_event udp_err_event;
        struct usb_ctl_req_event usb_ctl_req_event;
	struct usb_ctl_req_stp_event usb_ctl_req_stp_event;
	struct usb_ctl_req_cmd_event usb_ctl_req_cmd_event;
} log_entry;

static logger_t usb_logger;
static unsigned int qfull_cnt;

static void inc_qfull_cnt(void);
static void tsk(void *p);

/**
 * init_usb_log
 */
logger_t *init_usb_log(void)
{
        usb_logger.que = xQueueCreate(USB_LOG_EVENTS_QUEUE_SIZE, sizeof(union log_entry));
        if (usb_logger.que == NULL) {
                crit_err_exit(MALLOC_ERROR);
        }
	usb_logger.que_err = inc_qfull_cnt;
        if (pdPASS != xTaskCreate(tsk, nm, USB_LOG_EVENTS_TASK_STACK_SIZE, NULL,
				  USB_LOG_EVENTS_TASK_PRIO, &hndl)) {
                crit_err_exit(MALLOC_ERROR);
        }
	return (&usb_logger);
}

/**
 * log_usb_log_stats
 */
void log_usb_log_stats(void)
{
	if (qfull_cnt) {
		msg(INF, "usb_log.c: log_usb_que_full=%u\n", qfull_cnt);
	}
}

/**
 * inc_qfull_cnt
 */
static void inc_qfull_cnt(void)
{
	qfull_cnt++;
}

/**
 * tsk
 */
static void tsk(void *p)
{
	while (TRUE) {
		xQueueReceive(usb_logger.que, &log_entry, portMAX_DELAY);
		switch (log_entry.type) {
#if UDP_LOG_INTR_EVENTS == 1
		case UDP_INTR_EVENT_TYPE              :
			(*log_entry.udp_intr_event.fmt)(&log_entry.udp_intr_event);
			break;
#endif
#if UDP_LOG_STATE_EVENTS == 1
		case UDP_STATE_EVENT_TYPE             :
			(*log_entry.udp_state_event.fmt)(&log_entry.udp_state_event);
			break;
#endif
#if UDP_LOG_ENDP_EVENTS == 1
		case UDP_ENDP_EVENT_TYPE              :
			(*log_entry.udp_endp_event.fmt)(&log_entry.udp_endp_event);
			break;
#endif
#if UDP_LOG_OUT_IRP_EVENTS == 1
		case UDP_OUT_IRP_EVENT_TYPE           :
			(*log_entry.udp_out_irp_event.fmt)(&log_entry.udp_out_irp_event);
			break;
#endif
#if UDP_LOG_ERR_EVENTS == 1
		case UDP_ERR_EVENT_TYPE               :
			(*log_entry.udp_err_event.fmt)(&log_entry.udp_err_event);
			break;
#endif
#if USB_LOG_CTL_REQ_EVENTS == 1
		case USB_CTL_REQ_EVENT_TYPE           :
			(*log_entry.usb_ctl_req_event.fmt)(&log_entry.usb_ctl_req_event);
			break;
#endif
#if USB_LOG_CTL_REQ_STP_EVENTS == 1
		case USB_CTL_REQ_STP_EVENT_TYPE       :
			(*log_entry.usb_ctl_req_stp_event.fmt)(&log_entry.usb_ctl_req_stp_event);
			break;
#endif
#if USB_LOG_CTL_REQ_CMD_EVENTS == 1
		case USB_CTL_REQ_CMD_EVENT_TYPE       :
			(*log_entry.usb_ctl_req_cmd_event.fmt)(&log_entry.usb_ctl_req_cmd_event);
			break;
#endif
		default :
			msg(INF, "usb_log.c: unknown log event\n");
			break;
		}
	}
}
#endif
