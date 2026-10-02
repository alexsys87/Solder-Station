/**
 * @file    ui.c
 * @brief   Screens of the station.
 *
 * Main screen controls:
 *   rotate              - change the setpoint
 *   press + rotate      - select the tip profile
 *   click               - heater on / off (wake up from sleep)
 *   double click        - boost on / off
 *   long press          - settings menu
 */
#include "ui.h"
#include "menu.h"
#include "iron.h"
#include "settings.h"
#include "input.h"
#include "buzzer.h"
#include "motion.h"
#include "rtc.h"
#include "gfx.h"
#include "oled.h"
#include "sys.h"
#include "config.h"
#include "lang.h"
#include <stdio.h>
#include <string.h>

#define FRAME_MS            40U     /* 25 fps                            */
#define SETPOINT_VIEW_MS    2000U   /* big setpoint after a change       */
#define TIP_VIEW_MS         1500U   /* tip name after a change           */
#define DIM_DELAY_MS        60000U
#define SPLASH_MS           1200U
#define CAL_STABLE_MS       5000U

typedef enum {
    SCR_SPLASH = 0,
    SCR_MAIN,
    SCR_MENU,
    SCR_CAL,
    SCR_INFO,
    SCR_CONFIRM,
    SCR_MSG
} screen_t;

static screen_t s_scr;
static uint32_t s_scr_since;
static uint32_t s_last_draw;
static uint32_t s_set_view_until;
static uint32_t s_tip_view_until;
static uint32_t s_phase_start;
static bool     s_click_pending;
static uint32_t s_click_ms;
static bool     s_dimmed;
static iron_mode_t s_last_mode;

/* confirm / message */
static const char *s_question;
static void (*s_on_yes)(void);
static const char *s_msg1;
static const char *s_msg2;

/* calibration */
static const uint16_t s_cal_targets[CAL_POINTS] = { CAL_T1, CAL_T2, CAL_T3 };
static uint8_t  s_cal_step;
static bool     s_cal_input;
static bool     s_cal_ok;
static int16_t  s_cal_val;
static uint16_t s_cal_adc[CAL_POINTS];
static int16_t  s_cal_dt[CAL_POINTS];
static screen_t s_cal_return;           /* screen to go back to          */

static const uint16_t s_wday[8] = { S_NONE, S_MON, S_TUE, S_WED, S_THU, S_FRI, S_SAT, S_SUN };

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */
static void set_screen(screen_t scr)
{
    s_scr = scr;
    s_scr_since = sys_ms();
}

static int iround(float v)
{
    return (int)(v < 0.0f ? v - 0.5f : v + 0.5f);
}

/* "12.3" from a float with one decimal */
static void fmt_f1(char *buf, size_t n, float v)
{
    int x = iround(v * 10.0f);
    unsigned a = (unsigned)(x < 0 ? -x : x);
    snprintf(buf, n, "%s%u.%u", x < 0 ? "-" : "", (a / 10U) % 10000U, a % 10U);
}

static uint8_t contrast_value(void)
{
    uint32_t v = (uint32_t)g_set.contrast * 255U / 100U;
    return (uint8_t)(v == 0U ? 1U : v);
}

void ui_apply_settings(void)
{
    iron_apply_settings();
    oled_set_contrast(s_dimmed ? 1U : contrast_value());
    oled_set_flip(g_set.flip != 0U);
    buzzer_enable(g_set.buzzer != 0U);
    input_set_invert(g_set.enc_invert != 0U);
}

/* Big number with a degree sign, centred horizontally */
static void draw_big_temp(int y, int value, bool dashes)
{
    char buf[8];
    int w;
    int x;

    if (dashes) snprintf(buf, sizeof(buf), "---");
    else        snprintf(buf, sizeof(buf), "%d", value);
    w = gfx_text_width(buf, &font_big);
    x = (OLED_W - (w + 9)) / 2;
    gfx_text(x, y, buf, &font_big);
    gfx_circle(x + w + 5, y + 5, 3);
    gfx_circle(x + w + 5, y + 5, 2);
}

