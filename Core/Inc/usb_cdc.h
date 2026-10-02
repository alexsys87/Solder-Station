/**
 * @file    usb_cdc.h
 * @brief   USB CDC ACM device (virtual COM port) on USB OTG FS, PA11/PA12.
 *          Register level, no HAL / USB middleware.
 */
#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdint.h>
#include <stdbool.h>

void     usb_cdc_init(void);
bool     usb_cdc_configured(void);         /* enumerated by the host           */
bool     usb_cdc_dtr(void);                /* host opened the port (DTR set)   */

/* Non-blocking, returns the number of bytes accepted / read */
uint32_t usb_cdc_write(const void *data, uint32_t len);
uint32_t usb_cdc_tx_free(void);
uint32_t usb_cdc_read(void *data, uint32_t max);

#endif /* USB_CDC_H */
