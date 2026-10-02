/**
 * @file    buzzer.c
 * @brief   Non-blocking beeper: TIM3 generates the tone, SysTick times it.
 */
#include "buzzer.h"
#include "board.h"
#include "config.h"
#include "sys.h"

#if USE_BUZZER

static bool              s_enabled = true;
static volatile uint8_t  s_count;
static volatile uint16_t s_on_ms;
static volatile uint16_t s_off_ms;
static volatile uint16_t s_timer;
static volatile bool     s_sounding;

static void tone(bool on)
{
    if (on) {
        TIM3->CCER |= TIM_CCER_CC1E;
        TIM3->CR1  |= TIM_CR1_CEN;
    } else {
        TIM3->CR1  &= ~TIM_CR1_CEN;
        TIM3->CCER &= ~TIM_CCER_CC1E;
        TIM3->CNT   = 0U;
    }
    s_sounding = on;
}

void buzzer_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    (void)RCC->APB1ENR;

    /* Output low when idle */
    gpio_clr(BUZZER_PORT, BUZZER_PIN);
    gpio_pull(BUZZER_PORT, BUZZER_PIN, GPIO_PULL_DOWN);
    gpio_af(BUZZER_PORT, BUZZER_PIN, 2U);

    TIM3->PSC   = (SYSCLK_HZ / 1000000UL) - 1U;        /* 1 MHz tick      */
    TIM3->ARR   = (1000000UL / BUZZER_FREQ_HZ) - 1U;
    TIM3->CCR1  = (1000000UL / BUZZER_FREQ_HZ) / 2U;   /* 50 % duty       */
    TIM3->CCMR1 = (6U << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    TIM3->CR1   = TIM_CR1_ARPE;
    TIM3->EGR   = TIM_EGR_UG;
    tone(false);
}

void buzzer_enable(bool en)
{
    s_enabled = en;
    if (!en) {
        s_count = 0;
        tone(false);
    }
}

void buzzer_pattern(uint8_t count, uint16_t on_ms, uint16_t off_ms)
{
    uint32_t st;
    if (!s_enabled || count == 0U) return;
    st = sys_irq_save();
    s_count  = count;
    s_on_ms  = on_ms;
    s_off_ms = off_ms;
    s_timer  = on_ms;
    tone(true);
    sys_irq_restore(st);
}

void buzzer_tick_1ms(void)
{
    if (s_count == 0U) return;
    if (s_timer > 0U) {
        s_timer--;
        return;
    }
    if (s_sounding) {
        tone(false);
        if (--s_count == 0U) return;
        s_timer = s_off_ms;
    } else {
        tone(true);
        s_timer = s_on_ms;
    }
}

#else

void buzzer_init(void) { }
void buzzer_enable(bool en) { (void)en; }
void buzzer_tick_1ms(void) { }
void buzzer_pattern(uint8_t count, uint16_t on_ms, uint16_t off_ms)
{
    (void)count; (void)on_ms; (void)off_ms;
}

#endif /* USE_BUZZER */