/* ------------------------------------------------------------------------- */
/* Public API used by the menu                                               */
/* ------------------------------------------------------------------------- */
void ui_confirm(const char *question, void (*on_yes)(void))
{
    s_question = question;
    s_on_yes = on_yes;
    set_screen(SCR_CONFIRM);
}

void ui_message(const char *line1, const char *line2)
{
    s_msg1 = line1;
    s_msg2 = line2;
    set_screen(SCR_MSG);
}

void ui_show_setpoint(void)
{
    uint32_t now = sys_ms();
    s_set_view_until = now + SETPOINT_VIEW_MS;
    s_phase_start = now;
}

void ui_show_info(void)
{
    set_screen(SCR_INFO);
}

void ui_start_calibration(void)
{
    s_cal_return = (s_scr == SCR_CAL) ? s_cal_return : s_scr;
    if (s_cal_return != SCR_MENU) s_cal_return = SCR_MAIN;
    s_cal_step  = 0U;
    s_cal_input = false;
    s_cal_ok    = false;
    iron_set_cal_target(s_cal_targets[0]);
    iron_set_mode(IRON_CAL);
    set_screen(SCR_CAL);
}

/* ------------------------------------------------------------------------- */
/* Main screen                                                               */
/* ------------------------------------------------------------------------- */
static void main_click(void)
{
    if (iron_errors() & IRON_ERR_LATCHED) {
        iron_clear_errors();
        return;
    }
    switch (iron_mode()) {
    case IRON_OFF:   iron_set_mode(IRON_RUN); break;
    case IRON_SLEEP: iron_set_mode(IRON_RUN); break;
    default:         iron_set_mode(IRON_OFF); break;
    }
}

static void main_double(void)
{
    switch (iron_mode()) {
    case IRON_RUN:
    case IRON_OFF:   iron_set_mode(IRON_BOOST); break;
    default:         iron_set_mode(IRON_RUN); break;
    }
}

static void main_input(const input_rot_t *rot, btn_event_t ev)
{
    uint32_t now = sys_ms();
    iron_mode_t mode = iron_mode();

    /* restart the setpoint / clock alternation when the mode changes */
    if (mode != s_last_mode) {
        s_last_mode = mode;
        s_phase_start = now;
    }

    /* sleeping: a click (or a turn, if allowed) only wakes the iron up */
    if (mode == IRON_SLEEP) {
        s_click_pending = false;
        if (ev != BTN_NONE || (g_set.wake_on_enc && (rot->steps != 0 || rot->pressed_steps != 0))) {
            iron_set_mode(IRON_RUN);
            buzzer_short();
            s_set_view_until = 0U;
        }
        return;
    }

    /* press + rotate: select the tip */
    if (rot->pressed_steps != 0 && g_set.tip_count > 1U) {
        int32_t n = g_set.tip_count;
        int32_t t = ((int32_t)g_set.tip_active + rot->pressed_steps) % n;
        if (t < 0) t += n;
        g_set.tip_active = (uint8_t)t;
        iron_apply_settings();
        settings_save_later();
        s_tip_view_until = now + TIP_VIEW_MS;
    }

    /* rotate: setpoint */
    if (rot->accel != 0) {
        tip_t *tip = settings_tip();
        int32_t step = g_set.temp_step;
        int32_t sp = (int32_t)tip->setpoint + rot->accel * step;
        if (step > 1) sp = ((sp + step / 2) / step) * step;
        if (sp < g_set.temp_min) sp = g_set.temp_min;
        if (sp > g_set.temp_max) sp = g_set.temp_max;
        if (sp != tip->setpoint) {
            tip->setpoint = (uint16_t)sp;
            settings_save_later();
        }
        s_set_view_until = now + SETPOINT_VIEW_MS;
    }

    if (rot->steps != 0 || rot->pressed_steps != 0 || ev != BTN_NONE) {
        s_phase_start = now;    /* restart clock / setpoint alternation */
    }

    switch (ev) {
    case BTN_CLICK:
        s_click_pending = true;
        s_click_ms = now;
        break;
    case BTN_DOUBLE:
        s_click_pending = false;
        main_double();
        break;
    case BTN_LONG:
        s_click_pending = false;
        menu_open();
        set_screen(SCR_MENU);
        return;
    default:
        break;
    }

    /* a single click is confirmed when no second click followed */
    if (s_click_pending && (now - s_click_ms) > (BTN_DOUBLE_MS + 30U)) {
        s_click_pending = false;
        main_click();
    }
}

