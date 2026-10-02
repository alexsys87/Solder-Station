/**
 * Host-side UI simulator: compiles ui.c, menu.c, gfx.c, fonts and settings.c
 * on a PC with stubbed hardware and writes screenshots (PNG via PBM->python).
 *
 *   make -C tools/sim && ./tools/sim/sim out_dir
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include "oled.h"
#include "input.h"
#include "iron.h"
#include "rtc.h"
#include "settings.h"
#include "ui.h"

/* ---- stubs ---- */
uint8_t oled_fb[OLED_PAGES * OLED_W];
static uint32_t t_ms = 10;
uint32_t sys_ms(void) { return t_ms; }
void sys_delay_ms(uint32_t ms) { t_ms += ms; }
bool sys_hse_ok(void) { return true; }
uint32_t sys_irq_save(void) { return 0; }
void sys_irq_restore(uint32_t s) { (void)s; }
uint32_t sys_crc32(const void *d, uint32_t n) { const uint32_t *p = d; uint32_t c = 0; while (n--) c = c * 31 + *p++; return c; }
bool oled_busy(void) { return false; }
bool oled_flush(void) { return true; }
void oled_wait(void) { }
void oled_set_contrast(uint8_t v) { (void)v; }
void oled_set_flip(bool f) { (void)f; }
void buzzer_enable(bool e) { (void)e; }
void buzzer_pattern(uint8_t c, uint16_t a, uint16_t b) { (void)c; (void)a; (void)b; }
uint32_t motion_count(void) { return 42; }
bool rtc_lse_ok(void) { return true; }
static rtc_time_t s_rtc = { 14, 37, 10, 2, 10, 26, 5 };
void rtc_get(rtc_time_t *t) { *t = s_rtc; }
void rtc_set(const rtc_time_t *t) { s_rtc = *t; }
uint8_t rtc_days_in_month(uint8_t m, uint8_t y) { (void)m; (void)y; return 31; }
void heater_force_off(bool off) { (void)off; }
/* the settings flash area is emulated by memory mapped at its real address */
bool flash_erase_sector(uint32_t s) { memset((void *)(uintptr_t)(0x08004000UL + (s - 1U) * 0x4000UL), 0xFF, 0x4000); return true; }
bool flash_program(uint32_t a, const void *d, uint32_t n) { memcpy((void *)(uintptr_t)a, d, n * 4U); return true; }
void input_set_invert(bool i) { (void)i; }

/* input injection */
static input_rot_t s_rot;
static btn_event_t s_ev;
void input_take_rotation(input_rot_t *r) { *r = s_rot; memset(&s_rot, 0, sizeof(s_rot)); }
btn_event_t input_take_button(void) { btn_event_t e = s_ev; s_ev = BTN_NONE; return e; }
uint32_t input_last_activity(void) { return t_ms; }

/* iron model */
static iron_status_t s_st = { 318.4f, 318.2f, 2210.0f, 27.5f, 31.0f, 24.1f, 0.12f, 8.7f, 100 };
static iron_mode_t s_mode = IRON_RUN;
static uint8_t s_err;
static uint16_t s_cal_t;
void iron_apply_settings(void) { }
void iron_set_mode(iron_mode_t m) { s_mode = m; }
iron_mode_t iron_mode(void) { return s_mode; }
bool iron_auto_off(void) { return false; }
uint16_t iron_target(void) { return s_mode == IRON_SLEEP ? g_set.sleep_temp : s_mode == IRON_CAL ? s_cal_t : settings_tip()->setpoint; }
uint32_t iron_boost_left_s(void) { return 47; }
uint8_t iron_errors(void) { return s_err; }
void iron_clear_errors(void) { s_err = 0; }
const iron_status_t *iron_status(void) { return &s_st; }
bool iron_stable(uint32_t ms) { (void)ms; return false; }
void iron_set_cal_target(uint16_t t) { s_cal_t = t; }

/* ---- output ---- */
static const char *s_dir;
static void shot(const char *name)
{
    char path[256];
    FILE *f;
    int x, y;
    snprintf(path, sizeof(path), "%s/%s.pbm", s_dir, name);
    f = fopen(path, "w");
    fprintf(f, "P1\n%d %d\n", OLED_W, OLED_H);
    for (y = 0; y < OLED_H; y++) {
        for (x = 0; x < OLED_W; x++) {
            fputc((oled_fb[(y >> 3) * OLED_W + x] >> (y & 7)) & 1 ? '1' : '0', f);
            fputc(' ', f);
        }
        fputc('\n', f);
    }
    fclose(f);
}

static void run(uint32_t ms) { uint32_t end = t_ms + ms; while (t_ms < end) { t_ms += 10; ui_task(); } }
static void rot(int n) { s_rot.steps = (int16_t)n; s_rot.accel = (int16_t)n; run(50); }
static void click(void) { s_ev = BTN_CLICK; run(50); }
static void hold(void) { s_ev = BTN_LONG; run(50); }

int main(int argc, char **argv)
{
    void *fl;
    s_dir = argc > 1 ? argv[1] : ".";
    fl = mmap((void *)0x08004000UL, 0x8000, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (fl == MAP_FAILED) { perror("mmap"); return 1; }
    memset(fl, 0xFF, 0x8000);
    settings_init();
    ui_init();
    shot("00_splash");
    run(1500);
    shot("01_run");
    rot(2); shot("02_set_change");
    run(2500);
    s_mode = IRON_BOOST; run(100); shot("03_boost");
    s_mode = IRON_SLEEP; s_st.tip_disp = 181; s_st.duty = 0.03f; run(100); shot("04_sleep");
    s_mode = IRON_OFF; s_st.tip_disp = 112; s_st.duty = 0; run(100); shot("05_off_setpoint");
    run(5200); shot("06_off_clock");
    s_mode = IRON_RUN; s_err = IRON_ERR_NO_TIP; run(100); shot("07_err_notip");
    s_err = IRON_ERR_RUNAWAY; run(100); shot("08_err_runaway");
    s_err = 0; s_st.tip_disp = 320;
    s_rot.pressed_steps = 1; run(100); shot("09_tip_select");
    run(2000);
    hold(); shot("10_menu_root");
    rot(1); click(); shot("11_menu_tip");
    rot(2); click(); rot(5); shot("12_menu_edit_kp");
    click(); hold();
    rot(2); click(); shot("13_menu_temp"); hold();
    rot(1); click(); shot("14_menu_sleep"); hold();
    rot(1); click(); shot("15_menu_clock");
    rot(4); click(); shot("16_menu_settime"); hold(); hold();
    rot(1); click(); shot("17_menu_display"); hold();
    rot(2); click(); shot("18_menu_system");
    rot(7); click(); shot("19_info"); click(); hold();
    rot(-7); click(); click(); rot(3); shot("20_tip_name_edit");
    click(); rot(-2); shot("21_tip_name_edit2");
    hold();
    rot(1); click(); s_cal_t = 250; s_st.tip_disp = 238; run(100); shot("22_cal_heat");
    click(); rot(4); shot("23_cal_input");
    return 0;
}
