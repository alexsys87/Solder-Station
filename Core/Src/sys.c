/**
 * @file    sys.c
 * @brief   Clock tree (HSE 25 MHz -> PLL 84 MHz), SysTick, IWDG, CRC unit.
 */
#include "sys.h"
#include "board.h"
#include "config.h"
#include "input.h"
#include "buzzer.h"

static volatile uint32_t s_ms;
static bool s_hse_ok;
static bool s_wdg_reset;

/* ------------------------------------------------------------------------- */
/* Clock                                                                     */
/* ------------------------------------------------------------------------- */
/*
 * SYSCLK = 84 MHz, AHB = 84 MHz, APB1 = 42 MHz (timers 84 MHz),
 * APB2 = 84 MHz (timers 84 MHz), PLLQ = 48 MHz (USB, not used).
 * Falls back to HSI if the crystal does not start.
 */
void sys_clock_init(void)
{
    uint32_t timeout;
    uint32_t pllm;
    uint32_t src;

    /* Remember the reset reason before clearing the flags */
    s_wdg_reset = (RCC->CSR & RCC_CSR_IWDGRSTF) != 0U;
    RCC->CSR |= RCC_CSR_RMVF;

    RCC->CR |= RCC_CR_HSEON;
    for (timeout = 0; timeout < 500000U; timeout++) {
        if (RCC->CR & RCC_CR_HSERDY) break;
    }
    s_hse_ok = (RCC->CR & RCC_CR_HSERDY) != 0U;

    if (s_hse_ok) {
        pllm = BOARD_HSE_HZ / 1000000UL;   /* 1 MHz PLL input */
        src  = RCC_PLLCFGR_PLLSRC_HSE;
    } else {
        RCC->CR &= ~RCC_CR_HSEON;
        pllm = 16U;                        /* HSI 16 MHz */
        src  = 0U;
    }

    /* Voltage scale 2 is enough for 84 MHz */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    PWR->CR = (PWR->CR & ~PWR_CR_VOS) | PWR_CR_VOS_1;

    /* 2 wait states @ 84 MHz / 3.3 V, enable prefetch and caches */
    FLASH->ACR = FLASH_ACR_LATENCY_2WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    /* PLL: VCO = 1 MHz * 336 = 336 MHz, SYSCLK = VCO / 4, USB = VCO / 7 */
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY) { }
    RCC->PLLCFGR = (pllm << RCC_PLLCFGR_PLLM_Pos)
                 | (336U << RCC_PLLCFGR_PLLN_Pos)
                 | (1U   << RCC_PLLCFGR_PLLP_Pos)     /* P = 4 */
                 | (7U   << RCC_PLLCFGR_PLLQ_Pos)
                 | src;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }

    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2))
              | RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;

    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }

    SystemCoreClock = SYSCLK_HZ;

    /* Peripheral clocks used by the firmware */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN
                  | RCC_AHB1ENR_DMA2EN  | RCC_AHB1ENR_CRCEN;
    (void)RCC->AHB1ENR;

    /* Stop the heater timer and the watchdog while the core is halted by a
     * debugger: TIM1 outputs go to their (off) idle state. */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;
    DBGMCU->APB2FZ |= DBGMCU_APB2_FZ_DBG_TIM1_STOP;
}

bool sys_hse_ok(void)
{
    return s_hse_ok;
}

void sys_gpio_init(void)
{
    /* On-board LED, off */
    gpio_set(LED_PORT, LED_PIN);
    gpio_mode(LED_PORT, LED_PIN, GPIO_MODE_OUT);
}

/* ------------------------------------------------------------------------- */
/* 1 ms tick                                                                 */
/* ------------------------------------------------------------------------- */
void sys_tick_init(void)
{
    SysTick_Config(SystemCoreClock / 1000U);
    NVIC_SetPriority(SysTick_IRQn, IRQ_PRIO_SYSTICK);
}

uint32_t sys_ms(void)
{
    return s_ms;
}

void sys_delay_ms(uint32_t ms)
{
    uint32_t start = s_ms;
    while ((s_ms - start) < ms) {
        __WFI();
    }
}

void SysTick_Handler(void)
{
    s_ms++;
    input_tick_1ms();
#if USE_BUZZER
    buzzer_tick_1ms();
#endif
}

/* ------------------------------------------------------------------------- */
/* Independent watchdog                                                      */
/* ------------------------------------------------------------------------- */
void sys_wdg_init(void)
{
#if USE_WATCHDOG
    IWDG->KR  = 0xCCCCU;            /* start (also enables LSI)   */
    IWDG->KR  = 0x5555U;            /* unlock PR/RLR              */
    IWDG->PR  = 4U;                 /* LSI / 64 -> ~500 Hz        */
    IWDG->RLR = 1000U;              /* ~2 s                        */
    while (IWDG->SR != 0U) { }
    IWDG->KR  = 0xAAAAU;
#endif
}

void sys_wdg_feed(void)
{
#if USE_WATCHDOG
    IWDG->KR = 0xAAAAU;
#endif
}

bool sys_reset_by_wdg(void)
{
    return s_wdg_reset;
}

/* ------------------------------------------------------------------------- */
/* Hardware CRC-32 (poly 0x04C11DB7, init 0xFFFFFFFF, 32-bit words)           */
/* ------------------------------------------------------------------------- */
uint32_t sys_crc32(const void *data, uint32_t len_words)
{
    const uint32_t *p = (const uint32_t *)data;
    uint32_t state = sys_irq_save();   /* CRC unit is shared */
    uint32_t crc;

    CRC->CR = CRC_CR_RESET;
    while (len_words--) {
        CRC->DR = *p++;
    }
    crc = CRC->DR;
    sys_irq_restore(state);
    return crc;
}

uint32_t sys_irq_save(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

void sys_irq_restore(uint32_t state)
{
    __set_PRIMASK(state);
}
