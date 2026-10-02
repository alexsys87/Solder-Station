/**
 * @file    buzzer.h
 * @brief   Passive buzzer driven by TIM3_CH1 PWM (PB4).
 */
#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>
#include <stdbool.h>

void buzzer_init(void);
void buzzer_enable(bool en);
void buzzer_tick_1ms(void);

/* count beeps of on_ms with off_ms pauses */
void buzzer_pattern(uint8_t count, uint16_t on_ms, uint16_t off_ms);

#define buzzer_click()      buzzer_pattern(1U, 8U, 0U)
#define buzzer_short()      buzzer_pattern(1U, 60U, 0U)
#define buzzer_ready()      buzzer_pattern(2U, 80U, 80U)
#define buzzer_alarm()      buzzer_pattern(3U, 300U, 200U)

#endif /* BUZZER_H */
