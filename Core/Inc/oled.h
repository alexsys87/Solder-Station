/**
 * @file    oled.h
 * @brief   SH1106 / SSD1306 128x64 OLED over SPI1 with DMA page transfers.
 */
#ifndef OLED_DRV_H
#define OLED_DRV_H

#include <stdint.h>
#include <stdbool.h>

#define OLED_W      128
#define OLED_H      64
#define OLED_PAGES  (OLED_H / 8)

/* Frame buffer: 8 pages x 128 columns, bit 0 = top pixel of a page */
extern uint8_t oled_fb[OLED_PAGES * OLED_W];

void oled_init(void);
bool oled_busy(void);
bool oled_flush(void);              /* starts a DMA refresh, false if busy */
void oled_wait(void);
void oled_set_contrast(uint8_t value);
void oled_set_flip(bool flip);
void oled_set_power(bool on);

#endif /* OLED_DRV_H */