static bool clock_phase(uint32_t now)
{
    uint32_t set_ms = (uint32_t)g_set.set_show * 1000U;
    uint32_t per = set_ms + (uint32_t)g_set.clock_show * 1000U;
    if (per == 0U) return false;
    return ((now - s_phase_start) % per) >= set_ms;
}

static void draw_status_bar(const char *left, const char *right)
{
    gfx_text(0, 0, left, &font_small);
    gfx_text_right(OLED_W, 0, right, &font_small);
    gfx_hline(0, 9, OLED_W);
}

static void draw_clock(void)
{
    rtc_time_t t;
    char hh[4];
    char mm[4];
    char buf[24];
    char tip[16];
    int wh, wc, wm, x;
    uint8_t hour;
    const int y = 13;

    rtc_get(&t);
    hour = t.hour;
    if (!g_set.clock_24h) {
        hour = (uint8_t)(t.hour % 12U);
        if (hour == 0U) hour = 12U;
    }
    snprintf(hh, sizeof(hh), "%u", (unsigned)hour);
    snprintf(mm, sizeof(mm), "%02u", (unsigned)t.min);

    snprintf(buf, sizeof(buf), tr(S_OFF_SET_FMT), (unsigned)settings_tip()->setpoint);
    if (iron_errors() & IRON_ERR_NO_TIP) snprintf(tip, sizeof(tip), "%s", tr(S_NO_TIP));
    else snprintf(tip, sizeof(tip), "%d" CH_DEG "C", iround(iron_status()->tip_disp));
    draw_status_bar(buf, tip);

    wh = gfx_text_width(hh, &font_big);
    wc = gfx_text_width(":", &font_big);
    wm = gfx_text_width(mm, &font_big);
    x = (OLED_W - (wh + wc + wm + 4)) / 2;
    x = gfx_text(x, y, hh, &font_big);
    if ((t.sec & 1U) == 0U) gfx_text(x, y, ":", &font_big);
    x += wc + font_big.spacing;
    x = gfx_text(x, y, mm, &font_big);
    if (!g_set.clock_24h) {
        gfx_text(x, y, t.hour < 12U ? "AM" : "PM", &font_small);
    }

    snprintf(buf, sizeof(buf), "%s %02u.%02u.20%02u", tr((str_id_t)s_wday[t.wday & 7U]),
             (unsigned)t.day, (unsigned)t.month, (unsigned)t.year);
    gfx_text_center(55, buf, &font_small);
}

static void draw_error(uint8_t err)
{
    const char *title;
    char hint[24];
    char v[12];

    if (err & IRON_ERR_OVERHEAT) {
        title = tr(S_ERR_OVERHEAT);
        snprintf(hint, sizeof(hint), "%s", tr(S_HINT_RESET));
    } else if (err & IRON_ERR_RUNAWAY) {
        title = tr(S_ERR_RUNAWAY);
        snprintf(hint, sizeof(hint), "%s", tr(S_HINT_HEATER));
    } else if (err & IRON_ERR_NO_TIP) {
        title = tr(S_ERR_NO_TIP);
        snprintf(hint, sizeof(hint), "%s", tr(S_HINT_TIP));
    } else {
        title = tr(S_ERR_LOW_VOLT);
        fmt_f1(v, sizeof(v), iron_status()->vin);
        snprintf(hint, sizeof(hint), tr(S_HINT_VIN_FMT), v);
    }
    gfx_text2x((OLED_W - gfx_text2x_width(title)) / 2, 20, title);
    gfx_text_center(46, hint, &font_small);
}

