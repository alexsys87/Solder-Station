/**
 * @file    settings.h
 * @brief   Persistent settings and tip profiles stored in internal flash.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#define CAL_POINTS          3

/* Calibration reference temperatures, C */
#define CAL_T1              250
#define CAL_T2              350
#define CAL_T3              450

typedef struct {
    char     name[TIP_NAME_LEN + 1];    /* zero terminated                     */
    uint8_t  reserved;
    uint16_t setpoint;                  /* last used temperature, C           */
    uint16_t cal_adc[CAL_POINTS];       /* ADC readings at calibration points */
    int16_t  cal_dt[CAL_POINTS];        /* tip - cold junction at those, C    */
    uint16_t kp;                        /* PID gains x 1000                   */
    uint16_t ki;
    uint16_t kd;
    uint16_t reserved2;
} tip_t;

typedef enum {
    START_OFF = 0,
    START_RUN = 1
} start_mode_t;

typedef struct {
    /* --- header --- */
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t seq;                       /* incremented on every save          */

    /* --- tips --- */
    uint8_t  tip_count;
    uint8_t  tip_active;
    uint8_t  start_mode;                /* start_mode_t                       */
    uint8_t  temp_step;                 /* encoder step for setpoint, C       */

    /* --- temperatures --- */
    uint16_t temp_min;
    uint16_t temp_max;
    uint16_t boost_add;                 /* C added in boost mode              */
    uint16_t boost_time;                /* s                                  */
    uint16_t sleep_temp;                /* C                                  */

    /* --- sleep --- */
    uint8_t  sleep_time;                /* min, 0 = never                     */
    uint8_t  off_time;                  /* min in sleep before off, 0 = never */
    uint8_t  motion_en;                 /* vibration sensor in use            */
    uint8_t  wake_on_enc;               /* encoder wakes from sleep           */

    /* --- clock --- */
    uint8_t  clock_en;
    uint8_t  clock_24h;
    uint8_t  clock_show;                /* s the clock is shown               */
    uint8_t  set_show;                  /* s the setpoint is shown            */

    /* --- display / sound / input --- */
    uint8_t  contrast;                  /* %                                  */
    uint8_t  flip;
    uint8_t  buzzer;
    uint8_t  enc_invert;
    uint8_t  dim_idle;                  /* dim display in sleep / off         */
    uint8_t  adc_delay;                 /* 0.1 ms, heater off -> ADC          */
    uint16_t pwm_period;                /* ms                                 */

    /* --- system --- */
    uint16_t power_limit;               /* W, 0 = no limit                    */
    uint16_t heater_res;                /* 0.1 Ohm                            */
    uint16_t low_volt;                  /* 0.1 V, 0 = disabled                */
    int16_t  adc_offset;                /* thermocouple amplifier offset, LSB */

    tip_t    tips[TIP_MAX];
    uint16_t reserved3;

    uint32_t crc;                       /* hardware CRC-32 of all above       */
} settings_t;

extern settings_t g_set;

void   settings_init(void);                     /* load or defaults           */
void   settings_defaults(void);
void   settings_tip_defaults(tip_t *tip, const char *name);
void   settings_reset_cal(tip_t *tip);
bool   settings_save(void);                     /* immediately (if changed)   */
void   settings_save_later(void);               /* deferred save              */
void   settings_task(void);
bool   settings_loaded(void);

tip_t *settings_tip(void);                      /* active tip profile         */

#endif /* SETTINGS_H */
