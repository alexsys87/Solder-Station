/**
 * @file    heater.c
 * @brief   TIM1 PWM + ADC1 + DMA2 Stream0 setup.
 */
#include "heater.h"
#include "board.h"
#include "sys.h"

#define TIM1_TICK_HZ        10000U      /* 0.1 ms resolution            */
#define ADC_WINDOW_TICKS    10U         /* CCR2 is 1 ms before period end */

#define ADC_DMA             DMA2_Stream0
#define ADC_DMA_CH          0U

static volatile uint16_t s_adc_buf[ADC_SEQ_LEN];
static volatile uint16_t s_max_ticks;
static volatile uint16_t s_period_ms;
static volatile bool     s_forced_off;
static volatile float    s_duty;

/* ------------------------------------------------------------------------- */
static void adc_init(void)
{
    uint32_t i;
    uint32_t sq[16];

    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;

    gpio_mode(TIP_ADC_PORT, TIP_ADC_PIN, GPIO_MODE_AN);
    gpio_mode(TIP_ADC_PORT, NTC_ADC_PIN, GPIO_MODE_AN);
    gpio_mode(TIP_ADC_PORT, VIN_ADC_PIN, GPIO_MODE_AN);

    /* ADCCLK = 84 MHz / 4 = 21 MHz, enable temperature sensor + VREFINT */
    ADC->CCR = ADC_CCR_ADCPRE_0 | ADC_CCR_TSVREFE;

    /* Sampling times: tip 84 cycles, NTC/VIN 144, internal channels 480 */
    ADC1->SMPR2 = (4U << (TIP_ADC_CH * 3U)) | (5U << (NTC_ADC_CH * 3U)) | (5U << (VIN_ADC_CH * 3U));
    ADC1->SMPR1 = (7U << ((TSENS_ADC_CH - 10U) * 3U)) | (7U << ((VREF_ADC_CH - 10U) * 3U));

    /* Regular sequence */
    for (i = 0; i < TC_SAMPLES; i++) sq[i] = TIP_ADC_CH;
    sq[ADC_IDX_VIN]   = VIN_ADC_CH;
    sq[ADC_IDX_NTC]   = NTC_ADC_CH;
    sq[ADC_IDX_TSENS] = TSENS_ADC_CH;
    sq[ADC_IDX_VREF]  = VREF_ADC_CH;
    ADC1->SQR3 = 0U; ADC1->SQR2 = 0U; ADC1->SQR1 = (uint32_t)(ADC_SEQ_LEN - 1U) << ADC_SQR1_L_Pos;
    for (i = 0; i < ADC_SEQ_LEN; i++) {
        if (i < 6U)       ADC1->SQR3 |= sq[i] << (5U * i);
        else if (i < 12U) ADC1->SQR2 |= sq[i] << (5U * (i - 6U));
        else              ADC1->SQR1 |= sq[i] << (5U * (i - 12U));
    }

    /* DMA2 Stream0 Channel0: ADC1_DR -> buffer, 16 bit, circular */
    ADC_DMA->CR = 0U;
    while (ADC_DMA->CR & DMA_SxCR_EN) { }
    DMA2->LIFCR   = DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTEIF0
                  | DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CFEIF0;
    ADC_DMA->PAR  = (uint32_t)&ADC1->DR;
    ADC_DMA->M0AR = (uint32_t)s_adc_buf;
    ADC_DMA->NDTR = ADC_SEQ_LEN;
    ADC_DMA->FCR  = 0U;
    ADC_DMA->CR   = (ADC_DMA_CH << DMA_SxCR_CHSEL_Pos)
                  | DMA_SxCR_PL_1                       /* high priority */
                  | DMA_SxCR_MSIZE_0 | DMA_SxCR_PSIZE_0 /* 16 bit        */
                  | DMA_SxCR_MINC | DMA_SxCR_CIRC | DMA_SxCR_TCIE;
    ADC_DMA->CR  |= DMA_SxCR_EN;
    NVIC_SetPriority(DMA2_Stream0_IRQn, IRQ_PRIO_ADC_DMA);
    NVIC_EnableIRQ(DMA2_Stream0_IRQn);

    /* Scan mode, triggered on the rising edge of TIM1_CC2, DMA requests
     * continue after the last transfer (circular buffer). */
    ADC1->CR1 = ADC_CR1_SCAN | ADC_CR1_OVRIE;
    NVIC_SetPriority(ADC_IRQn, IRQ_PRIO_ADC_DMA);
    NVIC_EnableIRQ(ADC_IRQn);
    ADC1->CR2 = ADC_CR2_DMA | ADC_CR2_DDS
              | ADC_CR2_EXTEN_0                       /* rising edge   */
              | (1U << ADC_CR2_EXTSEL_Pos)            /* TIM1_CC2      */
              | ADC_CR2_ADON;
}

