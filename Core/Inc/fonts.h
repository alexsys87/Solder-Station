/**
 * @file    fonts.h
 * @brief   Bitmap fonts. Glyphs are stored column by column, each column is
 *          ceil(height / 8) bytes, least significant bit = top pixel
 *          (the native page format of SH1106 / SSD1306).
 */
#ifndef FONTS_H
#define FONTS_H

#include <stdint.h>

typedef struct font_s {
    uint8_t         height;     /* glyph height, pixels                       */
    uint8_t         spacing;    /* empty columns after each glyph             */
    uint8_t         first;      /* first code (when map == NULL)              */
    uint8_t         count;      /* number of glyphs                           */
    const char     *map;        /* glyph characters (sparse fonts) or NULL    */
    uint8_t         fixed_w;    /* width when widths == NULL                  */
    const uint8_t  *widths;     /* per glyph width or NULL                    */
    const uint16_t *offsets;    /* per glyph data offset or NULL              */
    const uint8_t  *data;
    const struct font_s *next;  /* searched for codes missing here, or NULL */
} font_t;

/* Special characters of the small font */
#define CH_DEG      "\x7F"      /* degree sign          */
#define CH_RIGHT    "\x80"      /* right triangle       */
#define CH_LEFT     "\x81"      /* left triangle        */
#define CH_UP       "\x82"      /* up arrow             */
#define CH_DOWN     "\x83"      /* down arrow           */
#define CH_BELL     "\x84"      /* bell                 */
#define CH_FLASH    "\x85"      /* heating (lightning)  */

extern const font_t font_small;     /* 5x7, ASCII 0x20..0x85 + font_cyr */
extern const font_t font_cyr;       /* 5x7, Cyrillic 0xC0..0xFF (cp1251) */
extern const font_t font_mid;       /* 16 px digits "0123456789:-. "    */
extern const font_t font_big;       /* 32 px digits "0123456789:- "     */

#endif /* FONTS_H */
