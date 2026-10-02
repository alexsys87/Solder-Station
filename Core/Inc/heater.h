/**
 * @file    heater.h
 * @brief   Heater PWM (TIM1_CH1) synchronised with thermocouple sampling
 *          (ADC1 triggered by TIM1_CC2, results moved by DMA2 Stream0).
 *
 *  One PWM period (e.g. 100 ms):
 *
 *   CNT: 0                    CCR1          CCR2      ARR
 *        |====== heater ON =====|___ OFF ____|^ ADC ___|
 *                               |<- delay ->|
 *
 *  The heater is always switched off at least "ADC delay" before CCR2, so
 *  the thermocouple (in series with the heater) is measured with no current.
 *  The DMA transfer-complete interrupt runs the control loop and writes the
 *  next duty cycle into the preloaded CCR1 register.
 */
#ifndef HEATER_H
#define HEATER_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/* ADC scan sequence layout in the DMA buffer */
#define ADC_IDX_TIP     0                       /* TC_SAMPLES entries     */
#define ADC_IDX_VIN     (TC_SAMPLES + 0)
#define ADC_IDX_NTC     (TC_SAMPLES + 1)
#define ADC_IDX_TSENS   (TC_SAMPLES + 2)
#define ADC_IDX_VREF    (TC_SAMPLES + 3)
#define ADC_SEQ_LEN     (TC_SAMPLES + 4)

#if (ADC_SEQ_LEN > 16)
#error "ADC regular sequence is limited to 16 conversions"
#endif

void     heater_init(uint16_t period_ms, uint8_t delay_01ms);
void     heater_set_timing(uint16_t period_ms, uint8_t delay_01ms);
void     heater_set_duty(float duty);         /* 0..1, applied next period */
float    heater_get_duty(void);
uint16_t heater_period_ms(void);

/* Immediate hard switch-off (used around flash erase, errors) */
void     heater_force_off(bool off);

/* Implemented by the control loop (iron.c), called from DMA interrupt */
void     iron_adc_isr(const volatile uint16_t *buf);

#endif /* HEATER_H */
