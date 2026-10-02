/**
 * @file    config.h
 * @brief   Compile-time configuration of the T12 soldering station.
 *
 * Everything that depends on the actual hardware build (display type,
 * amplifier gain, voltage divider, optional parts) is selected here.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* ------------------------------------------------------------------------- */
/* Firmware version                                                          */
/* ------------------------------------------------------------------------- */
#define FW_VERSION_STR          "1.2.0"

/* ------------------------------------------------------------------------- */
/* Display controller                                                        */
/* ------------------------------------------------------------------------- */
#define OLED_SH1106             1   /* 1.3" modules (132x64 RAM, column offset 2) */
#define OLED_SSD1306            2   /* 0.96" modules (128x64 RAM)                 */

/* Select the display controller here */
#define OLED_CONTROLLER         OLED_SH1106

/* SPI1 clock = 84 MHz / prescaler. SH1106 max SCK is ~4 MHz by datasheet,
 * SSD1306 allows 10 MHz. Values: 0=/2 1=/4 2=/8 3=/16 4=/32 5=/64 6=/128 7=/256 */
#if (OLED_CONTROLLER == OLED_SH1106)
  #define OLED_SPI_BR           4   /* 2.625 MHz */
#else
  #define OLED_SPI_BR           3   /* 5.25 MHz  */
#endif

/* ------------------------------------------------------------------------- */
/* Clock                                                                     */
/* ------------------------------------------------------------------------- */
/* WeAct BlackPill STM32F401 v3.x uses a 25 MHz crystal */
#define BOARD_HSE_HZ            25000000UL
#define SYSCLK_HZ               84000000UL

/* ------------------------------------------------------------------------- */
/* Heater / thermocouple front-end                                           */
/* ------------------------------------------------------------------------- */
/* 1 - heater is ON when PA8 is high (NPN + P-MOSFET driver, see README)     */
/* 0 - heater is ON when PA8 is low                                          */
#define HEATER_ACTIVE_HIGH      1

/* Non-inverting thermocouple amplifier gain: 1 + Rf/Rg (100k / 470R) */
#define TC_AMP_GAIN             213.8f

/* Approximate T12 thermocouple sensitivity, uV per degree C.
 * Only used for the default (uncalibrated) tip table. */
#define TC_UV_PER_DEG           21.0f

/* ADC reading above this value means the thermocouple circuit is open
 * (no tip / broken heater). The amplifier input has a pull-up for that. */
#define TC_ADC_NO_TIP           4000

/* Number of thermocouple samples per control cycle (oversampling) */
#define TC_SAMPLES              12

/* ------------------------------------------------------------------------- */
/* Supply voltage measurement                                                */
/* ------------------------------------------------------------------------- */
/* VIN divider ratio: (Rtop + Rbottom) / Rbottom, e.g. 100k / 10k -> 11 */
#define VIN_DIVIDER             11.0f

/* ------------------------------------------------------------------------- */
/* Cold junction compensation source                                        */
/* ------------------------------------------------------------------------- */
#define CJ_SRC_NONE             0   /* fixed 25 C                              */
#define CJ_SRC_CHIP             1   /* STM32 internal temperature sensor       */
#define CJ_SRC_NTC              2   /* NTC in the handle connected to PA2      */

#define CJ_SOURCE               CJ_SRC_CHIP

/* NTC parameters (only for CJ_SRC_NTC): NTC to GND, pull-up to 3.3V */
#define NTC_PULLUP_OHM          10000.0f
#define NTC_R25_OHM             10000.0f
#define NTC_BETA                3950.0f

/* The chip is warmer than the ambient air, compensate it (C) */
#define CJ_CHIP_OFFSET          3.0f

/* ------------------------------------------------------------------------- */
/* Encoder                                                                   */
/* ------------------------------------------------------------------------- */
/* TIM4 runs in x4 mode. A typical EC11 with 20 detents / 20 pulses per
 * revolution gives 4 counts per detent. Use 2 for "half step" encoders. */
#define ENC_COUNTS_PER_DETENT   4

#define BTN_DEBOUNCE_MS         20
#define BTN_LONG_MS             800
#define BTN_DOUBLE_MS           300

/* ------------------------------------------------------------------------- */
/* Optional features                                                         */
/* ------------------------------------------------------------------------- */
#define USE_BUZZER              1   /* passive buzzer on PB4 (TIM3_CH1)      */
#define BUZZER_FREQ_HZ          2700

#define USE_UART                1   /* USART1 PA9/PA10, same protocol as USB  */
#define UART_BAUD               115200
#define UART_STREAM_DEFAULT_MS  0   /* status stream on UART at start, 0=off */

#define USE_USB                 1   /* USB CDC virtual COM port (PA11/PA12)   */

#define USE_WATCHDOG            1   /* IWDG, ~2 s timeout                     */

/* UI language of fresh / factory reset settings: 0 = English, 1 = Russian.
 * It can be changed at any time in the menu (Language / Yazyk). */
#define DEFAULT_LANGUAGE        1

/* ------------------------------------------------------------------------- */
/* Limits and safety                                                         */
/* ------------------------------------------------------------------------- */
#define TEMP_ABS_MIN            100     /* lowest allowed setpoint, C        */
#define TEMP_ABS_MAX            480     /* highest allowed setpoint, C       */
#define TEMP_OVERHEAT           520     /* emergency heater shutdown, C      */
#define TEMP_HOT_WARN           50      /* "HOT" warning when off, C         */

/* Thermal runaway: near-full power for this time without temperature rise */
#define RUNAWAY_TIME_MS         20000
#define RUNAWAY_MIN_RISE        10      /* C */

/* Number of tip profiles stored in flash */
#define TIP_MAX                 10
#define TIP_NAME_LEN            8

#endif /* CONFIG_H */
