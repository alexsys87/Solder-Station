/**
 * @file    iron.c
 * @brief   Temperature control loop and operating mode state machine.
 *
 * iron_adc_isr() runs once per PWM period from the ADC DMA interrupt, right
 * after the thermocouple was sampled with the heater switched off. It
 * converts the readings, runs the PID and loads the next duty cycle.
 * iron_task() runs in the main loop and handles the slow logic: sleep and
 * auto-off timers, boost timeout, error latching.
 */
#include "iron.h"
#include "heater.h"
#include "settings.h"
#include "input.h"
#include "motion.h"
#include "buzzer.h"
#include "board.h"
#include "sys.h"
#include "config.h"
#include <math.h>

/* Factory calibration values (STM32F401 datasheet) */
#define VREFINT_CAL     (*(const uint16_t *)0x1FFF7A2AUL)  /* 3.3 V, 30 C */
#define TS_CAL1         (*(const uint16_t *)0x1FFF7A2CUL)  /* 30 C        */
#define TS_CAL2         (*(const uint16_t *)0x1FFF7A2EUL)  /* 110 C       */

#define PID_FULL_BAND   30.0f       /* below target - band: full power   */
#define STABLE_BAND     3.0f        /* C                                  */
#define READY_BAND      3.0f        /* C, "ready" beep                    */

/* ---- state shared with the interrupt ---- */
static volatile iron_mode_t s_mode;
static volatile uint16_t    s_target;
static volatile uint8_t     s_errors;
static volatile uint32_t    s_last_cycle_ms;
static iron_status_t        s_st;

/* Calibration table: x = ADC, y = tip - cold junction (C) */
static float s_cal_x[CAL_POINTS + 1];
static float s_cal_y[CAL_POINTS + 1];

/* PID */
static float s_kp, s_ki, s_kd;
static float s_integ;
static float s_prev_temp;
static bool  s_pid_init;
static float s_heater_r;
static float s_power_limit;

/* Filters */
static bool    s_first = true;
static uint8_t s_no_tip_cnt;

/* Thermal runaway detection */
static bool     s_rw_active;
static uint32_t s_rw_start_ms;
static float    s_rw_start_temp;

/* ---- main loop state ---- */
static uint32_t s_input_act;
static uint32_t s_motion_act;
static uint32_t s_idle_since;
static uint32_t s_sleep_since;
static uint32_t s_boost_since;
static bool     s_auto_off;
static bool     s_reached;
static uint32_t s_stable_since;
static uint16_t s_cal_target;
static uint8_t  s_reported_errors;

/* ------------------------------------------------------------------------- */
/* Conversions                                                               */
/* ------------------------------------------------------------------------- */
static float raw_to_dt(float raw)
{
    uint32_t i = 0;
    /* Piecewise linear, the last segment is extrapolated */
    while (i < CAL_POINTS - 1U && raw > s_cal_x[i + 1U]) i++;
    return s_cal_y[i] + (raw - s_cal_x[i]) * (s_cal_y[i + 1U] - s_cal_y[i])
                                           / (s_cal_x[i + 1U] - s_cal_x[i]);
}

static float chip_temp(uint16_t ts_raw, float vdda)
{
    float ts = (float)ts_raw * vdda / 3.3f;     /* as if VDDA were 3.3 V */
    uint16_t c1 = TS_CAL1;
    uint16_t c2 = TS_CAL2;

    if (c1 == 0xFFFFU || c2 <= c1) {
        /* typical values: 0.76 V at 25 C, 2.5 mV/C */
        return (ts * 3.3f / 4095.0f - 0.76f) / 0.0025f + 25.0f;
    }
    return 30.0f + (ts - (float)c1) * 80.0f / (float)(c2 - c1);
}

#if (CJ_SOURCE == CJ_SRC_NTC)
static float ntc_temp(uint16_t raw)
{
    float r;
    if (raw >= 4094U) raw = 4094U;
    if (raw < 1U) raw = 1U;
    r = NTC_PULLUP_OHM * (float)raw / (4095.0f - (float)raw);
    return 1.0f / (1.0f / 298.15f + logf(r / NTC_R25_OHM) / NTC_BETA) - 273.15f;
}
#endif

/* ------------------------------------------------------------------------- */
/* PID                                                                       */
/* ------------------------------------------------------------------------- */
static void pid_reset(void)
{
    s_integ = 0.0f;
    s_pid_init = false;
}

