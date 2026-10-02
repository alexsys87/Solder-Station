/**
 * @file    settings.c
 * @brief   Settings storage with simple wear levelling.
 *
 * Two 16 KB flash sectors are used as a log. Every save appends a complete
 * settings record (with sequence number and hardware CRC-32) to the active
 * sector. When the sector is full the other one is erased and used next.
 * On boot the valid record with the highest sequence number wins, so a
 * power loss during a save never destroys the previous settings.
 */
#include "settings.h"
#include "flash.h"
#include "heater.h"
#include "sys.h"
#include "lang.h"
#include <string.h>
#include <stddef.h>

#define SETTINGS_MAGIC      0x54313253UL        /* "S21T" */
#define SETTINGS_VERSION    1U
#define REC_SIZE            ((uint32_t)sizeof(settings_t))
#define REC_WORDS           (REC_SIZE / 4U)
#define CRC_WORDS           ((uint32_t)offsetof(settings_t, crc) / 4U)
#define SLOTS_PER_SECTOR    (FLASH_SETTINGS_SECTOR_SIZE / REC_SIZE)
#define SAVE_DELAY_MS       4000U

/* Compile time checks */
typedef char assert_rec_aligned[(sizeof(settings_t) % 4U) == 0U ? 1 : -1];
typedef char assert_crc_aligned[(offsetof(settings_t, crc) % 4U) == 0U ? 1 : -1];
typedef char assert_rec_fits[(sizeof(settings_t) * 4U) <= FLASH_SETTINGS_SECTOR_SIZE ? 1 : -1];

settings_t g_set;

static const uint32_t s_sector_addr[2] = { FLASH_SETTINGS_ADDR_A, FLASH_SETTINGS_ADDR_B };
static const uint32_t s_sector_num[2]  = { FLASH_SETTINGS_SECTOR_A, FLASH_SETTINGS_SECTOR_B };

static int32_t  s_cur_sector = -1;      /* sector with the newest record */
static uint32_t s_next_slot;            /* first free slot in it          */
static const settings_t *s_stored;      /* newest record in flash         */
static uint32_t s_save_at;
static bool     s_loaded;

/* ------------------------------------------------------------------------- */
static uint32_t calc_crc(const settings_t *s)
{
    return sys_crc32(s, CRC_WORDS);
}

static bool record_valid(const settings_t *r)
{
    return r->magic == SETTINGS_MAGIC
        && r->version == SETTINGS_VERSION
        && r->size == REC_SIZE
        && r->crc == calc_crc(r);
}

static bool slot_erased(const settings_t *r)
{
    const uint32_t *w = (const uint32_t *)r;
    uint32_t i;
    for (i = 0; i < REC_WORDS; i++) {
        if (w[i] != 0xFFFFFFFFUL) return false;
    }
    return true;
}

static const settings_t *slot_ptr(uint32_t sector, uint32_t slot)
{
    return (const settings_t *)(s_sector_addr[sector] + slot * REC_SIZE);
}

/* ------------------------------------------------------------------------- */
void settings_reset_cal(tip_t *tip)
{
    static const int16_t ref[CAL_POINTS] = { CAL_T1 - 25, CAL_T2 - 25, CAL_T3 - 25 };
    /* LSB per degree for the default amplifier */
    const float lsb_per_deg = TC_UV_PER_DEG * 1e-6f * TC_AMP_GAIN / 3.3f * 4095.0f;
    uint32_t i;

    for (i = 0; i < CAL_POINTS; i++) {
        tip->cal_dt[i]  = ref[i];
        tip->cal_adc[i] = (uint16_t)((float)ref[i] * lsb_per_deg + 0.5f);
    }
}

void settings_tip_defaults(tip_t *tip, const char *name)
{
    memset(tip, 0, sizeof(*tip));
    strncpy(tip->name, name, TIP_NAME_LEN);
    tip->name[TIP_NAME_LEN] = '\0';
    tip->setpoint = 320U;
    tip->kp = 30U;      /* 0.030 duty / C          */
    tip->ki = 10U;      /* 0.010 duty / (C * s)    */
    tip->kd = 5U;       /* 0.005 duty * s / C      */
    settings_reset_cal(tip);
}

void settings_defaults(void)
{
    uint32_t seq = g_set.seq;

    memset(&g_set, 0, sizeof(g_set));
    g_set.magic      = SETTINGS_MAGIC;
    g_set.version    = SETTINGS_VERSION;
    g_set.size       = (uint16_t)REC_SIZE;
    g_set.seq        = seq;

    g_set.tip_count  = 3U;
    g_set.tip_active = 0U;
    settings_tip_defaults(&g_set.tips[0], "T12-K");
    settings_tip_defaults(&g_set.tips[1], "T12-BC2");
    settings_tip_defaults(&g_set.tips[2], "T12-ILS");

    g_set.start_mode  = START_OFF;
    g_set.temp_step   = 5U;
    g_set.temp_min    = 150U;
    g_set.temp_max    = 450U;
    g_set.boost_add   = 50U;
    g_set.boost_time  = 60U;
    g_set.sleep_temp  = 180U;

    g_set.sleep_time  = 5U;
    g_set.off_time    = 20U;
    g_set.motion_en   = 1U;
    g_set.wake_on_enc = 1U;

    g_set.clock_en    = 1U;
    g_set.clock_24h   = 1U;
    g_set.clock_show  = 5U;
    g_set.set_show    = 5U;

    g_set.contrast    = 70U;
    g_set.flip        = 0U;
    g_set.buzzer      = 1U;
    g_set.enc_invert  = 0U;
    g_set.dim_idle    = 1U;
    g_set.adc_delay   = 25U;    /* 2.5 ms */
    g_set.pwm_period  = 100U;   /* ms     */

    g_set.power_limit = 0U;
    g_set.heater_res  = 80U;    /* 8.0 Ohm (T12) */
    g_set.low_volt    = 0U;
    g_set.adc_offset  = 0;

    g_set.lang        = (uint8_t)DEFAULT_LANGUAGE;
}

