/**
 * @file    motion.c
 * @brief   Vibration sensor on PB9 via EXTI line 9 (both edges).
 *
 * The sensor is a mechanical contact between the pin and GND, any change
 * of its state means the handle was moved.
 */
#include "motion.h"
#include "board.h"
#include "irq.h"
#include "sys.h"

#define MOTION_HOLDOFF_MS   20U

static volatile uint32_t s_last_ms;
static volatile uint32_t s_count;
static bool s_enabled;

void motion_init(void)
{
    gpio_pull(VIBRO_PORT, VIBRO_PIN, GPIO_PULL_UP);
    gpio_mode(VIBRO_PORT, VIBRO_PIN, GPIO_MODE_IN);

    /* EXTI9 <- PB9 */
    SYSCFG->EXTICR[VIBRO_PIN >> 2] = (SYSCFG->EXTICR[VIBRO_PIN >> 2] & ~(0xFU << ((VIBRO_PIN & 3U) * 4U)))
                                   | (1U << ((VIBRO_PIN & 3U) * 4U));
    EXTI->RTSR |= 1UL << VIBRO_PIN;
    EXTI->FTSR |= 1UL << VIBRO_PIN;
    EXTI->PR    = 1UL << VIBRO_PIN;

    NVIC_SetPriority(EXTI9_5_IRQn, IRQ_PRIO_EXTI);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void motion_enable(bool en)
{
    s_enabled = en;
    if (en) {
        EXTI->PR   = 1UL << VIBRO_PIN;
        EXTI->IMR |= 1UL << VIBRO_PIN;
    } else {
        EXTI->IMR &= ~(1UL << VIBRO_PIN);
    }
}

uint32_t motion_last_ms(void)
{
    return s_last_ms;
}

uint32_t motion_count(void)
{
    return s_count;
}

void EXTI9_5_IRQHandler(void)
{
    uint32_t now;
    if (EXTI->PR & (1UL << VIBRO_PIN)) {
        EXTI->PR = 1UL << VIBRO_PIN;
        now = sys_ms();
        if (s_enabled && (now - s_last_ms) >= MOTION_HOLDOFF_MS) {
            s_last_ms = now;
            s_count++;
        }
    }
}
