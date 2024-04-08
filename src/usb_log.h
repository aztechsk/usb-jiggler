/*
 * usb_log.h
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

#ifndef USB_LOG_H
#define USB_LOG_H

#if UDP_LOG_INTR_EVENTS == 1 || UDP_LOG_STATE_EVENTS == 1 ||\
    UDP_LOG_ENDP_EVENTS == 1 || UDP_LOG_OUT_IRP_EVENTS == 1 ||\
    UDP_LOG_ERR_EVENTS == 1 || USB_LOG_CTL_REQ_EVENTS == 1 ||\
    USB_LOG_CTL_REQ_STP_EVENTS == 1 || USB_LOG_CTL_REQ_CMD_EVENTS == 1

/**
 * init_usb_log
 */
logger_t *init_usb_log(void);

/**
 * log_usb_log_stats
 */
void log_usb_log_stats(void);
#endif

#endif
