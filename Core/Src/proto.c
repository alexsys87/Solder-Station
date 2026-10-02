/**
 * @file    proto.c
 * @brief   Line based ASCII protocol for the PC application.
 *
 * Request:   COMMAND [arguments]\n           (case insensitive)
 * Response:  zero or more data lines "= ..." followed by "OK" or
 *            "ERR <code> <text>".
 * Events:    asynchronous lines starting with "!":
 *              !S key=value ...   status stream (STREAM <ms>)
 *              !C                 settings were changed on the device
 *
 * The same protocol runs on USB CDC and on USART1, every port has its own
 * line buffer and stream settings, answers go to the port that asked.
 */
#include "proto.h"
#include "config.h"
#include "board.h"
#include "sys.h"
#include "settings.h"
#include "iron.h"
#include "rtc.h"
#include "ui.h"
#include "buzzer.h"
#include "usb_cdc.h"
#include "uart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>

#define PROTO_LINE_MAX  128
#define OUT_MAX         200
#define MAX_TOKENS      16
#define OUT_TIMEOUT_MS  200U
#define CHANGE_POLL_MS  500U

#define BOOT_MAGIC      0xB007DF00UL
#define SYSMEM_ADDR     0x1FFF0000UL

/* Error codes */
#define E_UNKNOWN       1
#define E_ARG           2
#define E_RANGE         3
#define E_STATE         4
#define E_FLASH         5

/* ------------------------------------------------------------------------- */
/* Ports                                                                     */
/* ------------------------------------------------------------------------- */
typedef struct {
    uint32_t (*write)(const void *data, uint32_t len);
    uint32_t (*read)(void *data, uint32_t max);
    bool     (*ready)(void);
    char     line[PROTO_LINE_MAX];
    uint16_t len;
    bool     overflow;
    uint16_t stream_ms;
    uint32_t stream_last;
    uint32_t stream_cycle;
} port_t;

#if USE_USB
static bool usb_ready(void) { return usb_cdc_configured(); }
#endif
#if USE_UART
static bool uart_ready(void) { return true; }
#endif

static port_t s_ports[] = {
#if USE_USB
    { usb_cdc_write, usb_cdc_read, usb_ready, {0}, 0, false, 0, 0, 0 },
#endif
#if USE_UART
    { uart_write, uart_read, uart_ready, {0}, 0, false, UART_STREAM_DEFAULT_MS, 0, 0 },
#endif
};
#define PORT_COUNT  (sizeof(s_ports) / sizeof(s_ports[0]))

static port_t   *s_cur;
static uint32_t  s_set_crc;             /* settings fingerprint      */
static uint32_t  s_change_poll;
static uint32_t  s_pending_reset;       /* 0 or time of reset        */
static bool      s_pending_boot;

/* Command line split into tokens */
static char      s_orig[PROTO_LINE_MAX];      /* untouched copy             */
static char      s_work[PROTO_LINE_MAX];
static char     *s_tok[MAX_TOKENS];
static uint8_t   s_ntok;

/* ------------------------------------------------------------------------- */
/* Output                                                                    */
/* ------------------------------------------------------------------------- */
static void out_raw(const char *s, uint32_t n)
{
    uint32_t start = sys_ms();
    while (n != 0U) {
        uint32_t w = s_cur->write(s, n);
        s += w;
        n -= w;
        if (n != 0U) {
            if (!s_cur->ready() || (sys_ms() - start) > OUT_TIMEOUT_MS) break;
            __WFI();                    /* wait for the buffer to drain */
        }
    }
}

static void outf(const char *fmt, ...)
{
    char buf[OUT_MAX];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf) - 2U, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(buf) - 3) n = (int)sizeof(buf) - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';
    out_raw(buf, (uint32_t)n);
}

static void ok(void)
{
    outf("OK");
}

static void err(int code, const char *text)
{
    outf("ERR %d %s", code, text);
}

