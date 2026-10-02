/**
 * @file    iron.h
 * @brief   Soldering iron control: temperature measurement, PID, operating
 *          modes (run / boost / sleep / off), safety checks.
 */
#ifndef IRON_H
#define IRON_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    IRON_OFF = 0,
    IRON_RUN,
    IRON_BOOST,
    IRON_SLEEP,
    IRON_CAL            /* calibration: regulates to an explicit target */
} iron_mode_t;

/* Error flags */
#define IRON_ERR_NO_TIP     0x01U   /* thermocouple open: no tip / handle  */
#define IRON_ERR_OVERHEAT   0x02U   /* tip above TEMP_OVERHEAT             */
#define IRON_ERR_RUNAWAY    0x04U   /* full power without temperature rise */
#define IRON_ERR_LOW_VOLT   0x08U   /* supply below the configured limit   */
#define IRON_ERR_LATCHED    (IRON_ERR_OVERHEAT | IRON_ERR_RUNAWAY)

typedef struct {
    float    tip_c;         /* tip temperature (control filter)          */
    float    tip_disp;      /* tip temperature smoothed for display      */
    float    tip_raw;       /* averaged thermocouple ADC reading         */
    float    cj_c;          /* cold junction temperature                 */
    float    chip_c;        /* MCU temperature                           */
    float    vin;           /* supply voltage                            */
    float    duty;          /* heater duty cycle 0..1                    */
    float    power_w;       /* average heater power                      */
    uint32_t cycles;        /* control loop iterations                   */
} iron_status_t;

void        iron_init(void);
void        iron_apply_settings(void);
void        iron_task(void);

void        iron_set_mode(iron_mode_t mode);
iron_mode_t iron_mode(void);
bool        iron_auto_off(void);
uint16_t    iron_target(void);
uint32_t    iron_boost_left_s(void);

uint8_t     iron_errors(void);
void        iron_clear_errors(void);

const iron_status_t *iron_status(void);
bool        iron_alive(void);
bool        iron_stable(uint32_t ms);
void        iron_set_cal_target(uint16_t t);

#endif /* IRON_H */