static float pid_run(float target, float temp, float dt, float out_max)
{
    float err = target - temp;
    float deriv;
    float out;

    if (!s_pid_init) {
        s_prev_temp = temp;
        s_integ = 0.0f;
        s_pid_init = true;
    }
    /* derivative on measurement: no kick on setpoint changes */
    deriv = -(temp - s_prev_temp) / dt;
    s_prev_temp = temp;

    if (err > PID_FULL_BAND) {
        s_integ = 0.0f;
        return out_max;
    }

    s_integ += s_ki * err * dt;
    if (s_integ > out_max) s_integ = out_max;
    if (s_integ < 0.0f)    s_integ = 0.0f;

    out = s_kp * err + s_integ + s_kd * deriv;
    if (out > out_max) out = out_max;
    if (out < 0.0f)    out = 0.0f;
    return out;
}

/* ------------------------------------------------------------------------- */
/* Control loop, ADC DMA interrupt                                           */
/* ------------------------------------------------------------------------- */
void iron_adc_isr(const volatile uint16_t *buf)
{
    uint32_t i;
    uint32_t sum = 0U;
    uint32_t mn = 0xFFFFU;
    uint32_t mx = 0U;
    uint32_t now = sys_ms();
    uint16_t vref_raw;
    float raw;
    float vdda;
    float vin;
    float cj;
    float temp;
    float duty = 0.0f;
    float out_max = 1.0f;
    float dt = (float)heater_period_ms() * 0.001f;
    uint16_t target = s_target;
    bool active;

    /* Thermocouple: average, dropping the extreme samples */
    for (i = 0; i < TC_SAMPLES; i++) {
        uint32_t v = buf[ADC_IDX_TIP + i];
        sum += v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    raw = (float)(sum - mn - mx) / (float)(TC_SAMPLES - 2);

    /* Supply voltage of the analog part from VREFINT */
    vref_raw = buf[ADC_IDX_VREF];
    vdda = (vref_raw > 1000U && VREFINT_CAL != 0xFFFFU)
         ? 3.3f * (float)VREFINT_CAL / (float)vref_raw : 3.3f;

    vin = (float)buf[ADC_IDX_VIN] * vdda / 4095.0f * VIN_DIVIDER;

    s_st.chip_c = s_first ? chip_temp(buf[ADC_IDX_TSENS], vdda)
                          : s_st.chip_c + (chip_temp(buf[ADC_IDX_TSENS], vdda) - s_st.chip_c) * 0.05f;
#if (CJ_SOURCE == CJ_SRC_CHIP)
    cj = s_st.chip_c - CJ_CHIP_OFFSET;
#elif (CJ_SOURCE == CJ_SRC_NTC)
    cj = s_first ? ntc_temp(buf[ADC_IDX_NTC])
                 : s_st.cj_c + (ntc_temp(buf[ADC_IDX_NTC]) - s_st.cj_c) * 0.05f;
#else
    cj = 25.0f;
#endif

    /* Open thermocouple: amplifier input pulled up */
    if (raw > (float)TC_ADC_NO_TIP) {
        if (s_no_tip_cnt < 255U) s_no_tip_cnt++;
    } else {
        s_no_tip_cnt = 0U;
    }
    if (s_no_tip_cnt >= 2U) {
        s_errors |= IRON_ERR_NO_TIP;
    } else if (s_no_tip_cnt == 0U) {
        s_errors &= (uint8_t)~IRON_ERR_NO_TIP;
    }

    /* Light filtering for the controller, heavier for the display */
    if (s_first || (s_errors & IRON_ERR_NO_TIP)) {
        s_st.tip_raw = raw;
    } else {
        s_st.tip_raw += (raw - s_st.tip_raw) * 0.6f;
    }
    temp = raw_to_dt(s_st.tip_raw) + cj;

    if (s_first) {
        s_st.vin = vin;
        s_st.tip_disp = temp;
    } else {
        s_st.vin += (vin - s_st.vin) * 0.2f;
        s_st.tip_disp += (temp - s_st.tip_disp) * 0.3f;
    }
    s_st.tip_c = temp;
    s_st.cj_c  = cj;
    s_first = false;

    /* Safety checks */
    if (raw <= (float)TC_ADC_NO_TIP && temp > (float)TEMP_OVERHEAT) {
        s_errors |= IRON_ERR_OVERHEAT;
    }
    if (g_set.low_volt != 0U) {
        if (s_st.vin * 10.0f < (float)g_set.low_volt) {
            s_errors |= IRON_ERR_LOW_VOLT;
        } else if (s_st.vin * 10.0f > (float)g_set.low_volt + 5.0f) {
            s_errors &= (uint8_t)~IRON_ERR_LOW_VOLT;
        }
    } else {
        s_errors &= (uint8_t)~IRON_ERR_LOW_VOLT;
    }

    /* Power limit: P = duty * Vin^2 / R */
    if (s_power_limit > 0.0f && s_st.vin > 5.0f) {
        float pmax = s_st.vin * s_st.vin / s_heater_r;
        if (pmax > s_power_limit) out_max = s_power_limit / pmax;
    }

    active = (s_mode != IRON_OFF) && (s_errors == 0U) && (target > 0U);
    if (active) {
        duty = pid_run((float)target, temp, dt, out_max);
    } else {
        pid_reset();
    }
    heater_set_duty(duty);

    /* Thermal runaway: (almost) full power, far from the target, no rise */
    if (active && duty >= 0.9f * out_max && temp < (float)target - PID_FULL_BAND) {
        if (!s_rw_active) {
            s_rw_active = true;
            s_rw_start_ms = now;
            s_rw_start_temp = temp;
        } else if ((now - s_rw_start_ms) > RUNAWAY_TIME_MS) {
            if (temp - s_rw_start_temp < (float)RUNAWAY_MIN_RISE) {
                s_errors |= IRON_ERR_RUNAWAY;
            }
            s_rw_start_ms = now;
            s_rw_start_temp = temp;
        }
    } else {
        s_rw_active = false;
    }

    s_st.duty = duty;
    s_st.power_w = duty * s_st.vin * s_st.vin / s_heater_r;
    s_st.cycles++;
    s_last_cycle_ms = now;
}

/* ------------------------------------------------------------------------- */
/* Main loop part                                                            */
/* ------------------------------------------------------------------------- */
void iron_apply_settings(void)
{
    const tip_t *tip = settings_tip();
    uint32_t st;
    uint32_t i;

    st = sys_irq_save();
    s_cal_x[0] = (float)g_set.adc_offset;
    s_cal_y[0] = 0.0f;
    for (i = 0; i < CAL_POINTS; i++) {
        s_cal_x[i + 1U] = (float)tip->cal_adc[i];
        s_cal_y[i + 1U] = (float)tip->cal_dt[i];
    }
    /* keep the table monotonic even with a strange offset */
    if (s_cal_x[0] >= s_cal_x[1]) s_cal_x[0] = s_cal_x[1] - 1.0f;

    s_kp = (float)tip->kp * 0.001f;
    s_ki = (float)tip->ki * 0.001f;
    s_kd = (float)tip->kd * 0.001f;
    s_heater_r = (g_set.heater_res > 0U) ? (float)g_set.heater_res * 0.1f : 8.0f;
    s_power_limit = (float)g_set.power_limit;
    sys_irq_restore(st);

    heater_set_timing(g_set.pwm_period, g_set.adc_delay);
    motion_enable(g_set.motion_en != 0U);
}

void iron_init(void)
{
    uint32_t now = sys_ms();
    heater_init(g_set.pwm_period, g_set.adc_delay);
    iron_apply_settings();
    s_input_act  = input_last_activity();
    s_motion_act = motion_last_ms();
    s_idle_since = now;
    s_last_cycle_ms = now;
    iron_set_mode(g_set.start_mode == START_RUN ? IRON_RUN : IRON_OFF);
}

void iron_set_mode(iron_mode_t mode)
{
    uint32_t now = sys_ms();

    if (mode == IRON_BOOST && s_mode != IRON_BOOST) s_boost_since = now;
    if (mode == IRON_SLEEP && s_mode != IRON_SLEEP) s_sleep_since = now;
    if (mode == IRON_RUN || mode == IRON_BOOST || mode == IRON_CAL) {
        s_idle_since = now;
        if (s_mode == IRON_OFF || s_mode == IRON_SLEEP) s_reached = false;
    }
    s_auto_off = false;
    s_stable_since = 0U;
    s_mode = mode;
}

iron_mode_t iron_mode(void)
{
    return s_mode;
}

bool iron_auto_off(void)
{
    return s_auto_off;
}

uint8_t iron_errors(void)
{
    return s_errors;
}

void iron_clear_errors(void)
{
    uint32_t st = sys_irq_save();
    s_errors &= (uint8_t)~IRON_ERR_LATCHED;
    s_rw_active = false;
    sys_irq_restore(st);
    s_reported_errors = 0U;
}

const iron_status_t *iron_status(void)
{
    return &s_st;
}

uint16_t iron_target(void)
{
    return s_target;
}

uint32_t iron_boost_left_s(void)
{
    uint32_t el;
    if (s_mode != IRON_BOOST) return 0U;
    el = (sys_ms() - s_boost_since) / 1000U;
    return (el < g_set.boost_time) ? (g_set.boost_time - el) : 0U;
}

bool iron_alive(void)
{
    /* the control loop must run at least every 3 PWM periods */
    return (sys_ms() - s_last_cycle_ms) < (3U * (uint32_t)heater_period_ms() + 50U);
}

bool iron_stable(uint32_t ms)
{
    return s_stable_since != 0U && (sys_ms() - s_stable_since) >= ms;
}

void iron_set_cal_target(uint16_t t)
{
    s_cal_target = t;
    s_stable_since = 0U;
}

static uint16_t clamp_setpoint(uint32_t t)
{
    if (t < g_set.temp_min) t = g_set.temp_min;
    if (t > g_set.temp_max) t = g_set.temp_max;
    return (uint16_t)t;
}

void iron_task(void)
{
    uint32_t now = sys_ms();
    uint32_t act;
    bool input_event = false;
    bool motion_event = false;
    uint16_t set = clamp_setpoint(settings_tip()->setpoint);
    uint16_t target = 0U;
    const iron_status_t *st = &s_st;

    /* ---- activity sources ---- */
    act = input_last_activity();
    if (act != s_input_act) {
        s_input_act = act;
        input_event = true;
    }
    act = motion_last_ms();
    if (act != s_motion_act) {
        s_motion_act = act;
        motion_event = g_set.motion_en != 0U;
    }
    if (input_event || motion_event) s_idle_since = now;

    /* ---- latched errors switch the iron off ---- */
    if ((s_errors & IRON_ERR_LATCHED) && s_mode != IRON_OFF) {
        s_mode = IRON_OFF;
    }
    if (s_errors != s_reported_errors) {
        if (s_errors & ~s_reported_errors) buzzer_alarm();
        s_reported_errors = s_errors;
    }

    /* ---- mode timers ---- */
    switch (s_mode) {
    case IRON_BOOST:
        if ((now - s_boost_since) >= (uint32_t)g_set.boost_time * 1000U) {
            s_mode = IRON_RUN;
        }
        /* fall through */
    case IRON_RUN:
        if (g_set.sleep_time != 0U
            && (now - s_idle_since) >= (uint32_t)g_set.sleep_time * 60000U) {
            iron_set_mode(IRON_SLEEP);
            buzzer_short();
        }
        break;

    case IRON_SLEEP:
        /* encoder / button wake-up is handled by the UI (it consumes the event) */
        if (motion_event) {
            iron_set_mode(IRON_RUN);
            buzzer_short();
        } else if (g_set.off_time != 0U
                   && (now - s_sleep_since) >= (uint32_t)g_set.off_time * 60000U) {
            iron_set_mode(IRON_OFF);
            s_auto_off = true;
            buzzer_short();
        }
        break;

    default:
        break;
    }

    /* ---- effective target ---- */
    switch (s_mode) {
    case IRON_RUN:   target = set; break;
    case IRON_BOOST: target = (uint16_t)((set + g_set.boost_add > TEMP_ABS_MAX)
                                         ? TEMP_ABS_MAX : set + g_set.boost_add); break;
    case IRON_SLEEP: target = (g_set.sleep_temp < set) ? g_set.sleep_temp : set; break;
    case IRON_CAL:   target = s_cal_target; break;
    default:         target = 0U; break;
    }
    s_target = target;

    /* ---- stability / ready beep ---- */
    if (target != 0U && s_errors == 0U && fabsf(st->tip_c - (float)target) <= STABLE_BAND) {
        if (s_stable_since == 0U) s_stable_since = now;
    } else {
        s_stable_since = 0U;
    }
    if (s_mode == IRON_RUN && !s_reached && s_errors == 0U
        && fabsf(st->tip_c - (float)target) <= READY_BAND) {
        s_reached = true;
        buzzer_ready();
    }

    led_set(st->duty > 0.0f);
}
