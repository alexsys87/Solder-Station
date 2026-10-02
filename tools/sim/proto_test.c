/* Host test of the text protocol: feeds commands into proto.c, prints replies */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include "settings.h"
#include "iron.h"
#include "rtc.h"
#include "ui.h"
#include "proto.h"
#include "board.h"

RCC_TypeDef sim_rcc; PWR_TypeDef sim_pwr; RTC_TypeDef sim_rtc; SYSCFG_TypeDef sim_syscfg;
uint8_t sim_uid[12] = {1,2,3,4,5,6,7,8,9,10,11,12};
void sim_reset(void) { printf("<<MCU RESET, BKP1R=%08X>>\n", (unsigned)sim_rtc.BKP1R); }

static uint32_t t_ms = 1000;
uint32_t sys_ms(void) { return t_ms; }
bool sys_hse_ok(void) { return true; }
uint32_t sys_irq_save(void) { return 0; }
void sys_irq_restore(uint32_t s) { (void)s; }
uint32_t sys_crc32(const void *d, uint32_t n) { const uint32_t *p = d; uint32_t c = 0; while (n--) c = c * 31 + *p++; return c; }
bool flash_erase_sector(uint32_t s) { memset((void *)(uintptr_t)(0x08004000UL + (s - 1U) * 0x4000UL), 0xFF, 0x4000); return true; }
bool flash_program(uint32_t a, const void *d, uint32_t n) { memcpy((void *)(uintptr_t)a, d, n * 4U); return true; }
void heater_force_off(bool o) { (void)o; }
void buzzer_pattern(uint8_t c, uint16_t a, uint16_t b) { printf("<<beep x%u>>\n", c); (void)a; (void)b; }
bool rtc_lse_ok(void) { return true; }
static rtc_time_t s_rtc = { 14, 37, 10, 2, 10, 26, 5 };
void rtc_get(rtc_time_t *t) { *t = s_rtc; }
void rtc_set(const rtc_time_t *t) { s_rtc = *t; }
uint8_t rtc_days_in_month(uint8_t m, uint8_t y) { static const uint8_t d[]={31,28,31,30,31,30,31,31,30,31,30,31}; return (m==2 && y%4==0)?29:d[m-1]; }

static iron_status_t s_st = { 318.4f, 318.2f, 2210.0f, 27.5f, 31.0f, 24.1f, 0.12f, 8.7f, 100 };
static iron_mode_t s_mode = IRON_OFF;
void iron_set_mode(iron_mode_t m) { s_mode = m; }
iron_mode_t iron_mode(void) { return s_mode; }
bool iron_auto_off(void) { return false; }
uint16_t iron_target(void) { return s_mode == IRON_OFF ? 0 : settings_tip()->setpoint; }
uint32_t iron_boost_left_s(void) { return 0; }
uint8_t iron_errors(void) { return 0; }
void iron_clear_errors(void) { }
const iron_status_t *iron_status(void) { return &s_st; }
bool iron_stable(uint32_t ms) { (void)ms; return true; }

static bool s_cal; static int s_cal_step;
void ui_apply_settings(void) { }
void ui_show_setpoint(void) { }
void ui_start_calibration(void) { s_cal = true; s_cal_step = 0; s_mode = IRON_CAL; }
bool ui_cal_active(void) { return s_cal; }
bool ui_cal_point(int m) { if (!s_cal || s_cal_step >= 3) return false; (void)m; s_cal_step++; return true; }
void ui_cal_abort(void) { s_cal = false; s_mode = IRON_OFF; }
void ui_cal_state(ui_cal_state_t *cs) { cs->active = s_cal; cs->step = (uint8_t)s_cal_step; cs->target = 250 + 100 * s_cal_step; cs->stable = 1; cs->done = s_cal_step >= 3; cs->ok = cs->done; }

/* ports */
static const char *s_in; static size_t s_in_len;
uint32_t usb_cdc_read(void *d, uint32_t max) { uint32_t n = s_in_len < max ? (uint32_t)s_in_len : max; memcpy(d, s_in, n); s_in += n; s_in_len -= n; return n; }
uint32_t usb_cdc_write(const void *d, uint32_t n) { fwrite(d, 1, n, stdout); return n; }
bool usb_cdc_configured(void) { return true; }
uint32_t uart_read(void *d, uint32_t m) { (void)d; (void)m; return 0; }
uint32_t uart_write(const void *d, uint32_t n) { (void)d; return n; }

static void cmd(const char *c) { printf("> %s", c); s_in = c; s_in_len = strlen(c); proto_task(); }

int main(void)
{
    void *fl = mmap((void *)0x08004000UL, 0x8000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    memset(fl, 0xFF, 0x8000);
    settings_init();
    proto_init();
    cmd("PING\n"); cmd("info\n"); cmd("STATUS\n"); cmd("GET temp_max\n");
    cmd("SET temp_max 470\n"); cmd("SET temp_max 999\n"); cmd("SET nope 1\n");
    cmd("PARAMS\n");
    cmd("TIPS\n"); cmd("TIP ADD my tip 2\n"); cmd("TIP NAME 3 hakko d24\n"); cmd("TIP SET 3 kp 45\n");
    cmd("TIP SEL 3\n"); cmd("TIP DEL 0\n"); cmd("TIPS\n");
    cmd("TEMP 350\n"); cmd("MODE RUN\n"); cmd("STATUS\n");
    cmd("TIME\n"); cmd("TIME 2026-02-30 10:00:00\n"); cmd("TIME 2026-10-02 18:05:00\n"); cmd("TIME\n");
    cmd("CAL START\n"); cmd("CAL\n"); cmd("CAL POINT 252\n"); cmd("CAL\n");  cmd("CAL ABORT\n");
    cmd("STREAM 100\n"); t_ms += 200; s_st.cycles++; cmd("");
    g_set.contrast = 33; t_ms += 600; cmd("");
    cmd("SAVE\n"); cmd("BEEP\n"); cmd("FOO\n"); cmd("DFU\n"); t_ms += 200; cmd("");
    return 0;
}
