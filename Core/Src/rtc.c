/**
 * @file    rtc.c
 * @brief   RTC calendar. The backup domain is initialised only once, so the
 *          time keeps running through resets and, with a battery on VBAT,
 *          through power cycles.
 */
#include "rtc.h"
#include "board.h"
#include "sys.h"

#define RTC_MAGIC           0x51DE7C10UL
#define LSE_TIMEOUT_MS      3000U

static bool s_lse_ok;

static uint8_t bcd2bin(uint32_t v) { return (uint8_t)(((v >> 4) & 0xFU) * 10U + (v & 0xFU)); }
static uint32_t bin2bcd(uint8_t v) { return ((uint32_t)(v / 10U) << 4) | (uint32_t)(v % 10U); }

static void rtc_unlock(void)
{
    RTC->WPR = 0xCAU;
    RTC->WPR = 0x53U;
}

static void rtc_lock(void)
{
    RTC->WPR = 0xFFU;
}

static bool rtc_enter_init(void)
{
    uint32_t start = sys_ms();
    RTC->ISR |= RTC_ISR_INIT;
    while (!(RTC->ISR & RTC_ISR_INITF)) {
        if ((sys_ms() - start) > 100U) return false;
    }
    return true;
}

/* Sakamoto's algorithm, returns 1 = Monday .. 7 = Sunday */
static uint8_t day_of_week(uint16_t y, uint8_t m, uint8_t d)
{
    static const uint8_t t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    uint16_t w;
    if (m < 3U) y--;
    w = (uint16_t)((y + y / 4U - y / 100U + y / 400U + t[m - 1U] + d) % 7U);
    return (uint8_t)(w == 0U ? 7U : w);
}

uint8_t rtc_days_in_month(uint8_t month, uint8_t year)
{
    static const uint8_t dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1U || month > 12U) return 31U;
    if (month == 2U && (year % 4U) == 0U) return 29U;
    return dim[month - 1U];
}

void rtc_init(void)
{
    uint32_t start;
    bool configured;

    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR |= PWR_CR_DBP;                  /* allow backup domain access */

    configured = (RCC->BDCR & RCC_BDCR_RTCEN) && (RTC->BKP0R == RTC_MAGIC);

    if (configured && (RCC->BDCR & RCC_BDCR_RTCSEL) == RCC_BDCR_RTCSEL_0) {
        s_lse_ok = (RCC->BDCR & RCC_BDCR_LSERDY) != 0U;
    }

    if (!configured) {
        rtc_time_t def = { 12U, 0U, 0U, 1U, 1U, 26U, 0U };

        /* Reset the backup domain to be able to select the clock source */
        RCC->BDCR |= RCC_BDCR_BDRST;
        RCC->BDCR &= ~RCC_BDCR_BDRST;

        RCC->BDCR |= RCC_BDCR_LSEON;
        start = sys_ms();
        while (!(RCC->BDCR & RCC_BDCR_LSERDY)) {
            if ((sys_ms() - start) > LSE_TIMEOUT_MS) break;
        }
        s_lse_ok = (RCC->BDCR & RCC_BDCR_LSERDY) != 0U;

        if (s_lse_ok) {
            RCC->BDCR |= RCC_BDCR_RTCSEL_0;                 /* LSE */
        } else {
            /* No crystal: run from LSI (~32 kHz, not accurate) */
            RCC->BDCR &= ~RCC_BDCR_LSEON;
            RCC->CSR  |= RCC_CSR_LSION;
            while (!(RCC->CSR & RCC_CSR_LSIRDY)) { }
            RCC->BDCR |= RCC_BDCR_RTCSEL_1;                 /* LSI */
        }
        RCC->BDCR |= RCC_BDCR_RTCEN;

        rtc_unlock();
        if (rtc_enter_init()) {
            /* 1 Hz calendar clock: (PREDIV_A + 1) * (PREDIV_S + 1) = f_rtcclk */
            RTC->PRER = s_lse_ok ? 255U : 249U;             /* PREDIV_S first */
            RTC->PRER |= 127U << RTC_PRER_PREDIV_A_Pos;
            RTC->CR &= ~RTC_CR_FMT;                         /* 24 h       */
            RTC->ISR &= ~RTC_ISR_INIT;
        }
        rtc_lock();
        rtc_set(&def);
        RTC->BKP0R = RTC_MAGIC;
    }

    /* Wait for the shadow registers to be synchronised */
    rtc_unlock();
    RTC->ISR &= ~RTC_ISR_RSF;
    rtc_lock();
    start = sys_ms();
    while (!(RTC->ISR & RTC_ISR_RSF)) {
        if ((sys_ms() - start) > 100U) break;
    }
}

bool rtc_lse_ok(void)
{
    return s_lse_ok;
}

void rtc_get(rtc_time_t *t)
{
    uint32_t tr = RTC->TR;          /* reading TR locks DR until it is read */
    uint32_t dr = RTC->DR;

    t->hour  = bcd2bin((tr >> RTC_TR_HU_Pos) & 0x3FU);
    t->min   = bcd2bin((tr >> RTC_TR_MNU_Pos) & 0x7FU);
    t->sec   = bcd2bin(tr & 0x7FU);
    t->year  = bcd2bin((dr >> RTC_DR_YU_Pos) & 0xFFU);
    t->month = bcd2bin((dr >> RTC_DR_MU_Pos) & 0x1FU);
    t->day   = bcd2bin(dr & 0x3FU);
    t->wday  = (uint8_t)((dr >> RTC_DR_WDU_Pos) & 7U);
}

void rtc_set(const rtc_time_t *t)
{
    uint8_t wday = day_of_week((uint16_t)(2000U + t->year), t->month, t->day);
    uint32_t tr = (bin2bcd(t->hour) << RTC_TR_HU_Pos)
                | (bin2bcd(t->min)  << RTC_TR_MNU_Pos)
                | (bin2bcd(t->sec)  << RTC_TR_SU_Pos);
    uint32_t dr = (bin2bcd(t->year) << RTC_DR_YU_Pos)
                | ((uint32_t)wday   << RTC_DR_WDU_Pos)
                | (bin2bcd(t->month) << RTC_DR_MU_Pos)
                | (bin2bcd(t->day)  << RTC_DR_DU_Pos);

    PWR->CR |= PWR_CR_DBP;
    rtc_unlock();
    if (rtc_enter_init()) {
        RTC->TR = tr;
        RTC->DR = dr;
        RTC->ISR &= ~RTC_ISR_INIT;
    }
    RTC->ISR &= ~RTC_ISR_RSF;   /* next read waits for fresh shadow values */
    rtc_lock();
}
