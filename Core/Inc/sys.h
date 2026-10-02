/**
 * @file    sys.h
 * @brief   System services: clock tree, 1 ms tick, watchdog, hardware CRC.
 */
#ifndef SYS_H
#define SYS_H

#include <stdint.h>
#include <stdbool.h>

void     sys_clock_init(void);
bool     sys_hse_ok(void);
void     sys_tick_init(void);
uint32_t sys_ms(void);
void     sys_delay_ms(uint32_t ms);

void     sys_gpio_init(void);

void     sys_wdg_init(void);
void     sys_wdg_feed(void);
bool     sys_reset_by_wdg(void);

uint32_t sys_crc32(const void *data, uint32_t len_words);

/* Critical section helpers (nesting safe) */
uint32_t sys_irq_save(void);
void     sys_irq_restore(uint32_t state);

#endif /* SYS_H */