/* Clamp values that could have been damaged or are out of range */
static void settings_sanitize(void)
{
    uint32_t i;
    if (g_set.tip_count == 0U || g_set.tip_count > TIP_MAX) g_set.tip_count = 1U;
    if (g_set.tip_active >= g_set.tip_count) g_set.tip_active = 0U;
    if (g_set.temp_min < TEMP_ABS_MIN) g_set.temp_min = TEMP_ABS_MIN;
    if (g_set.temp_max > TEMP_ABS_MAX) g_set.temp_max = TEMP_ABS_MAX;
    if (g_set.temp_min >= g_set.temp_max) g_set.temp_min = TEMP_ABS_MIN;
    if (g_set.pwm_period < 50U || g_set.pwm_period > 500U) g_set.pwm_period = 100U;
    if (g_set.adc_delay < 5U || g_set.adc_delay > 200U) g_set.adc_delay = 25U;
    if (g_set.temp_step == 0U) g_set.temp_step = 1U;
    if (g_set.lang >= (uint8_t)LANG_COUNT) g_set.lang = (uint8_t)LANG_EN;
    for (i = 0; i < g_set.tip_count; i++) {
        tip_t *t = &g_set.tips[i];
        t->name[TIP_NAME_LEN] = '\0';
        if (t->setpoint < g_set.temp_min) t->setpoint = g_set.temp_min;
        if (t->setpoint > g_set.temp_max) t->setpoint = g_set.temp_max;
        if (!(t->cal_adc[0] < t->cal_adc[1] && t->cal_adc[1] < t->cal_adc[2]
              && t->cal_dt[0] < t->cal_dt[1] && t->cal_dt[1] < t->cal_dt[2]
              && t->cal_dt[0] > 0)) {
            settings_reset_cal(t);
        }
    }
}

/* ------------------------------------------------------------------------- */
void settings_init(void)
{
    uint32_t sec;
    uint32_t slot;
    const settings_t *best = NULL;
    int32_t best_sec = -1;

    for (sec = 0; sec < 2U; sec++) {
        for (slot = 0; slot < SLOTS_PER_SECTOR; slot++) {
            const settings_t *r = slot_ptr(sec, slot);
            if (r->magic == 0xFFFFFFFFUL) break;          /* end of log   */
            if (record_valid(r) && (best == NULL || (int32_t)(r->seq - best->seq) > 0)) {
                best = r;
                best_sec = (int32_t)sec;
            }
        }
    }

    if (best != NULL) {
        memcpy(&g_set, best, sizeof(g_set));
        s_stored = best;
        s_cur_sector = best_sec;
        s_next_slot = (uint32_t)(((uint32_t)best - s_sector_addr[best_sec]) / REC_SIZE) + 1U;
        s_loaded = true;
    } else {
        g_set.seq = 0U;
        settings_defaults();
        s_stored = NULL;
        s_cur_sector = -1;
        s_loaded = false;
    }
    settings_sanitize();
}

bool settings_loaded(void)
{
    return s_loaded;
}

tip_t *settings_tip(void)
{
    return &g_set.tips[g_set.tip_active];
}

static bool write_record(uint32_t sector, uint32_t slot)
{
    uint32_t addr = s_sector_addr[sector] + slot * REC_SIZE;
    if (!flash_program(addr, &g_set, REC_WORDS)) return false;
    s_stored = (const settings_t *)addr;
    s_cur_sector = (int32_t)sector;
    s_next_slot = slot + 1U;
    return true;
}

bool settings_save(void)
{
    bool ok = false;
    uint32_t sec;
    uint32_t attempt;

    s_save_at = 0U;
    settings_sanitize();

    /* Nothing changed since the last save? */
    g_set.magic   = SETTINGS_MAGIC;
    g_set.version = SETTINGS_VERSION;
    g_set.size    = (uint16_t)REC_SIZE;
    g_set.crc     = calc_crc(&g_set);
    if (s_stored != NULL && memcmp(&g_set, s_stored, sizeof(g_set)) == 0) {
        return true;
    }

    g_set.seq++;
    g_set.crc = calc_crc(&g_set);

    /* The CPU stalls during flash operations: keep the heater off */
    heater_force_off(true);

    /* Try the next free slot of the current sector */
    if (s_cur_sector >= 0) {
        sec = (uint32_t)s_cur_sector;
        while (s_next_slot < SLOTS_PER_SECTOR && !ok) {
            if (slot_erased(slot_ptr(sec, s_next_slot))) {
                ok = write_record(sec, s_next_slot);
            }
            if (!ok) s_next_slot++;
        }
    }

    /* Sector full (or nothing stored yet): erase the other one */
    for (attempt = 0; attempt < 2U && !ok; attempt++) {
        sec = (s_cur_sector == 0) ? 1U : 0U;
        if (flash_erase_sector(s_sector_num[sec])) {
            ok = write_record(sec, 0U);
        }
        if (!ok) s_cur_sector = (int32_t)sec;   /* try the other sector next */
    }

    heater_force_off(false);
    if (ok) s_loaded = true;
    return ok;
}

void settings_save_later(void)
{
    s_save_at = sys_ms() + SAVE_DELAY_MS;
    if (s_save_at == 0U) s_save_at = 1U;
}

void settings_task(void)
{
    if (s_save_at != 0U && (int32_t)(sys_ms() - s_save_at) >= 0) {
        settings_save();
    }
}
