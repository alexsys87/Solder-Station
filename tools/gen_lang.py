#!/usr/bin/env python3
"""
Generates Core/Inc/lang.h and Core/Src/lang.c - UI strings in English and
Russian. This table is the only place to edit texts: change it and run

    python3 tools/gen_lang.py

The generated C files are pure ASCII (Russian letters become Windows-1251
"\\xC0"-style escapes), so they compile with any source encoding setting of
IAR or GCC. Special glyphs of the small font are written here as Unicode:
    deg -> 0x7F,  right/left triangle -> 0x80/0x81,  up/down arrow -> 0x82/0x83

Checks: printf conversions must be the same in both languages and a plain
text line must fit the 128 px display (21 characters of the 5x7 font).
Format strings and the real layout are best checked with the screen
simulator (tools/sim).
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(__file__), "..")
OUT_H = os.path.join(ROOT, "Core", "Inc", "lang.h")
OUT_C = os.path.join(ROOT, "Core", "Src", "lang.c")

LANGS = [("LANG_EN", "English"), ("LANG_RU", "Russian")]

# id, English, Russian.  Comments in [] tell where the string is shown.
STRINGS = [
    ("S_NONE",          "",                     ""),
    ("S_LANG_NAME",     "English",              "Русский"),     # language list
    # --- common ---
    ("S_ON",            "On",                   "Вкл"),
    ("S_OFF",           "Off",                  "Выкл"),
    ("S_BACK",          "Back",                 "Назад"),
    ("S_EXIT",          "Exit",                 "Выход"),
    ("S_CANCEL",        "Cancel",               "Отмена"),
    ("S_SAVE",          "Save",                 "Сохранить"),
    ("S_CLICK",         "Click",                "Нажмите"),
    # --- units ---
    ("S_U_DEGC",        "°C",                   "°C"),
    ("S_U_S",           "s",                    "с"),
    ("S_U_MIN",         "min",                  "мин"),
    ("S_U_MS",          "ms",                   "мс"),
    ("S_U_PCT",         "%",                    "%"),
    ("S_U_W",           "W",                    "Вт"),
    ("S_U_OHM",         "R",                    "Ом"),
    ("S_U_V",           "V",                    "В"),
    # --- menu titles ---
    ("S_SETTINGS",      "Settings",             "Настройки"),
    ("S_TIP_TITLE_FMT", "Tip: %s",              "Жало: %s"),
    ("S_TEMPERATURE",   "Temperature",          "Температура"),
    ("S_SLEEP",         "Sleep",                "Сон"),
    ("S_CLOCK",         "Clock",                "Часы"),
    ("S_SET_TIME",      "Set time",             "Задать время"),
    ("S_DISPLAY",       "Display",              "Дисплей"),
    ("S_SYSTEM",        "System",               "Система"),
    # --- root menu ---
    ("S_TIP",           "Tip",                  "Жало"),
    ("S_TIP_SETTINGS",  "Tip settings",         "Настройки жала"),
    ("S_ADD_TIP",       "Add new tip",          "Добавить жало"),
    ("S_SOUND",         "Sound",                "Звук"),
    ("S_LANGUAGE",      "Language",             "Язык"),
    # --- tip menu ---
    ("S_NAME",          "Name",                 "Имя"),
    ("S_CALIBRATE",     "Calibrate",            "Калибровка"),
    ("S_PID_KP",        "PID Kp",               "ПИД Kp"),
    ("S_PID_KI",        "PID Ki",               "ПИД Ki"),
    ("S_PID_KD",        "PID Kd",               "ПИД Kd"),
    ("S_RESET_CAL",     "Reset calib.",         "Сброс калибр."),
    ("S_DELETE_TIP",    "Delete tip",           "Удалить жало"),
    # --- temperature menu ---
    ("S_MIN_TEMP",      "Min temp",             "Мин. темп."),
    ("S_MAX_TEMP",      "Max temp",             "Макс. темп."),
    ("S_TEMP_STEP",     "Temp step",            "Шаг темп."),
    ("S_BOOST_TEMP",    "Boost temp",           "Форсаж темп."),
    ("S_BOOST_TIME",    "Boost time",           "Форсаж время"),
    # --- sleep menu ---
    ("S_MOTION",        "Motion sensor",        "Датчик вибр."),
    ("S_SLEEP_AFTER",   "Sleep after",          "Сон через"),
    ("S_SLEEP_TEMP",    "Sleep temp",           "Темп. сна"),
    ("S_OFF_AFTER",     "Off after",            "Выкл. через"),
    ("S_ENC_WAKES",     "Enc. wakes",           "Энкодер будит"),
    ("S_POWER_ON",      "Power on",             "При включ."),
    ("S_HEAT_OPT",      "Heat",                 "Нагрев"),
    # --- clock menu ---
    ("S_SHOW_CLOCK",    "Show clock",           "Показ часов"),
    ("S_FORMAT",        "Format",               "Формат"),
    ("S_12H",           "12h",                  "12ч"),
    ("S_24H",           "24h",                  "24ч"),
    ("S_CLOCK_TIME",    "Clock time",           "Время часов"),
    ("S_SETPT_TIME",    "Setpt time",           "Время уставки"),
    ("S_HOURS",         "Hours",                "Часы"),
    ("S_MINUTES",       "Minutes",              "Минуты"),
    ("S_DAY",           "Day",                  "День"),
    ("S_MONTH",         "Month",                "Месяц"),
    ("S_YEAR",          "Year",                 "Год"),
    # --- display menu ---
    ("S_CONTRAST",      "Contrast",             "Контраст"),
    ("S_FLIP",          "Flip 180°",            "Поворот 180°"),
    ("S_DIM_IDLE",      "Dim idle",             "Затемнение"),
    # --- system menu ---
    ("S_PWM_PERIOD",    "PWM period",           "Период ШИМ"),
    ("S_ADC_DELAY",     "ADC delay",            "Задержка АЦП"),
    ("S_POWER_LIMIT",   "Power limit",          "Огр. мощности"),
    ("S_HEATER_R",      "Heater R",             "R нагревателя"),
    ("S_LOW_VOLT",      "Low voltage",          "Мин. напряж."),
    ("S_TC_OFFSET",     "TC offset",            "Смещение ТП"),
    ("S_ENCODER",       "Encoder",              "Энкодер"),
    ("S_NORMAL",        "Normal",               "Прямое"),
    ("S_REVERSE",       "Reverse",              "Обратное"),
    ("S_INFO",          "Info",                 "Инфо"),
    ("S_FACTORY",       "Factory reset",        "Сброс настроек"),
    # --- menu messages ---
    ("S_RESET_CAL_Q",   "Reset calibration?",   "Сбросить калибровку?"),
    ("S_CANNOT_DELETE", "Cannot delete",        "Нельзя удалить"),
    ("S_LAST_TIP",      "the last tip",         "последнее жало"),
    ("S_DELETE_TIP_Q",  "Delete this tip?",     "Удалить это жало?"),
    ("S_TIME_SAVED",    "Time saved",           "Время сохранено"),
    ("S_DEFAULTS",      "Defaults",             "Настройки"),
    ("S_LOADED",        "loaded",               "сброшены"),
    ("S_FACTORY_Q",     "Factory reset?",       "Сбросить настройки?"),
    ("S_TIP_LIST",      "Tip list",             "Список жал"),
    ("S_IS_FULL",       "is full",              "заполнен"),
    # --- main screen ---
    ("S_ST_OFF",        "OFF",                  "ВЫКЛ"),
    ("S_ST_AUTO_OFF",   "AUTO OFF",             "АВТОВЫКЛ"),
    ("S_ST_READY",      "READY",                "ГОТОВ"),
    ("S_ST_HEAT",       "HEAT",                 "НАГРЕВ"),
    ("S_ST_BOOST_FMT",  "BOOST %lus",           "ФОРСАЖ %luс"),
    ("S_ST_SLEEP",      "SLEEP zZ",             "СОН zZ"),
    ("S_ST_CAL",        "CAL",                  "КАЛ"),
    ("S_SET",           "SET",                  "УСТ"),
    ("S_NO_TIP",        "No tip",               "Нет жала"),
    ("S_HOT",           "HOT!",                 "ГОРЯЧО!"),
    ("S_TIP_FMT",       "Tip %d°C",             "Жало %d°C"),
    ("S_SET_FMT",       "Set %u°C",             "Уст %u°C"),
    ("S_SLEEP_FMT",     "Sleep %u°C",           "Сон %u°C"),
    ("S_V_FMT",         "%sV",                  "%sВ"),
    ("S_W_V_FMT",       "%dW %sV",              "%dВт %sВ"),
    ("S_OFF_SET_FMT",   "OFF  Set %u°",         "ВЫКЛ  Уст %u°"),
    # --- week days (clock screen) ---
    ("S_MON",           "Mon",                  "Пн"),
    ("S_TUE",           "Tue",                  "Вт"),
    ("S_WED",           "Wed",                  "Ср"),
    ("S_THU",           "Thu",                  "Чт"),
    ("S_FRI",           "Fri",                  "Пт"),
    ("S_SAT",           "Sat",                  "Сб"),
    ("S_SUN",           "Sun",                  "Вс"),
    # --- errors (title in the double size font: max 10 characters) ---
    ("S_ERR_OVERHEAT",  "OVERHEAT",             "ПЕРЕГРЕВ"),
    ("S_ERR_RUNAWAY",   "RUNAWAY",              "ОТКАЗ"),
    ("S_ERR_NO_TIP",    "NO TIP",               "НЕТ ЖАЛА"),
    ("S_ERR_LOW_VOLT",  "LOW VOLT",             "НИЗКОЕ U"),
    ("S_HINT_RESET",    "Click to reset",       "Нажмите для сброса"),
    ("S_HINT_HEATER",   "Check heater. Click",  "Проверьте нагрев"),
    ("S_HINT_TIP",      "Insert tip / handle",  "Вставьте жало"),
    ("S_HINT_VIN_FMT",  "Vin %sV",              "Uвх %sВ"),
    # --- calibration ---
    ("S_CALIBRATION",   "Calibration",          "Калибровка"),
    ("S_CAL_STEP_FMT",  "Calibration %u/%u",    "Калибровка %u/%u"),
    ("S_DONE",          "DONE",                 "ГОТОВО"),
    ("S_FAILED",        "FAILED",               "ОШИБКА"),
    ("S_CAL_SAVED",     "Saved to tip profile", "Сохранено в профиль"),
    ("S_CAL_BAD",       "Values not monotonic", "Значения не растут"),
    ("S_CLICK_EXIT",    "Click to exit",        "Нажмите для выхода"),
    ("S_TARGET_FMT",    "Target %u°C",          "Цель %u°C"),
    ("S_HEATING",       "Heating...",           "Нагрев..."),
    ("S_SETTLING",      "Settling..",           "Ожидание.."),
    ("S_CLICK_SKIP",    "Click:skip",           "Клик:далее"),
    ("S_REAL_TEMP",     "Real tip temp:",       "Реальная темп.:"),
    ("S_CLICK_OK",      "Click=OK",             "Клик=OK"),
    ("S_HOLD_ABORT",    "Hold=abort",           "Удерж=отмена"),
    # --- info screen ---
    ("S_INFO_VIN_FMT",  "Vin %sV  %sW",         "Uвх %sВ  %sВт"),
    ("S_INFO_TIP_FMT",  "Tip %d°C  ADC %d",     "Жало %d°C  АЦП %d"),
    ("S_INFO_CJ_FMT",   "CJ %s  MCU %s°C",      "ХС %s  МК %s°C"),
    ("S_INFO_DUTY_FMT", "Duty %d.%d%%  Err %02X", "ШИМ %d.%d%%  Ош %02X"),
    ("S_INFO_SAVES_FMT", "Saves %lu  Moves %lu", "Записей %lu  Дв %lu"),
    # --- confirm / splash ---
    ("S_CONFIRM",       "Confirm",              "Подтверждение"),
    ("S_CLICK_YES",     "Click = Yes",          "Клик  = Да "),
    ("S_HOLD_NO",       "Hold  = No ",          "Удерж = Нет"),
    ("S_STATION",       "SOLDERING STATION",    "ПАЯЛЬНАЯ СТАНЦИЯ"),
    ("S_DEFAULTS_LOADED", "defaults loaded",    "настройки сброшены"),
]

SPECIAL = {"°": 0x7F, "►": 0x80, "◄": 0x81, "↑": 0x82, "↓": 0x83}
MAX_CHARS = 21
FMT_RE = re.compile(r"%(?:%|[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[diouxXcsp])")


def encode(text):
    """Unicode -> bytes of the display encoding (ASCII + cp1251 Cyrillic)."""
    out = bytearray()
    for ch in text:
        if ch in SPECIAL:
            out.append(SPECIAL[ch])
        elif ch in "Ёё":
            out += ("Е" if ch == "Ё" else "е").encode("cp1251")
        elif ord(ch) < 0x80:
            out.append(ord(ch))
        else:
            b = ch.encode("cp1251")
            if not 0xC0 <= b[0] <= 0xFF:
                raise ValueError(f"no glyph for {ch!r} in {text!r}")
            out += b
    return bytes(out)


def c_literal(data):
    """C string literal; hex escapes are split when a hex digit follows."""
    s = '"'
    hex_open = False
    for b in data:
        c = chr(b)
        if b >= 0x7F or b < 0x20:
            s += f"\\x{b:02X}"
            hex_open = True
            continue
        if hex_open and c in "0123456789abcdefABCDEF":
            s += '" "'
        hex_open = False
        s += "\\" + c if c in '"\\' else c
    return s + '"'


def check():
    ok = True
    ids = [s[0] for s in STRINGS]
    if len(ids) != len(set(ids)):
        print("duplicate ids"); ok = False
    for sid, en, ru in STRINGS:
        if FMT_RE.findall(en) != FMT_RE.findall(ru):
            print(f"{sid}: printf conversions differ: {en!r} / {ru!r}"); ok = False
        for t in (en, ru):
            if not FMT_RE.search(t) and len(t) > MAX_CHARS:
                print(f"{sid}: too long ({len(t)} > {MAX_CHARS}): {t!r}"); ok = False
            encode(t)
    return ok


def main():
    if not check():
        sys.exit(1)

    h = ["/**",
         " * @file    lang.h",
         " * @brief   UI strings, generated by tools/gen_lang.py - do not edit.",
         " *          Texts are edited in the table of tools/gen_lang.py.",
         " */",
         "#ifndef LANG_H",
         "#define LANG_H",
         "",
         "#include <stdint.h>",
         "",
         "typedef enum {"]
    h += [f"    {name}{' = 0' if i == 0 else ''},".ljust(20) + f"/* {title} */"
          for i, (name, title) in enumerate(LANGS)]
    h += ["    LANG_COUNT", "} lang_t;", "", "typedef enum {"]
    h += [f"    {sid}," for sid, _, _ in STRINGS]
    h += ["    S_COUNT", "} str_id_t;", "",
          "/* Text of a string in the selected language (g_set.lang) */",
          "const char *tr(str_id_t id);",
          "",
          "/* Name of a language written in that language */",
          "const char *lang_name(uint8_t lang);",
          "",
          "#endif /* LANG_H */", ""]

    c = ["/**",
         " * @file    lang.c",
         " * @brief   UI strings, generated by tools/gen_lang.py - do not edit.",
         " *          Non-ASCII characters are Windows-1251 codes (see fonts_cyr.c).",
         " */",
         '#include "lang.h"',
         '#include "settings.h"',
         ""]
    for li, (lname, title) in enumerate(LANGS):
        var = "s_" + lname[5:].lower()
        c.append(f"static const char * const {var}[S_COUNT] = {{")
        for sid, *texts in STRINGS:
            lit = c_literal(encode(texts[li]))
            c.append(f"    {lit},".ljust(36) + f" /* {sid} */")
        c += ["};", ""]
    c.append("static const char * const * const s_tables[LANG_COUNT] = {")
    c.append("    " + ", ".join("s_" + l[5:].lower() for l, _ in LANGS))
    c += ["};", "",
          "const char *tr(str_id_t id)",
          "{",
          "    uint8_t lang = g_set.lang < (uint8_t)LANG_COUNT ? g_set.lang : (uint8_t)LANG_EN;",
          "    if ((unsigned)id >= (unsigned)S_COUNT) return \"\";",
          "    return s_tables[lang][id];",
          "}",
          "",
          "const char *lang_name(uint8_t lang)",
          "{",
          "    if (lang >= (uint8_t)LANG_COUNT) lang = (uint8_t)LANG_EN;",
          "    return s_tables[lang][S_LANG_NAME];",
          "}",
          ""]

    for path, lines in ((OUT_H, h), (OUT_C, c)):
        with open(path, "w", newline="\n") as f:
            f.write("\n".join(lines))
        print("written", os.path.abspath(path))


if __name__ == "__main__":
    main()