static void draw_main(void)
{
    uint32_t now = sys_ms();
    const iron_status_t *st = iron_status();
    iron_mode_t mode = iron_mode();
    uint8_t err = iron_errors();
    const tip_t *tip = settings_tip();
    char right[24];
    char left[24];
    char v[12];
    bool show_set;
    bool no_tip = (err & IRON_ERR_NO_TIP) != 0U;
    int tip_t_c = iround(st->tip_disp);

    gfx_clear();

    /* OFF: alternate the setpoint screen with the clock */
    if (mode == IRON_OFF && g_set.clock_en && !(err & IRON_ERR_LATCHED)
        && now >= s_set_view_until && now >= s_tip_view_until && clock_phase(now)) {
        draw_clock();
        return;
    }

    /* status bar */
    switch (mode) {
    case IRON_OFF:
        snprintf(right, sizeof(right), "%s", tr(iron_auto_off() ? S_ST_AUTO_OFF : S_ST_OFF));
        break;
    case IRON_RUN:
        if (iron_stable(500U)) snprintf(right, sizeof(right), "%s", tr(S_ST_READY));
        else                   snprintf(right, sizeof(right), CH_FLASH "%s", tr(S_ST_HEAT));
        break;
    case IRON_BOOST:
        right[0] = CH_UP[0];
        snprintf(&right[1], sizeof(right) - 1U, tr(S_ST_BOOST_FMT), (unsigned long)iron_boost_left_s());
        break;
    case IRON_SLEEP: snprintf(right, sizeof(right), "%s", tr(S_ST_SLEEP)); break;
    default:         snprintf(right, sizeof(right), "%s", tr(S_ST_CAL)); break;
    }
    draw_status_bar(tip->name, right);

    if ((err & IRON_ERR_LATCHED) || (err && mode != IRON_OFF)) {
        draw_error(err);
        return;
    }

    /* big number: measured temperature, or the setpoint while it is being
     * changed and whenever the heater is off */
    show_set = (now < s_set_view_until) || (mode == IRON_OFF);
    if (show_set) {
        draw_big_temp(13, tip->setpoint, false);
        gfx_text(0, 13, tr(S_SET), &font_small);
    } else {
        draw_big_temp(13, tip_t_c, no_tip);
    }

    /* heater power bar */
    if (mode != IRON_OFF) {
        gfx_rect(0, 47, OLED_W, 6);
        gfx_fill(1, 48, iround(st->duty * (float)(OLED_W - 2)), 4);
    }

    /* bottom line */
    fmt_f1(v, sizeof(v), st->vin);
    switch (mode) {
    case IRON_OFF:
        if (no_tip) snprintf(left, sizeof(left), "%s", tr(S_NO_TIP));
        else        snprintf(left, sizeof(left), tr(S_TIP_FMT), tip_t_c);
        if (!no_tip && tip_t_c >= TEMP_HOT_WARN && ((now / 500U) & 1U)) {
            snprintf(right, sizeof(right), "%s", tr(S_HOT));
        } else {
            snprintf(right, sizeof(right), tr(S_V_FMT), v);
        }
        break;
    case IRON_SLEEP:
        snprintf(left, sizeof(left), tr(S_SLEEP_FMT), (unsigned)iron_target());
        snprintf(right, sizeof(right), tr(S_V_FMT), v);
        break;
    default:
        if (show_set) snprintf(left, sizeof(left), tr(S_TIP_FMT), tip_t_c);
        else          snprintf(left, sizeof(left), tr(S_SET_FMT), (unsigned)iron_target());
        snprintf(right, sizeof(right), tr(S_W_V_FMT), iround(st->power_w), v);
        break;
    }
    gfx_text(0, 56, left, &font_small);
    gfx_text_right(OLED_W, 56, right, &font_small);

    /* tip name pop-up after a tip change */
    if (now < s_tip_view_until) {
        gfx_set_mode(GFX_CLR);
        gfx_fill(0, 11, OLED_W, 44);
        gfx_set_mode(GFX_SET);
        gfx_rect(4, 14, OLED_W - 8, 32);
        gfx_text_center(17, tr(S_TIP), &font_small);
        gfx_text2x((OLED_W - gfx_text2x_width(tip->name)) / 2, 27, tip->name);
    }
}

