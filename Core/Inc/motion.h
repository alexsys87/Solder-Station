/**
 * @file    motion.h
 * @brief   Vibration / tilt sensor in the handle (SW-18010P, SW-200D ...).
 */
#ifndef MOTION_H
#define MOTION_H

#include <stdint.h>
#include <stdbool.h>

void     motion_init(void);
void     motion_enable(bool en);
uint32_t motion_last_ms(void);      /* timestamp of the last movement       */
uint32_t motion_count(void);        /* number of detected movements         */

#endif /* MOTION_H */
