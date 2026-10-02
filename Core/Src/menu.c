/**
 * @file    menu.c
 * @brief   Settings menu: generic engine driven by constant item tables.
 *
 * Navigation: rotate = move, click = open / edit / confirm,
 *             long press = back (leaves the menu from the top level).
 */
#include "menu.h"
#include "settings.h"
#include "iron.h"
#include "rtc.h"
#include "gfx.h"
#include "oled.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>

/* ------------------------------------------------------------------------- */
/* Item description                                                          */
/* ------------------------------------------------------------------------- */
typedef enum {
    MI_SUB = 0,     /* opens a sub menu                         */
    MI_ACT,         /* calls a function                         */
    MI_U8,          /* numbers                                  */
    MI_U16,
    MI_I16,
    MI_BOOL,        /* uint8_t On / Off, toggled by a click     */
    MI_LIST,        /* uint8_t index into a list of names       */
    MI_TEXT,        /* char[TIP_NAME_LEN + 1]                   */
    MI_BACK
} mi_type_t;

#define MF_TIP      0x01U   /* value lives in the active tip (tofs)  */
#define MF_ZOFF     0x02U   /* 0 is shown as "Off"                   */
#define MF_ACCEL    0x04U   /* fast rotation changes faster          */

typedef struct menu menu_t;

typedef struct {
    const char         *label;
    uint8_t             type;
    uint8_t             flags;
    uint8_t             dec;            /* decimals of fixed point numbers */
    uint8_t             step;
    void               *ptr;
    uint16_t            tofs;           /* offset in tip_t (MF_TIP)        */
    int16_t             min;
    int16_t             max;
    const char         *unit;
    const char * const *opts;
    const menu_t       *sub;
    void              (*action)(void);
    int16_t           (*max_fn)(void);
    const char       *(*fmt_fn)(int32_t v);
} menu_item_t;

struct menu {
    const char         *title;
    const menu_item_t  *items;
    uint8_t             count;
    void              (*on_enter)(void);
    const char       *(*title_fn)(void);
};

#define M_SUB(l, m)         { .label = (l), .type = MI_SUB, .sub = &(m) }
#define M_ACT(l, fn)        { .label = (l), .type = MI_ACT, .action = (fn) }
#define M_BACK(l)           { .label = (l), .type = MI_BACK }
#define M_BOOL(l, p)        { .label = (l), .type = MI_BOOL, .ptr = (p), .min = 0, .max = 1 }
#define M_LIST(l, p, o)     { .label = (l), .type = MI_LIST, .ptr = (p), .opts = (o), \
                              .min = 0, .max = (int16_t)(sizeof(o) / sizeof((o)[0]) - 1U) }
#define M_NUM(l, t, p, mn, mx, st, d, u, f) \
                            { .label = (l), .type = (t), .ptr = (p), .min = (mn), .max = (mx), \
                              .step = (st), .dec = (d), .unit = (u), .flags = (f) }
#define M_TIPNUM(l, t, field, mn, mx, st, d, u, f) \
                            { .label = (l), .type = (t), .tofs = (uint16_t)offsetof(tip_t, field), \
                              .min = (mn), .max = (mx), .step = (st), .dec = (d), .unit = (u), \
                              .flags = (uint8_t)((f) | MF_TIP) }
#define MENU(t, items, enter, tfn) \
                            { (t), (items), (uint8_t)(sizeof(items) / sizeof((items)[0])), (enter), (tfn) }

/* ------------------------------------------------------------------------- */
/* Navigation state                                                          */
/* ------------------------------------------------------------------------- */
#define MENU_ROWS       6
#define MENU_DEPTH      4
#define ROW_H           9
#define ROW_Y0          10

typedef struct {
    const menu_t *m;
    uint8_t sel;
    uint8_t top;
} level_t;

static level_t  s_stack[MENU_DEPTH];
static int8_t   s_depth;
static bool     s_edit;
static uint8_t  s_text_pos;
static bool     s_exit;
static rtc_time_t s_time;

static const char s_charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.+/#";

static void menu_push(const menu_t *m);
static void menu_pop(void);