/* ------------------------------------------------------------------------- */
static void tim1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    (void)RCC->APB2ENR;

    gpio_speed(HEATER_PORT, HEATER_PIN, GPIO_SPEED_LOW);
    gpio_af(HEATER_PORT, HEATER_PIN, 1U);

    TIM1->CR1  = TIM_CR1_ARPE;
    TIM1->PSC  = (uint16_t)(SYSCLK_HZ / TIM1_TICK_HZ - 1U);
    TIM1->CCR1 = 0U;

    /* CH1: PWM mode 1 (heater on while CNT < CCR1)
     * CH2: PWM mode 2, its rising edge at CCR2 triggers the ADC */
    TIM1->CCMR1 = (6U << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE
                | (7U << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
#if HEATER_ACTIVE_HIGH
    TIM1->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;
    TIM1->CR2  = 0U;                        /* idle level low = heater off */
#else
    TIM1->CCER = TIM_CCER_CC1E | TIM_CCER_CC1P | TIM_CCER_CC2E;
    TIM1->CR2  = TIM_CR2_OIS1;              /* idle level high = heater off */
#endif
    /* CH2 is not routed to any pin (PA9 is used by USART1). */
    TIM1->BDTR = TIM_BDTR_MOE | TIM_BDTR_OSSI;
}

void heater_set_timing(uint16_t period_ms, uint8_t delay_01ms)
{
    uint32_t arr;
    uint32_t ccr2;
    uint32_t st;

    if (period_ms < 20U)   period_ms = 20U;
    if (period_ms > 1000U) period_ms = 1000U;

    arr  = (uint32_t)period_ms * (TIM1_TICK_HZ / 1000U);
    ccr2 = arr - ADC_WINDOW_TICKS;

    st = sys_irq_save();
    s_period_ms = period_ms;
    s_max_ticks = (delay_01ms + 10U < ccr2) ? (uint16_t)(ccr2 - delay_01ms) : 0U;
    TIM1->ARR  = arr - 1U;
    TIM1->CCR2 = ccr2;
    if (TIM1->CCR1 > s_max_ticks) TIM1->CCR1 = s_max_ticks;
    sys_irq_restore(st);
}

void heater_init(uint16_t period_ms, uint8_t delay_01ms)
{
    adc_init();
    tim1_init();
    heater_set_timing(period_ms, delay_01ms);
    TIM1->EGR  = TIM_EGR_UG;                /* load preload registers  */
    TIM1->CR1 |= TIM_CR1_CEN;
}

void heater_set_duty(float duty)
{
    uint32_t ticks;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    if (s_forced_off) duty = 0.0f;
    s_duty = duty;
    ticks = (uint32_t)(duty * (float)s_max_ticks + 0.5f);
    TIM1->CCR1 = ticks;
}

float heater_get_duty(void)
{
    return s_duty;
}

uint16_t heater_period_ms(void)
{
    return s_period_ms;
}

void heater_force_off(bool off)
{
    s_forced_off = off;
    if (off) {
        /* "Force inactive" takes effect immediately, no preload */
        TIM1->CCR1  = 0U;
        TIM1->CCMR1 = (TIM1->CCMR1 & ~TIM_CCMR1_OC1M) | (4U << TIM_CCMR1_OC1M_Pos);
        s_duty = 0.0f;
    } else {
        TIM1->CCMR1 = (TIM1->CCMR1 & ~TIM_CCMR1_OC1M) | (6U << TIM_CCMR1_OC1M_Pos);
    }
}

void DMA2_Stream0_IRQHandler(void)
{
    uint32_t isr = DMA2->LISR;
    DMA2->LIFCR = DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTEIF0
                | DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CFEIF0;
    if (isr & DMA_LISR_TCIF0) {
        iron_adc_isr(s_adc_buf);
    }
}

/* Overrun recovery: restart the DMA so the sequence stays aligned */
void ADC_IRQHandler(void)
{
    if (ADC1->SR & ADC_SR_OVR) {
        ADC1->CR2 &= ~ADC_CR2_DMA;
        ADC_DMA->CR &= ~DMA_SxCR_EN;
        while (ADC_DMA->CR & DMA_SxCR_EN) { }
        DMA2->LIFCR = DMA_LIFCR_CTCIF0 | DMA_LIFCR_CHTIF0 | DMA_LIFCR_CTEIF0
                    | DMA_LIFCR_CDMEIF0 | DMA_LIFCR_CFEIF0;
        ADC_DMA->NDTR = ADC_SEQ_LEN;
        ADC_DMA->CR |= DMA_SxCR_EN;
        ADC1->SR = ~ADC_SR_OVR;
        ADC1->CR2 |= ADC_CR2_DMA;
    }
}
