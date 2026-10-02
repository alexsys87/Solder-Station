/**
 * @file    main.c
 * @brief   T12 soldering station on WeAct BlackPill STM32F401.
 *
 * Bare metal, CMSIS + direct register access, no HAL.
 *
 * Hardware usage:
 *   TIM1  CH1 heater PWM, CH2 triggers ADC1 while the heater is off
 *   ADC1  scan of 16 channels -> DMA2 Stream0 (circular), control loop in
 *         the DMA interrupt
 *   SPI1  + DMA2 Stream3: OLED frame transfer
 *   TIM4  encoder interface (hardware quadrature decoder, input filters)
 *   TIM3  CH1 buzzer tone
 *   USART1 + DMA2 Stream7: telemetry
 *   RTC   (LSE) calendar for the clock
 *   EXTI9 vibration sensor
 *   CRC   settings checksum, FLASH sectors 1-2 settings storage
 *   IWDG  watchdog, fed only while the control loop is alive
 */
#include "board.h"
#include "config.h"
#include "sys.h"
#include "input.h"
#include "buzzer.h"
#include "oled.h"
#include "rtc.h"
#include "motion.h"
#include "settings.h"
#include "iron.h"
#include "telemetry.h"
#include "ui.h"

int main(void)
{
    sys_clock_init();
    sys_gpio_init();
    sys_tick_init();

    settings_init();

    input_init();
    buzzer_init();
    motion_init();
    oled_init();
    rtc_init();
    telemetry_init();
    iron_init();
    ui_init();

    sys_wdg_init();
    buzzer_short();

    for (;;) {
        /* Feed the watchdog only while the heater control loop is running:
         * if the ADC/DMA chain stops, the MCU is reset (heater pin then
         * floats and is pulled to the off level by the external resistor). */
        if (iron_alive()) {
            sys_wdg_feed();
        }

        iron_task();
        ui_task();
        settings_task();
        telemetry_task();

        __WFI();    /* wake up on the next interrupt (SysTick at least) */
    }
}
