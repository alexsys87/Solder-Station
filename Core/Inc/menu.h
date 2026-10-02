/**
 * @file    menu.h
 * @brief   Table driven settings menu.
 */
#ifndef MENU_H
#define MENU_H

#include <stdint.h>
#include <stdbool.h>
#include "input.h"

void menu_open(void);
/* returns false when the user left the menu */
bool menu_input(const input_rot_t *rot, btn_event_t ev);
void menu_draw(void);

/* Implemented in ui.c, used by menu actions */
void ui_apply_settings(void);
void ui_start_calibration(void);
void ui_show_info(void);
void ui_confirm(const char *question, void (*on_yes)(void));
void ui_message(const char *line1, const char *line2);

#endif /* MENU_H */
