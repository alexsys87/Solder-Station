/**
 * @file    input.h
 * @brief   Rotary encoder (TIM4 hardware quadrature decoder) and push button.
 */
#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BTN_NONE = 0,
    BTN_CLICK,      /* short press and release                        */
    BTN_DOUBLE,     /* second click within BTN_DOUBLE_MS              */
    BTN_LONG        /* held for BTN_LONG_MS (sent while still held)   */
} btn_event_t;

typedef struct {
    int16_t steps;          /* detents, button released                  */
    int16_t accel;          /* same detents with speed acceleration      */
    int16_t pressed_steps;  /* detents turned while the button was held  */
} input_rot_t;

void        input_init(void);
void        input_tick_1ms(void);          /* called from SysTick       */

void        input_take_rotation(input_rot_t *rot);
btn_event_t input_take_button(void);
bool        input_button_down(void);
void        input_flush(void);

void        input_set_invert(bool invert);
uint32_t    input_last_activity(void);     /* ms timestamp              */

#endif /* INPUT_H */