/* ------------------------------------------------------------------------- */
/* Calibration                                                               */
/* ------------------------------------------------------------------------- */
static void cal_finish(void)
{
    tip_t *tip = settings_tip();
    uint32_t i;

    s_cal_ok = s_cal_dt[0] > 0
            && s_cal_adc[0] < s_cal_adc[1] && s_cal_adc[1] < s_cal_adc[2]
            && s_cal_dt[0]  < s_cal_dt[1]  && s_cal_dt[1]  < s_cal_dt[2];
    iron_set_mode(IRON_OFF);
    if (s_cal_ok) {
        for (i = 0; i < CAL_POINTS; i++) {
            tip->cal_adc[i] = s_cal_adc[i];
            tip->cal_dt[i]  = s_cal_dt[i];
        }
        ui_apply_settings();
        settings_save();
        buzzer_ready();
    } else {
        buzzer_alarm();
    }
    s_cal_step = CAL_POINTS;
}

/* Store the current point with the real (measured) tip temperature */
static void cal_record(int measured)
{
    const iron_status_t *st = iron_status();

    s_cal_adc[s_cal_step] = (uint16_t)iround(st->tip_raw);
    s_cal_dt[s_cal_step]  = (int16_t)(measured - iround(st->cj_c));
    s_cal_step++;
    s_cal_input = false;
    if (s_cal_step < CAL_POINTS) {
        iron_set_cal_target(s_cal_targets[s_cal_step]);
    } else {
        cal_finish();
    }
}

static void cal_exit(void)
{
    if (iron_mode() == IRON_CAL) iron_set_mode(IRON_OFF);
    set_screen(s_cal_return);
}

static void cal_input(const input_rot_t *rot, btn_event_t ev)
{
    if (ev == BTN_LONG) {                      /* abort / exit */
        cal_exit();
        return;
    }
    if (s_cal_step >= CAL_POINTS) {
        if (ev != BTN_NONE) cal_exit();
        return;
    }
    if (iron_errors() != 0U) return;

    if (!s_cal_input) {
        if (iron_stable(CAL_STABLE_MS) || ev == BTN_CLICK) {
            s_cal_input = true;
            s_cal_val = (int16_t)s_cal_targets[s_cal_step];
            buzzer_short();
        }
        return;
    }

    s_cal_val = (int16_t)(s_cal_val + rot->accel);
    if (s_cal_val < 50)  s_cal_val = 50;
    if (s_cal_val > 600) s_cal_val = 600;

    if (ev == BTN_CLICK) {
        cal_record(s_cal_val);
    }
}

/* ---- remote calibration (protocol) ---- */
bool ui_cal_active(void)
{
    return s_scr == SCR_CAL;
}

bool ui_cal_point(int measured)
{
    if (s_scr != SCR_CAL || s_cal_step >= CAL_POINTS || iron_errors() != 0U) return false;
    if (measured < 50 || measured > 600) return false;
    cal_record(measured);
    return true;
}

void ui_cal_abort(void)
{
    if (s_scr == SCR_CAL) cal_exit();
}

void ui_cal_state(ui_cal_state_t *cs)
{
    cs->active = (s_scr == SCR_CAL);
    cs->step   = s_cal_step;
    cs->target = (s_cal_step < CAL_POINTS) ? s_cal_targets[s_cal_step] : 0U;
    cs->stable = iron_stable(CAL_STABLE_MS);
    cs->done   = (s_cal_step >= CAL_POINTS);
    cs->ok     = s_cal_ok;
}

