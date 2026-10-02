/**
 * @file    ui.h
 * @brief   User interface: screens, encoder handling, rendering.
 */
#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <stdbool.h>

void ui_init(void);
void ui_task(void);

/* Remote control of the calibration wizard (used by the PC protocol) */
typedef struct {
    bool     active;        /* calibration screen is open           */
    uint8_t  step;          /* current point 0..2, 3 = finished     */
    uint16_t target;        /* target temperature of the point      */
    bool     stable;        /* temperature stable for 5 s           */
    bool     done;
    bool     ok;            /* finished and saved                   */
} ui_cal_state_t;

void ui_start_calibration(void);
bool ui_cal_active(void);
bool ui_cal_point(int measured);
void ui_cal_abort(void);
void ui_cal_state(ui_cal_state_t *cs);

/* Re-apply settings to the hardware (display, buzzer, iron) */
void ui_apply_settings(void);
/* Force the main screen to show the setpoint for a moment */
void ui_show_setpoint(void);

#endif /* UI_H */
