/**
 * @file    rtc.h
 * @brief   Real time clock (STM32 RTC, LSE 32.768 kHz, kept by VBAT).
 */
#ifndef RTC_H
#define RTC_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t hour;       /* 0..23  */
    uint8_t min;        /* 0..59  */
    uint8_t sec;        /* 0..59  */
    uint8_t day;        /* 1..31  */
    uint8_t month;      /* 1..12  */
    uint8_t year;       /* 0..99 -> 2000..2099 */
    uint8_t wday;       /* 1 = Monday .. 7 = Sunday */
} rtc_time_t;

void    rtc_init(void);
bool    rtc_lse_ok(void);
void    rtc_get(rtc_time_t *t);
void    rtc_set(const rtc_time_t *t);
uint8_t rtc_days_in_month(uint8_t month, uint8_t year);

#endif /* RTC_H */