static void draw_title(const char *title)
{
    gfx_fill(0, 0, OLED_W, 9);
    gfx_set_mode(GFX_CLR);
    gfx_text(2, 1, title, &font_small);
    gfx_set_mode(GFX_SET);
}

static void draw_cal(void)
{
    char buf[24];
    const iron_status_t *st = iron_status();

    gfx_clear();
    if (s_cal_step >= CAL_POINTS) {
        const char *res = tr(s_cal_ok ? S_DONE : S_FAILED);
        draw_title(tr(S_CALIBRATION));
        gfx_text2x((OLED_W - gfx_text2x_width(res)) / 2, 20, res);
        gfx_text_center(44, tr(s_cal_ok ? S_CAL_SAVED : S_CAL_BAD), &font_small);
        gfx_text_center(55, tr(S_CLICK_EXIT), &font_small);
        return;
    }

    snprintf(buf, sizeof(buf), tr(S_CAL_STEP_FMT), (unsigned)(s_cal_step + 1U), (unsigned)CAL_POINTS);
    draw_title(buf);

    if (iron_errors() != 0U) {
        draw_error(iron_errors());
        return;
    }

    if (!s_cal_input) {
        snprintf(buf, sizeof(buf), tr(S_TARGET_FMT), (unsigned)s_cal_targets[s_cal_step]);
        gfx_text(0, 11, buf, &font_small);
        draw_big_temp(20, iround(st->tip_disp), false);
        gfx_text(0, 56, tr(iron_stable(500U) ? S_SETTLING : S_HEATING), &font_small);
        gfx_text_right(OLED_W, 56, tr(S_CLICK_SKIP), &font_small);
    } else {
        gfx_text(0, 11, tr(S_REAL_TEMP), &font_small);
        draw_big_temp(20, s_cal_val, false);
        gfx_text(0, 56, tr(S_CLICK_OK), &font_small);
        gfx_text_right(OLED_W, 56, tr(S_HOLD_ABORT), &font_small);
    }
}

/* ------------------------------------------------------------------------- */
/* Info / confirm / message / splash                                         */
/* ------------------------------------------------------------------------- */
static void draw_info(void)
{
    const iron_status_t *st = iron_status();
    char buf[40];
    char a[12];
    char b[12];

    gfx_clear();
    snprintf(buf, sizeof(buf), "%s  FW " FW_VERSION_STR, tr(S_INFO));
    draw_title(buf);

    fmt_f1(a, sizeof(a), st->vin);
    fmt_f1(b, sizeof(b), st->power_w);
    snprintf(buf, sizeof(buf), tr(S_INFO_VIN_FMT), a, b);
    gfx_text(0, 11, buf, &font_small);

    snprintf(buf, sizeof(buf), tr(S_INFO_TIP_FMT), iround(st->tip_c), iround(st->tip_raw));
    gfx_text(0, 20, buf, &font_small);

    fmt_f1(a, sizeof(a), st->cj_c);
    fmt_f1(b, sizeof(b), st->chip_c);
    snprintf(buf, sizeof(buf), tr(S_INFO_CJ_FMT), a, b);
    gfx_text(0, 29, buf, &font_small);

    snprintf(buf, sizeof(buf), tr(S_INFO_DUTY_FMT), iround(st->duty * 1000.0f) / 10,
             iround(st->duty * 1000.0f) % 10, (unsigned)iron_errors());
    gfx_text(0, 38, buf, &font_small);

    snprintf(buf, sizeof(buf), "HSE %s  RTC %s", sys_hse_ok() ? "ok" : "FAIL",
             rtc_lse_ok() ? "LSE" : "LSI");
    gfx_text(0, 47, buf, &font_small);

    snprintf(buf, sizeof(buf), tr(S_INFO_SAVES_FMT), (unsigned long)g_set.seq,
             (unsigned long)motion_count());
    gfx_text(0, 56, buf, &font_small);
}