/* ------------------------------------------------------------------------- */
/* Value helpers                                                             */
/* ------------------------------------------------------------------------- */
static void *item_ptr(const menu_item_t *it)
{
    if (it->flags & MF_TIP) return (uint8_t *)settings_tip() + it->tofs;
    return it->ptr;
}

static int32_t item_get(const menu_item_t *it)
{
    void *p = item_ptr(it);
    switch (it->type) {
    case MI_U16: return *(uint16_t *)p;
    case MI_I16: return *(int16_t *)p;
    default:     return *(uint8_t *)p;
    }
}

static void item_set(const menu_item_t *it, int32_t v)
{
    void *p = item_ptr(it);
    switch (it->type) {
    case MI_U16: *(uint16_t *)p = (uint16_t)v; break;
    case MI_I16: *(int16_t *)p  = (int16_t)v;  break;
    default:     *(uint8_t *)p  = (uint8_t)v;  break;
    }
}

static int32_t item_max(const menu_item_t *it)
{
    return it->max_fn != 0 ? it->max_fn() : it->max;
}

static void fmt_fixed(char *buf, size_t n, int32_t v, uint8_t dec, const char *unit)
{
    const char *u = unit != 0 ? unit : "";
    if (dec == 0U) {
        snprintf(buf, n, "%ld%s", (long)v, u);
    } else {
        int32_t div = (dec == 1U) ? 10 : (dec == 2U) ? 100 : 1000;
        int32_t a = v < 0 ? -v : v;
        if (dec == 1U) {
            snprintf(buf, n, "%s%ld.%01ld%s", v < 0 ? "-" : "", (long)(a / div), (long)(a % div), u);
        } else if (dec == 2U) {
            snprintf(buf, n, "%s%ld.%02ld%s", v < 0 ? "-" : "", (long)(a / div), (long)(a % div), u);
        } else {
            snprintf(buf, n, "%s%ld.%03ld%s", v < 0 ? "-" : "", (long)(a / div), (long)(a % div), u);
        }
    }
}