/* "-12.3" from a float, one decimal, no float printf needed */
static const char *f1(char *buf, float v)
{
    int x = (int)(v < 0.0f ? v * 10.0f - 0.5f : v * 10.0f + 0.5f);
    unsigned a = (unsigned)(x < 0 ? -x : x);
    snprintf(buf, 12, "%s%u.%u", x < 0 ? "-" : "", (a / 10U) % 100000U, a % 10U);
    return buf;
}

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */
static bool eq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return false;
    }
    return *a == *b;
}

static bool to_int(const char *s, int32_t *v)
{
    char *end;
    long x;
    if (s == 0 || *s == '\0') return false;
    x = strtol(s, &end, 10);
    if (*end != '\0') return false;
    *v = (int32_t)x;
    return true;
}

static const char *arg(uint8_t i)
{
    return (i < s_ntok) ? s_tok[i] : 0;
}

/* Rest of the original line starting at token i (keeps spaces) */
static const char *rest(uint8_t i)
{
    if (i >= s_ntok) return "";
    return &s_orig[s_tok[i] - s_work];
}

static void settings_changed(void)
{
    ui_apply_settings();
    settings_save_later();
}

static uint32_t settings_fingerprint(void)
{
    const uint32_t start = (uint32_t)offsetof(settings_t, tip_count);
    const uint32_t end = (uint32_t)offsetof(settings_t, crc);
    return sys_crc32((const uint8_t *)&g_set + start, (end - start) / 4U);
}

