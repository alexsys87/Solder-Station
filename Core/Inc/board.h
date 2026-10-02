/**
 * @file    board.h
 * @brief   WeAct BlackPill STM32F401 pin map and small GPIO helpers.
 *
 *  Function            Pin    Peripheral
 *  ------------------  -----  -------------------------------
 *  Heater PWM          PA8    TIM1_CH1 (AF1)
 *  Tip thermocouple    PA1    ADC1_IN1
 *  Handle NTC (opt.)   PA2    ADC1_IN2
 *  VIN divider         PA3    ADC1_IN3
 *  OLED CS             PA4    GPIO
 *  OLED SCK            PA5    SPI1_SCK  (AF5)
 *  OLED SDA (MOSI)     PA7    SPI1_MOSI (AF5)
 *  OLED DC             PB0    GPIO
 *  OLED RES            PB1    GPIO
 *  Encoder CLK (A)     PB6    TIM4_CH1 (AF2)
 *  Encoder DT  (B)     PB7    TIM4_CH2 (AF2)
 *  Encoder SW          PB8    GPIO input, pull-up
 *  Vibration sensor    PB9    EXTI9, pull-up
 *  Buzzer              PB4    TIM3_CH1 (AF2)
 *  UART TX / RX        PA9/10 USART1 (AF7), text protocol
 *  USB D- / D+         PA11/12 OTG FS (AF10), CDC virtual COM port
 *  On-board LED        PC13   GPIO, active low
 *  32.768 kHz crystal  PC14/PC15 (LSE for RTC)
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32f4xx.h"
#include <stdint.h>
#include <stdbool.h>

/* GPIO modes */
#define GPIO_MODE_IN            0U
#define GPIO_MODE_OUT           1U
#define GPIO_MODE_AF            2U
#define GPIO_MODE_AN            3U

#define GPIO_PULL_NONE          0U
#define GPIO_PULL_UP            1U
#define GPIO_PULL_DOWN          2U

#define GPIO_SPEED_LOW          0U
#define GPIO_SPEED_MED          1U
#define GPIO_SPEED_FAST         2U
#define GPIO_SPEED_HIGH         3U

/* Pins */
#define HEATER_PORT             GPIOA
#define HEATER_PIN              8U

#define TIP_ADC_PORT            GPIOA
#define TIP_ADC_PIN             1U
#define TIP_ADC_CH              1U
#define NTC_ADC_PIN             2U
#define NTC_ADC_CH              2U
#define VIN_ADC_PIN             3U
#define VIN_ADC_CH              3U
#define TSENS_ADC_CH            16U
#define VREF_ADC_CH             17U

#define OLED_CS_PORT            GPIOA
#define OLED_CS_PIN             4U
#define OLED_SCK_PORT           GPIOA
#define OLED_SCK_PIN            5U
#define OLED_MOSI_PORT          GPIOA
#define OLED_MOSI_PIN           7U
#define OLED_DC_PORT            GPIOB
#define OLED_DC_PIN             0U
#define OLED_RES_PORT           GPIOB
#define OLED_RES_PIN            1U

#define ENC_PORT                GPIOB
#define ENC_A_PIN               6U
#define ENC_B_PIN               7U
#define ENC_SW_PORT             GPIOB
#define ENC_SW_PIN              8U

#define VIBRO_PORT              GPIOB
#define VIBRO_PIN               9U

#define BUZZER_PORT             GPIOB
#define BUZZER_PIN              4U

#define UART_PORT               GPIOA
#define UART_TX_PIN             9U
#define UART_RX_PIN             10U

#define LED_PORT                GPIOC
#define LED_PIN                 13U

/* ------------------------------------------------------------------------- */
/* GPIO helpers                                                              */
/* ------------------------------------------------------------------------- */
static inline void gpio_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode)
{
    port->MODER = (port->MODER & ~(3U << (pin * 2U))) | (mode << (pin * 2U));
}

static inline void gpio_pull(GPIO_TypeDef *port, uint32_t pin, uint32_t pull)
{
    port->PUPDR = (port->PUPDR & ~(3U << (pin * 2U))) | (pull << (pin * 2U));
}

static inline void gpio_speed(GPIO_TypeDef *port, uint32_t pin, uint32_t speed)
{
    port->OSPEEDR = (port->OSPEEDR & ~(3U << (pin * 2U))) | (speed << (pin * 2U));
}

static inline void gpio_af(GPIO_TypeDef *port, uint32_t pin, uint32_t af)
{
    uint32_t idx = pin >> 3U;
    uint32_t sh  = (pin & 7U) * 4U;
    port->AFR[idx] = (port->AFR[idx] & ~(0xFU << sh)) | (af << sh);
    gpio_mode(port, pin, GPIO_MODE_AF);
}

static inline void gpio_set(GPIO_TypeDef *port, uint32_t pin)
{
    port->BSRR = 1UL << pin;
}

static inline void gpio_clr(GPIO_TypeDef *port, uint32_t pin)
{
    port->BSRR = 1UL << (pin + 16U);
}

static inline bool gpio_read(GPIO_TypeDef *port, uint32_t pin)
{
    return (port->IDR & (1UL << pin)) != 0U;
}

static inline void led_set(bool on)
{
    if (on) gpio_clr(LED_PORT, LED_PIN); else gpio_set(LED_PORT, LED_PIN);
}

/* Interrupt priorities (lower number = higher priority) */
#define IRQ_PRIO_ADC_DMA        1U   /* control loop                         */
#define IRQ_PRIO_SYSTICK        2U   /* 1 ms tick, input sampling            */
#define IRQ_PRIO_OLED_DMA       3U
#define IRQ_PRIO_EXTI           3U
#define IRQ_PRIO_USB            4U
#define IRQ_PRIO_UART           4U

#endif /* BOARD_H */