static void item_value_str(const menu_item_t *it, char *buf, size_t n)
{
    int32_t v;
    buf[0] = '\0';
    switch (it->type) {
    case MI_SUB:
        snprintf(buf, n, CH_RIGHT);
        break;
    case MI_ACT:
    case MI_BACK:
        break;
    case MI_TEXT:
        snprintf(buf, n, "%s", (const char *)item_ptr(it));
        break;
    case MI_BOOL:
        snprintf(buf, n, "%s", item_get(it) ? "On" : "Off");
        break;
    case MI_LIST:
        v = item_get(it);
        if (it->fmt_fn != 0) snprintf(buf, n, "%s", it->fmt_fn(v));
        else                 snprintf(buf, n, "%s", it->opts[v]);
        break;
    default:
        v = item_get(it);
        if ((it->flags & MF_ZOFF) && v == 0) snprintf(buf, n, "Off");
        else if (it->fmt_fn != 0)            snprintf(buf, n, "%s", it->fmt_fn(v));
        else                                 fmt_fixed(buf, n, v, it->dec, it->unit);
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* Dynamic helpers                                                           */
/* ------------------------------------------------------------------------- */
static int16_t tip_max(void)
{
    return (int16_t)(g_set.tip_count - 1U);
}

static const char *tip_name(int32_t v)
{
    return g_set.tips[v].name;
}

static const char *tip_title(void)
{
    static char buf[24];
    snprintf(buf, sizeof(buf), "Tip: %s", settings_tip()->name);
    return buf;
}

static const char *fmt_year(int32_t v)
{
    static char buf[8];
    snprintf(buf, sizeof(buf), "20%02ld", (long)v);
    return buf;
}

static const char *fmt_boost(int32_t v)
{
    static char buf[10];
    snprintf(buf, sizeof(buf), "+%ld" CH_DEG "C", (long)v);
    return buf;
}

/* ------------------------------------------------------------------------- */
/* Actions                                                                   */
/* ------------------------------------------------------------------------- */
static void act_calibrate(void)
{
    ui_start_calibration();
}

static void do_reset_cal(void)
{
    settings_reset_cal(settings_tip());
    ui_apply_settings();
}

static void act_reset_cal(void)
{
    ui_confirm("Reset calibration?", do_reset_cal);
}

static void do_delete_tip(void)
{
    uint8_t i = g_set.tip_active;
    if (g_set.tip_count <= 1U) return;
    for (; i + 1U < g_set.tip_count; i++) {
        g_set.tips[i] = g_set.tips[i + 1U];
    }
    g_set.tip_count--;
    if (g_set.tip_active >= g_set.tip_count) g_set.tip_active = (uint8_t)(g_set.tip_count - 1U);
    ui_apply_settings();
    menu_pop();
}

static void act_delete_tip(void)
{
    if (g_set.tip_count <= 1U) {
        ui_message("Cannot delete", "the last tip");
    } else {
        ui_confirm("Delete this tip?", do_delete_tip);
    }
}

static void time_enter(void)
{
    rtc_get(&s_time);
}

static void act_time_save(void)
{
    uint8_t dim;
    if (s_time.month < 1U)  s_time.month = 1U;
    if (s_time.month > 12U) s_time.month = 12U;
    dim = rtc_days_in_month(s_time.month, s_time.year);
    if (s_time.day > dim) s_time.day = dim;
    if (s_time.day < 1U)  s_time.day = 1U;
    s_time.sec = 0U;
    rtc_set(&s_time);
    menu_pop();
    ui_message("Time saved", "");
}

static void act_info(void)
{
    ui_show_info();
}

static void do_factory(void)
{
    settings_defaults();
    ui_apply_settings();
    ui_message("Defaults", "loaded");
}

static void act_factory(void)
{
    ui_confirm("Factory reset?", do_factory);
}

/* ------------------------------------------------------------------------- */
/* Menu tables (leaves first)                                                */
/* ------------------------------------------------------------------------- */
static const char * const s_on_start[] = { "Off", "Heat" };
static const char * const s_fmt24[]    = { "12h", "24h" };
static const char * const s_enc_dir[]  = { "Normal", "Reverse" };

/* --- Tip settings (operate on the active tip) --- */
static const menu_item_t s_tip_items[] = {
    { .label = "Name", .type = MI_TEXT, .tofs = (uint16_t)offsetof(tip_t, name), .flags = MF_TIP },
    M_ACT("Calibrate", act_calibrate),
    M_TIPNUM("PID Kp", MI_U16, kp, 0, 2000, 1, 3, "", MF_ACCEL),
    M_TIPNUM("PID Ki", MI_U16, ki, 0, 2000, 1, 3, "", MF_ACCEL),
    M_TIPNUM("PID Kd", MI_U16, kd, 0, 2000, 1, 3, "", MF_ACCEL),
    M_ACT("Reset calib.", act_reset_cal),
    M_ACT("Delete tip", act_delete_tip),
    M_BACK("Back"),
};
static const menu_t m_tip = MENU("Tip", s_tip_items, 0, tip_title);

/* --- Temperatures --- */
static const menu_item_t s_temp_items[] = {
    M_NUM("Min temp",   MI_U16, &g_set.temp_min,   TEMP_ABS_MIN, TEMP_ABS_MAX, 5, 0, CH_DEG "C", MF_ACCEL),
    M_NUM("Max temp",   MI_U16, &g_set.temp_max,   TEMP_ABS_MIN, TEMP_ABS_MAX, 5, 0, CH_DEG "C", MF_ACCEL),
    M_NUM("Temp step",  MI_U8,  &g_set.temp_step,  1, 25, 1, 0, CH_DEG "C", 0),
    { .label = "Boost temp", .type = MI_U16, .ptr = &g_set.boost_add, .min = 10, .max = 150,
      .step = 5, .fmt_fn = fmt_boost },
    M_NUM("Boost time", MI_U16, &g_set.boost_time, 10, 600, 10, 0, "s", MF_ACCEL),
    M_BACK("Back"),
};
static const menu_t m_temp = MENU("Temperature", s_temp_items, 0, 0);

/* --- Sleep --- */
static const menu_item_t s_sleep_items[] = {
    M_BOOL("Motion sensor", &g_set.motion_en),
    M_NUM("Sleep after",  MI_U8,  &g_set.sleep_time, 0, 60, 1, 0, "min", MF_ZOFF),
    M_NUM("Sleep temp",   MI_U16, &g_set.sleep_temp, TEMP_ABS_MIN, 300, 10, 0, CH_DEG "C", 0),
    M_NUM("Off after",    MI_U8,  &g_set.off_time, 0, 120, 1, 0, "min", MF_ZOFF | MF_ACCEL),
    M_BOOL("Enc. wakes",  &g_set.wake_on_enc),
    M_LIST("Power on",    &g_set.start_mode, s_on_start),
    M_BACK("Back"),
};
static const menu_t m_sleep = MENU("Sleep", s_sleep_items, 0, 0);

/* --- Clock: set time --- */
static const menu_item_t s_time_items[] = {
    M_NUM("Hours",   MI_U8, &s_time.hour,  0, 23, 1, 0, "", 0),
    M_NUM("Minutes", MI_U8, &s_time.min,   0, 59, 1, 0, "", MF_ACCEL),
    M_NUM("Day",     MI_U8, &s_time.day,   1, 31, 1, 0, "", 0),
    M_NUM("Month",   MI_U8, &s_time.month, 1, 12, 1, 0, "", 0),
    { .label = "Year", .type = MI_U8, .ptr = &s_time.year, .min = 0, .max = 99, .step = 1,
      .fmt_fn = fmt_year },
    M_ACT("Save", act_time_save),
    M_BACK("Cancel"),
};
static const menu_t m_time = MENU("Set time", s_time_items, time_enter, 0);

/* --- Clock --- */
static const menu_item_t s_clock_items[] = {
    M_BOOL("Show clock", &g_set.clock_en),
    M_LIST("Format",     &g_set.clock_24h, s_fmt24),
    M_NUM("Clock time",  MI_U8, &g_set.clock_show, 1, 60, 1, 0, "s", 0),
    M_NUM("Setpt time",  MI_U8, &g_set.set_show,   1, 60, 1, 0, "s", 0),
    M_SUB("Set time",    m_time),
    M_BACK("Back"),
};
static const menu_t m_clock = MENU("Clock", s_clock_items, 0, 0);

/* --- Display --- */
static const menu_item_t s_disp_items[] = {
    M_NUM("Contrast", MI_U8, &g_set.contrast, 1, 100, 5, 0, "%", 0),
    M_BOOL("Flip 180" CH_DEG, &g_set.flip),
    M_BOOL("Dim idle", &g_set.dim_idle),
    M_BACK("Back"),
};
static const menu_t m_disp = MENU("Display", s_disp_items, 0, 0);

/* --- System --- */
static const menu_item_t s_sys_items[] = {
    M_NUM("PWM period",  MI_U16, &g_set.pwm_period, 50, 500, 10, 0, "ms", 0),
    M_NUM("ADC delay",   MI_U8,  &g_set.adc_delay, 5, 200, 1, 1, "ms", MF_ACCEL),
    M_NUM("Power limit", MI_U16, &g_set.power_limit, 0, 150, 5, 0, "W", MF_ZOFF),
    M_NUM("Heater R",    MI_U16, &g_set.heater_res, 20, 200, 1, 1, "R", MF_ACCEL),
    M_NUM("Low voltage", MI_U16, &g_set.low_volt, 0, 300, 1, 1, "V", MF_ZOFF | MF_ACCEL),
    M_NUM("TC offset",   MI_I16, &g_set.adc_offset, -500, 500, 1, 0, "", MF_ACCEL),
    M_LIST("Encoder",    &g_set.enc_invert, s_enc_dir),
    M_ACT("Info", act_info),
    M_ACT("Factory reset", act_factory),
    M_BACK("Back"),
};
static const menu_t m_sys = MENU("System", s_sys_items, 0, 0);

/* --- Root --- */
static void act_add_tip(void);

static const menu_item_t s_root_items[] = {
    { .label = "Tip", .type = MI_LIST, .ptr = &g_set.tip_active, .min = 0,
      .max_fn = tip_max, .fmt_fn = tip_name },
    M_SUB("Tip settings", m_tip),
    M_ACT("Add new tip",  act_add_tip),
    M_SUB("Temperature",  m_temp),
    M_SUB("Sleep",        m_sleep),
    M_SUB("Clock",        m_clock),
    M_SUB("Display",      m_disp),
    M_BOOL("Sound",       &g_set.buzzer),
    M_SUB("System",       m_sys),
    M_BACK("Exit"),
};
static const menu_t m_root = MENU("Settings", s_root_items, 0, 0);

/* ------------------------------------------------------------------------- */
static void text_edit_begin(char *s)
{
    size_t n = strlen(s);
    while (n < TIP_NAME_LEN) s[n++] = ' ';
    s[TIP_NAME_LEN] = '\0';
    s_text_pos = 0U;
}

static void text_edit_end(char *s)
{
    int n = TIP_NAME_LEN;
    while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
    if (n == 0) strcpy(s, "TIP");
}

static void act_add_tip(void)
{
    tip_t *t;
    char name[TIP_NAME_LEN + 1];

    if (g_set.tip_count >= TIP_MAX) {
        ui_message("Tip list", "is full");
        return;
    }
    t = &g_set.tips[g_set.tip_count];
    snprintf(name, sizeof(name), "TIP%u", (unsigned)(g_set.tip_count + 1U));
    settings_tip_defaults(t, name);
    g_set.tip_active = g_set.tip_count;
    g_set.tip_count++;
    ui_apply_settings();

    /* open the tip menu and start editing the name */
    menu_push(&m_tip);
    s_stack[s_depth].sel = 0U;
    s_edit = true;
    text_edit_begin(settings_tip()->name);
}

/* ------------------------------------------------------------------------- */
/* Engine                                                                    */
/* ------------------------------------------------------------------------- */
static void menu_push(const menu_t *m)
{
    if (s_depth + 1 >= MENU_DEPTH) return;
    s_depth++;
    s_stack[s_depth].m   = m;
    s_stack[s_depth].sel = 0U;
    s_stack[s_depth].top = 0U;
    s_edit = false;
    if (m->on_enter != 0) m->on_enter();
}

static void menu_pop(void)
{
    s_edit = false;
    if (s_depth > 0) s_depth--;
    else s_exit = true;
}

void menu_open(void)
{
    s_depth = 0;
    s_stack[0].m   = &m_root;
    s_stack[0].sel = 0U;
    s_stack[0].top = 0U;
    s_edit = false;
    s_exit = false;
}

static void edit_number(const menu_item_t *it, int32_t steps)
{
    int32_t v = item_get(it);
    int32_t mx = item_max(it);
    int32_t step = it->step != 0U ? it->step : 1;

    if (it->type == MI_LIST || it->type == MI_BOOL) {
        int32_t n = mx - it->min + 1;
        v = it->min + (((v - it->min + steps) % n) + n) % n;
    } else {
        v += steps * step;
        /* keep values on the step grid */
        if (step > 1) v = (v / step) * step;
        if (v < it->min) v = it->min;
        if (v > mx)      v = mx;
    }
    item_set(it, v);
    ui_apply_settings();
}

static void edit_text(const menu_item_t *it, const input_rot_t *rot, btn_event_t ev)
{
    char *s = (char *)item_ptr(it);
    const int n = (int)(sizeof(s_charset) - 1U);

    if (rot->steps != 0) {
        const char *p = strchr(s_charset, s[s_text_pos]);
        int idx = (p != 0 && s[s_text_pos] != '\0') ? (int)(p - s_charset) : 0;
        idx = ((idx + rot->steps) % n + n) % n;
        s[s_text_pos] = s_charset[idx];
    }
    if (ev == BTN_CLICK || ev == BTN_DOUBLE) {
        if (++s_text_pos >= TIP_NAME_LEN) ev = BTN_LONG;
    }
    if (ev == BTN_LONG) {
        text_edit_end(s);
        s_edit = false;
    }
}

bool menu_input(const input_rot_t *rot, btn_event_t ev)
{
    level_t *lv = &s_stack[s_depth];
    const menu_item_t *it = &lv->m->items[lv->sel];
    int32_t sel;

    if (s_edit) {
        if (it->type == MI_TEXT) {
            edit_text(it, rot, ev);
        } else {
            int32_t steps = (it->flags & MF_ACCEL) ? rot->accel : rot->steps;
            if (steps != 0) edit_number(it, steps);
            if (ev != BTN_NONE) s_edit = false;
        }
        return true;
    }

    if (rot->steps != 0) {
        sel = (int32_t)lv->sel + rot->steps;
        if (sel < 0) sel = 0;
        if (sel >= lv->m->count) sel = lv->m->count - 1;
        lv->sel = (uint8_t)sel;
        if (lv->sel < lv->top) lv->top = lv->sel;
        if (lv->sel >= lv->top + MENU_ROWS) lv->top = (uint8_t)(lv->sel - MENU_ROWS + 1U);
    }

    if (ev == BTN_CLICK || ev == BTN_DOUBLE) {
        switch (it->type) {
        case MI_SUB:  menu_push(it->sub); break;
        case MI_ACT:  it->action(); break;
        case MI_BACK: menu_pop(); break;
        case MI_BOOL: edit_number(it, 1); break;
        case MI_TEXT:
            s_edit = true;
            text_edit_begin((char *)item_ptr(it));
            break;
        default:      s_edit = true; break;
        }
    } else if (ev == BTN_LONG) {
        menu_pop();
    }

    if (s_exit) {
        s_exit = false;
        return false;
    }
    return true;
}

void menu_draw(void)
{
    const level_t *lv = &s_stack[s_depth];
    const menu_t *m = lv->m;
    char val[24];
    char pos[8];
    uint8_t row;

    gfx_clear();

    /* title bar */
    gfx_set_mode(GFX_SET);
    gfx_fill(0, 0, OLED_W, 9);
    gfx_set_mode(GFX_CLR);
    gfx_text(2, 1, m->title_fn != 0 ? m->title_fn() : m->title, &font_small);
    snprintf(pos, sizeof(pos), "%u/%u", (unsigned)(lv->sel + 1U), (unsigned)m->count);
    gfx_text_right(OLED_W - 2, 1, pos, &font_small);
    gfx_set_mode(GFX_SET);

    for (row = 0; row < MENU_ROWS && (lv->top + row) < m->count; row++) {
        uint8_t idx = (uint8_t)(lv->top + row);
        const menu_item_t *it = &m->items[idx];
        int y = ROW_Y0 + row * ROW_H + 1;
        int vx;
        bool selected = (idx == lv->sel);

        gfx_text(3, y, it->label, &font_small);
        item_value_str(it, val, sizeof(val));
        vx = gfx_text_right(OLED_W - 5, y, val, &font_small);

        if (selected) {
            gfx_set_mode(GFX_XOR);
            if (s_edit && it->type == MI_TEXT) {
                gfx_fill(0, y - 1, 2, ROW_H);
                gfx_fill(vx + s_text_pos * 6 - 1, y - 1, 7, ROW_H);
            } else if (s_edit) {
                gfx_fill(0, y - 1, 2, ROW_H);
                gfx_fill(vx - 2, y - 1, OLED_W - 5 - vx + 3, ROW_H);
            } else {
                gfx_fill(0, y - 1, OLED_W - 3, ROW_H);
            }
            gfx_set_mode(GFX_SET);
        }
    }

    /* scroll bar */
    if (m->count > MENU_ROWS) {
        int track = OLED_H - ROW_Y0;
        int th = track * MENU_ROWS / m->count;
        int ty = ROW_Y0 + (track - th) * lv->top / (m->count - MENU_ROWS);
        gfx_vline(OLED_W - 1, ROW_Y0, track);
        gfx_fill(OLED_W - 2, ty, 2, th);
    }
}