/* Name characters allowed by the on-device editor */
static void set_tip_name(tip_t *t, const char *name)
{
    static const char allowed[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.+/#";
    uint32_t i = 0;

    while (*name == ' ') name++;
    while (*name != '\0' && i < TIP_NAME_LEN) {
        char c = *name++;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (strchr(allowed, c) == 0 || c == '\0') c = '-';
        t->name[i++] = c;
    }
    while (i > 0U && t->name[i - 1U] == ' ') i--;
    t->name[i] = '\0';
    if (i == 0U) strcpy(t->name, "TIP");
}

/* ------------------------------------------------------------------------- */
/* Parameters (system settings)                                              */
/* ------------------------------------------------------------------------- */
typedef enum { PT_U8 = 0, PT_U16, PT_I16 } ptype_t;

typedef struct {
    const char *key;
    void       *ptr;
    uint8_t     type;
    int16_t     min;
    int16_t     max;
} param_t;

static const param_t s_params[] = {
    { "temp_min",    &g_set.temp_min,    PT_U16, TEMP_ABS_MIN, TEMP_ABS_MAX },
    { "temp_max",    &g_set.temp_max,    PT_U16, TEMP_ABS_MIN, TEMP_ABS_MAX },
    { "temp_step",   &g_set.temp_step,   PT_U8,  1,   25  },
    { "boost_add",   &g_set.boost_add,   PT_U16, 10,  150 },
    { "boost_time",  &g_set.boost_time,  PT_U16, 10,  600 },
    { "sleep_temp",  &g_set.sleep_temp,  PT_U16, TEMP_ABS_MIN, 300 },
    { "sleep_time",  &g_set.sleep_time,  PT_U8,  0,   60  },
    { "off_time",    &g_set.off_time,    PT_U8,  0,   120 },
    { "motion_en",   &g_set.motion_en,   PT_U8,  0,   1   },
    { "wake_on_enc", &g_set.wake_on_enc, PT_U8,  0,   1   },
    { "start_mode",  &g_set.start_mode,  PT_U8,  0,   1   },
    { "clock_en",    &g_set.clock_en,    PT_U8,  0,   1   },
    { "clock_24h",   &g_set.clock_24h,   PT_U8,  0,   1   },
    { "clock_show",  &g_set.clock_show,  PT_U8,  1,   60  },
    { "set_show",    &g_set.set_show,    PT_U8,  1,   60  },
    { "contrast",    &g_set.contrast,    PT_U8,  1,   100 },
    { "flip",        &g_set.flip,        PT_U8,  0,   1   },
    { "dim_idle",    &g_set.dim_idle,    PT_U8,  0,   1   },
    { "buzzer",      &g_set.buzzer,      PT_U8,  0,   1   },
    { "enc_invert",  &g_set.enc_invert,  PT_U8,  0,   1   },
    { "pwm_period",  &g_set.pwm_period,  PT_U16, 50,  500 },
    { "adc_delay",   &g_set.adc_delay,   PT_U8,  5,   200 },
    { "power_limit", &g_set.power_limit, PT_U16, 0,   150 },
    { "heater_res",  &g_set.heater_res,  PT_U16, 20,  200 },
    { "low_volt",    &g_set.low_volt,    PT_U16, 0,   300 },
    { "adc_offset",  &g_set.adc_offset,  PT_I16, -500, 500 },
};
#define PARAM_COUNT (sizeof(s_params) / sizeof(s_params[0]))

static int32_t param_get(const param_t *p)
{
    switch (p->type) {
    case PT_U16: return *(const uint16_t *)p->ptr;
    case PT_I16: return *(const int16_t *)p->ptr;
    default:     return *(const uint8_t *)p->ptr;
    }
}

static void param_set(const param_t *p, int32_t v)
{
    switch (p->type) {
    case PT_U16: *(uint16_t *)p->ptr = (uint16_t)v; break;
    case PT_I16: *(int16_t *)p->ptr  = (int16_t)v;  break;
    default:     *(uint8_t *)p->ptr  = (uint8_t)v;  break;
    }
}

static const param_t *param_find(const char *key)
{
    uint32_t i;
    for (i = 0; i < PARAM_COUNT; i++) {
        if (eq(s_params[i].key, key)) return &s_params[i];
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Commands                                                                  */
/* ------------------------------------------------------------------------- */
static const char *mode_name(iron_mode_t m)
{
    switch (m) {
    case IRON_RUN:   return "RUN";
    case IRON_BOOST: return "BOOST";
    case IRON_SLEEP: return "SLEEP";
    case IRON_CAL:   return "CAL";
    default:         return "OFF";
    }
}

static void status_line(char prefix_bang)
{
    const iron_status_t *st = iron_status();
    char a[12], b[12], c[12], d[12], e[12], f[12];

    outf("%s mode=%s auto=%u set=%u tgt=%u tip=%s raw=%d duty=%s pwr=%s vin=%s cj=%s mcu=%s "
         "err=%u tipn=%u boost=%lu stable=%u cal=%u",
         prefix_bang ? "!S" : "=",
         mode_name(iron_mode()), iron_auto_off() ? 1U : 0U,
         (unsigned)settings_tip()->setpoint, (unsigned)iron_target(),
         f1(a, st->tip_disp), (int)(st->tip_raw + 0.5f), f1(b, st->duty * 100.0f),
         f1(c, st->power_w), f1(d, st->vin), f1(e, st->cj_c), f1(f, st->chip_c),
         (unsigned)iron_errors(), (unsigned)g_set.tip_active,
         (unsigned long)iron_boost_left_s(), iron_stable(500U) ? 1U : 0U,
         ui_cal_active() ? 1U : 0U);
}

static void cmd_help(void)
{
    outf("= PING INFO STATUS STREAM <ms> HELP");
    outf("= PARAMS GET [key] SET <key> <value> SAVE DEFAULTS");
    outf("= MODE OFF|RUN|BOOST|SLEEP TEMP <C> CLEAR BEEP");
    outf("= TIPS TIP SEL|ADD|DEL|NAME|SET|CALRESET ...");
    outf("= CAL [START|POINT <C>|ABORT] TIME [YYYY-MM-DD HH:MM:SS]");
    outf("= RESET DFU");
    ok();
}

static void cmd_info(void)
{
    static const char hex[] = "0123456789ABCDEF";
    const uint8_t *uid = (const uint8_t *)UID_BASE;
    char id[25];
    uint32_t i;

    for (i = 0; i < 12U; i++) {
        id[i * 2U]      = hex[uid[11U - i] >> 4];
        id[i * 2U + 1U] = hex[uid[11U - i] & 0x0FU];
    }
    id[24] = '\0';
    outf("= fw=%s oled=%s tipmax=%u namelen=%u uid=%s hse=%u lse=%u saves=%lu",
         FW_VERSION_STR,
#if (OLED_CONTROLLER == OLED_SH1106)
         "SH1106",
#else
         "SSD1306",
#endif
         (unsigned)TIP_MAX, (unsigned)TIP_NAME_LEN, id,
         sys_hse_ok() ? 1U : 0U, rtc_lse_ok() ? 1U : 0U, (unsigned long)g_set.seq);
    ok();
}

static void cmd_params(void)
{
    uint32_t i;
    for (i = 0; i < PARAM_COUNT; i++) {
        outf("= %s %d %d %ld", s_params[i].key, s_params[i].min, s_params[i].max,
             (long)param_get(&s_params[i]));
    }
    ok();
}

static void cmd_get(void)
{
    const param_t *p;
    uint32_t i;

    if (s_ntok < 2U) {
        for (i = 0; i < PARAM_COUNT; i++) {
            outf("= %s %ld", s_params[i].key, (long)param_get(&s_params[i]));
        }
        ok();
        return;
    }
    p = param_find(arg(1));
    if (p == 0) { err(E_ARG, "unknown key"); return; }
    outf("= %s %ld", p->key, (long)param_get(p));
    ok();
}

static void cmd_set(void)
{
    const param_t *p;
    int32_t v;

    if (s_ntok < 3U || !to_int(arg(2), &v)) { err(E_ARG, "usage: SET key value"); return; }
    p = param_find(arg(1));
    if (p == 0) { err(E_ARG, "unknown key"); return; }
    if (v < p->min || v > p->max) { err(E_RANGE, "out of range"); return; }
    param_set(p, v);
    settings_changed();
    ok();
}

static void cmd_mode(void)
{
    const char *m = arg(1);
    if (m == 0) { outf("= %s", mode_name(iron_mode())); ok(); return; }
    if (ui_cal_active()) { err(E_STATE, "calibration running"); return; }
    if      (eq(m, "OFF"))   iron_set_mode(IRON_OFF);
    else if (eq(m, "RUN"))   iron_set_mode(IRON_RUN);
    else if (eq(m, "BOOST")) iron_set_mode(IRON_BOOST);
    else if (eq(m, "SLEEP")) iron_set_mode(IRON_SLEEP);
    else { err(E_ARG, "OFF|RUN|BOOST|SLEEP"); return; }
    ok();
}

static void cmd_temp(void)
{
    int32_t v;
    if (s_ntok < 2U) { outf("= %u", (unsigned)settings_tip()->setpoint); ok(); return; }
    if (!to_int(arg(1), &v)) { err(E_ARG, "number expected"); return; }
    if (v < g_set.temp_min || v > g_set.temp_max) { err(E_RANGE, "out of range"); return; }
    settings_tip()->setpoint = (uint16_t)v;
    settings_save_later();
    ui_show_setpoint();
    ok();
}

static void cmd_tips(void)
{
    uint32_t i;
    outf("= ACTIVE %u %u %u", (unsigned)g_set.tip_active, (unsigned)g_set.tip_count, (unsigned)TIP_MAX);
    for (i = 0; i < g_set.tip_count; i++) {
        const tip_t *t = &g_set.tips[i];
        outf("= TIP %lu %u %u %u %u %u %u %u %d %d %d %s", (unsigned long)i,
             (unsigned)t->setpoint, (unsigned)t->kp, (unsigned)t->ki, (unsigned)t->kd,
             (unsigned)t->cal_adc[0], (unsigned)t->cal_adc[1], (unsigned)t->cal_adc[2],
             (int)t->cal_dt[0], (int)t->cal_dt[1], (int)t->cal_dt[2], t->name);
    }
    ok();
}

static bool tip_index(uint8_t tok, uint32_t *idx)
{
    int32_t v;
    if (!to_int(arg(tok), &v) || v < 0 || v >= (int32_t)g_set.tip_count) return false;
    *idx = (uint32_t)v;
    return true;
}

static void cmd_tip(void)
{
    const char *sub = arg(1);
    uint32_t i;
    int32_t v;

    if (sub == 0) { err(E_ARG, "TIP SEL|ADD|DEL|NAME|SET|CALRESET"); return; }

    if (eq(sub, "ADD")) {
        if (g_set.tip_count >= TIP_MAX) { err(E_STATE, "tip list is full"); return; }
        i = g_set.tip_count;
        settings_tip_defaults(&g_set.tips[i], "TIP");
        if (s_ntok > 2U) set_tip_name(&g_set.tips[i], rest(2));
        else snprintf(g_set.tips[i].name, sizeof(g_set.tips[i].name), "TIP%lu", (unsigned long)(i + 1U));
        g_set.tip_count++;
        settings_changed();
        outf("= %lu", (unsigned long)i);
        ok();
        return;
    }

    if (!tip_index(2, &i)) { err(E_ARG, "bad tip index"); return; }

    if (eq(sub, "SEL")) {
        g_set.tip_active = (uint8_t)i;
        settings_changed();
        ui_show_setpoint();
    } else if (eq(sub, "DEL")) {
        uint32_t del = i;
        if (g_set.tip_count <= 1U) { err(E_STATE, "cannot delete the last tip"); return; }
        for (; i + 1U < g_set.tip_count; i++) g_set.tips[i] = g_set.tips[i + 1U];
        g_set.tip_count--;
        if (g_set.tip_active > del) g_set.tip_active--;
        if (g_set.tip_active >= g_set.tip_count) g_set.tip_active = (uint8_t)(g_set.tip_count - 1U);
        settings_changed();
    } else if (eq(sub, "NAME")) {
        if (s_ntok < 4U) { err(E_ARG, "name expected"); return; }
        set_tip_name(&g_set.tips[i], rest(3));
        settings_changed();
    } else if (eq(sub, "CALRESET")) {
        settings_reset_cal(&g_set.tips[i]);
        settings_changed();
    } else if (eq(sub, "SET")) {
        tip_t *t = &g_set.tips[i];
        const char *f = arg(3);
        if (f == 0 || !to_int(arg(4), &v)) { err(E_ARG, "usage: TIP SET i field value"); return; }
        if (eq(f, "set")) {
            if (v < g_set.temp_min || v > g_set.temp_max) { err(E_RANGE, "out of range"); return; }
            t->setpoint = (uint16_t)v;
        } else if (eq(f, "kp") || eq(f, "ki") || eq(f, "kd")) {
            if (v < 0 || v > 2000) { err(E_RANGE, "0..2000"); return; }
            if (eq(f, "kp")) t->kp = (uint16_t)v;
            else if (eq(f, "ki")) t->ki = (uint16_t)v;
            else t->kd = (uint16_t)v;
        } else if (eq(f, "adc1") || eq(f, "adc2") || eq(f, "adc3")) {
            if (v < 1 || v > 4095) { err(E_RANGE, "1..4095"); return; }
            t->cal_adc[f[3] - '1'] = (uint16_t)v;
        } else if (eq(f, "dt1") || eq(f, "dt2") || eq(f, "dt3")) {
            if (v < 1 || v > 700) { err(E_RANGE, "1..700"); return; }
            t->cal_dt[f[2] - '1'] = (int16_t)v;
        } else {
            err(E_ARG, "set|kp|ki|kd|adc1..3|dt1..3");
            return;
        }
        settings_changed();
    } else {
        err(E_ARG, "TIP SEL|ADD|DEL|NAME|SET|CALRESET");
        return;
    }
    ok();
}

static void cmd_cal(void)
{
    const char *sub = arg(1);
    ui_cal_state_t cs;
    int32_t v;
    char a[12];

    if (sub == 0) {
        ui_cal_state(&cs);
        outf("= active=%u step=%u target=%u tip=%s stable=%u done=%u ok=%u",
             cs.active ? 1U : 0U, (unsigned)cs.step, (unsigned)cs.target,
             f1(a, iron_status()->tip_disp), cs.stable ? 1U : 0U,
             cs.done ? 1U : 0U, cs.ok ? 1U : 0U);
        ok();
    } else if (eq(sub, "START")) {
        if (iron_errors() != 0U) { err(E_STATE, "iron error"); return; }
        ui_start_calibration();
        ok();
    } else if (eq(sub, "POINT")) {
        if (!to_int(arg(2), &v)) { err(E_ARG, "measured temperature expected"); return; }
        if (!ui_cal_point((int)v)) { err(E_STATE, "calibration is not waiting"); return; }
        ok();
    } else if (eq(sub, "ABORT")) {
        ui_cal_abort();
        ok();
    } else {
        err(E_ARG, "CAL [START|POINT C|ABORT]");
    }
}

static void cmd_time(void)
{
    rtc_time_t t;
    int y, mo, d, h, mi, se;

    if (s_ntok < 2U) {
        rtc_get(&t);
        outf("= 20%02u-%02u-%02u %02u:%02u:%02u", (unsigned)t.year, (unsigned)t.month,
             (unsigned)t.day, (unsigned)t.hour, (unsigned)t.min, (unsigned)t.sec);
        ok();
        return;
    }
    if (sscanf(rest(1), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6
        || y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1
        || d > rtc_days_in_month((uint8_t)mo, (uint8_t)(y - 2000))
        || h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 59) {
        err(E_ARG, "YYYY-MM-DD HH:MM:SS");
        return;
    }
    t.year = (uint8_t)(y - 2000); t.month = (uint8_t)mo; t.day = (uint8_t)d;
    t.hour = (uint8_t)h; t.min = (uint8_t)mi; t.sec = (uint8_t)se; t.wday = 0U;
    rtc_set(&t);
    ok();
}

static void execute(void)
{
    const char *c;
    int32_t v;
    char *p;

    /* tokenize */
    strcpy(s_orig, s_cur->line);
    strcpy(s_work, s_cur->line);
    s_ntok = 0;
    p = s_work;
    while (*p != '\0' && s_ntok < MAX_TOKENS) {
        while (*p == ' ' || *p == '\t') *p++ = '\0';
        if (*p == '\0') break;
        s_tok[s_ntok++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') p++;
    }
    if (s_ntok == 0U) return;
    c = s_tok[0];

    if      (eq(c, "PING"))     { outf("= T12STATION fw=%s", FW_VERSION_STR); ok(); }
    else if (eq(c, "HELP"))     cmd_help();
    else if (eq(c, "INFO"))     cmd_info();
    else if (eq(c, "STATUS"))   { status_line(0); ok(); }
    else if (eq(c, "STREAM")) {
        if (!to_int(arg(1), &v) || v < 0) { err(E_ARG, "STREAM <ms>, 0 = off"); return; }
        if (v != 0 && v < 50) v = 50;
        if (v > 5000) v = 5000;
        s_cur->stream_ms = (uint16_t)v;
        ok();
    }
    else if (eq(c, "PARAMS"))   cmd_params();
    else if (eq(c, "GET"))      cmd_get();
    else if (eq(c, "SET"))      cmd_set();
    else if (eq(c, "SAVE"))     { if (settings_save()) ok(); else err(E_FLASH, "flash write failed"); }
    else if (eq(c, "DEFAULTS")) {
        if (ui_cal_active()) { err(E_STATE, "calibration running"); return; }
        settings_defaults();
        settings_changed();
        ok();
    }
    else if (eq(c, "MODE"))     cmd_mode();
    else if (eq(c, "TEMP"))     cmd_temp();
    else if (eq(c, "CLEAR"))    { iron_clear_errors(); ok(); }
    else if (eq(c, "BEEP"))     { buzzer_pattern(3U, 80U, 80U); ok(); }
    else if (eq(c, "TIPS"))     cmd_tips();
    else if (eq(c, "TIP"))      cmd_tip();
    else if (eq(c, "CAL"))      cmd_cal();
    else if (eq(c, "TIME"))     cmd_time();
    else if (eq(c, "RESET"))    { ok(); s_pending_reset = sys_ms(); s_pending_boot = false; }
    else if (eq(c, "DFU"))      { ok(); s_pending_reset = sys_ms(); s_pending_boot = true; }
    else err(E_UNKNOWN, "unknown command, try HELP");
}

/* ------------------------------------------------------------------------- */
/* Bootloader / reset                                                        */
/* ------------------------------------------------------------------------- */
static void do_reset(bool bootloader)
{
    if (bootloader) {
        RCC->APB1ENR |= RCC_APB1ENR_PWREN;
        PWR->CR |= PWR_CR_DBP;
        RTC->BKP1R = BOOT_MAGIC;
    }
    NVIC_SystemReset();
}

void proto_check_bootloader(void)
{
    void (*boot)(void);
    uint32_t sp;

    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    (void)RCC->APB1ENR;
    if (RTC->BKP1R != BOOT_MAGIC) return;

    PWR->CR |= PWR_CR_DBP;
    RTC->BKP1R = 0U;

    /* Map the system memory at 0 and start the ROM bootloader (USB DFU,
     * USART1 PA9/PA10). The heater pin floats -> its pull-down keeps it off. */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;
    SYSCFG->MEMRMP = 1U;
    sp = *(const volatile uint32_t *)SYSMEM_ADDR;
    boot = (void (*)(void))(*(const volatile uint32_t *)(SYSMEM_ADDR + 4U));
    __set_MSP(sp);
    boot();
}

/* ------------------------------------------------------------------------- */
void proto_init(void)
{
    s_set_crc = settings_fingerprint();
    s_change_poll = sys_ms();
}

static void poll_port(port_t *port)
{
    char buf[32];
    uint32_t n;
    uint32_t i;

    s_cur = port;
    while ((n = port->read(buf, sizeof(buf))) != 0U) {
        for (i = 0; i < n; i++) {
            char ch = buf[i];
            if (ch == '\r' || ch == '\n') {
                if (port->overflow) {
                    err(E_ARG, "line too long");
                } else if (port->len != 0U) {
                    port->line[port->len] = '\0';
                    execute();
                    s_set_crc = settings_fingerprint();   /* no "!C" echo */
                }
                port->len = 0U;
                port->overflow = false;
            } else if (ch == 0x08 || ch == 0x7F) {
                if (port->len != 0U) port->len--;
            } else if ((uint8_t)ch >= 0x20U) {
                if (port->len < PROTO_LINE_MAX - 1U) port->line[port->len++] = ch;
                else port->overflow = true;
            }
        }
    }
}

void proto_task(void)
{
    uint32_t now = sys_ms();
    uint32_t i;
    uint32_t crc;
    const iron_status_t *st = iron_status();

    for (i = 0; i < PORT_COUNT; i++) {
        poll_port(&s_ports[i]);
    }

    /* status stream: once per control cycle at most */
    for (i = 0; i < PORT_COUNT; i++) {
        port_t *p = &s_ports[i];
        if (p->stream_ms != 0U && p->ready() && st->cycles != p->stream_cycle
            && (now - p->stream_last) >= p->stream_ms) {
            p->stream_last = now;
            p->stream_cycle = st->cycles;
            s_cur = p;
            status_line(1);
        }
    }

    /* settings changed on the device (menu, encoder): notify the PC */
    if ((now - s_change_poll) >= CHANGE_POLL_MS) {
        s_change_poll = now;
        crc = settings_fingerprint();
        if (crc != s_set_crc) {
            s_set_crc = crc;
            for (i = 0; i < PORT_COUNT; i++) {
                if (s_ports[i].stream_ms != 0U && s_ports[i].ready()) {
                    s_cur = &s_ports[i];
                    outf("!C");
                }
            }
        }
    }

    /* delayed reset so the "OK" reaches the host */
    if (s_pending_reset != 0U && (now - s_pending_reset) > 100U) {
        do_reset(s_pending_boot);
    }
}