static void draw_confirm(void)
{
    gfx_clear();
    draw_title(tr(S_CONFIRM));
    gfx_text_center(20, s_question, &font_small);
    gfx_text_center(40, tr(S_CLICK_YES), &font_small);
    gfx_text_center(50, tr(S_HOLD_NO), &font_small);
}

static void draw_msg(void)
{
    gfx_clear();
    gfx_rect(0, 0, OLED_W, OLED_H);
    gfx_text_center(18, s_msg1, &font_small);
    gfx_text_center(30, s_msg2, &font_small);
    gfx_text_center(50, tr(S_CLICK), &font_small);
}

static void draw_splash(void)
{
    gfx_clear();
    gfx_text2x((OLED_W - gfx_text2x_width("T12")) / 2, 4, "T12");
    gfx_text_center(24, tr(S_STATION), &font_small);
    gfx_text_center(36, "FW " FW_VERSION_STR, &font_small);
#if (OLED_CONTROLLER == OLED_SH1106)
    gfx_text_center(48, "STM32F401  SH1106", &font_small);
#else
    gfx_text_center(48, "STM32F401  SSD1306", &font_small);
#endif
    if (!settings_loaded()) gfx_text_center(56, tr(S_DEFAULTS_LOADED), &font_small);
}

/* ------------------------------------------------------------------------- */
void ui_init(void)
{
    ui_apply_settings();
    s_phase_start = sys_ms();
    set_screen(SCR_SPLASH);
    draw_splash();
    oled_flush();
}

void ui_task(void)
{
    uint32_t now = sys_ms();
    input_rot_t rot;
    btn_event_t ev;
    bool want_dim;
    iron_mode_t mode;

    input_take_rotation(&rot);
    ev = input_take_button();
    if (ev != BTN_NONE) buzzer_click();

    switch (s_scr) {
    case SCR_SPLASH:
        if ((now - s_scr_since) > SPLASH_MS || ev != BTN_NONE) set_screen(SCR_MAIN);
        break;

    case SCR_MAIN:
        main_input(&rot, ev);
        break;

    case SCR_MENU:
        if (!menu_input(&rot, ev)) {
            ui_apply_settings();
            settings_save();
            s_phase_start = now;
            set_screen(SCR_MAIN);
        }
        break;

    case SCR_CAL:
        cal_input(&rot, ev);
        break;

    case SCR_CONFIRM:
        if (ev == BTN_CLICK || ev == BTN_DOUBLE) {
            set_screen(SCR_MENU);
            if (s_on_yes != 0) s_on_yes();
        } else if (ev == BTN_LONG) {
            set_screen(SCR_MENU);
        }
        break;

    case SCR_INFO:
    case SCR_MSG:
        if (ev != BTN_NONE) set_screen(SCR_MENU);
        break;

    default:
        set_screen(SCR_MAIN);
        break;
    }

    /* dim the display when nothing happens */
    mode = iron_mode();
    want_dim = g_set.dim_idle && s_scr == SCR_MAIN
            && (mode == IRON_OFF || mode == IRON_SLEEP)
            && (now - input_last_activity()) > DIM_DELAY_MS;
    if (want_dim != s_dimmed) {
        s_dimmed = want_dim;
        oled_set_contrast(want_dim ? 1U : contrast_value());
    }

    /* render */
    if ((now - s_last_draw) >= FRAME_MS && !oled_busy()) {
        s_last_draw = now;
        switch (s_scr) {
        case SCR_SPLASH:  draw_splash();  break;
        case SCR_MAIN:    draw_main();    break;
        case SCR_MENU:    menu_draw();    break;
        case SCR_CAL:     draw_cal();     break;
        case SCR_INFO:    draw_info();    break;
        case SCR_CONFIRM: draw_confirm(); break;
        case SCR_MSG:     draw_msg();     break;
        default: break;
        }
        oled_flush();
    }
}
