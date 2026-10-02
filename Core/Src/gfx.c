/**
 * @file    gfx.c
 * @brief   Drawing into oled_fb (page format, 1 bit per pixel).
 */
#include "gfx.h"
#include "oled.h"
#include <string.h>

static gfx_mode_t s_mode = GFX_SET;

void gfx_clear(void)
{
    memset(oled_fb, 0, sizeof(oled_fb));
}

void gfx_set_mode(gfx_mode_t mode)
{
    s_mode = mode;
}

void gfx_pixel(int x, int y)
{
    uint8_t *p;
    uint8_t bit;

    if ((unsigned)x >= OLED_W || (unsigned)y >= OLED_H) return;
    p = &oled_fb[(y >> 3) * OLED_W + x];
    bit = (uint8_t)(1U << (y & 7));
    switch (s_mode) {
    case GFX_SET: *p |= bit;              break;
    case GFX_CLR: *p &= (uint8_t)~bit;    break;
    default:      *p ^= bit;              break;
    }
}

void gfx_hline(int x, int y, int w)
{
    while (w-- > 0) gfx_pixel(x++, y);
}

void gfx_vline(int x, int y, int h)
{
    while (h-- > 0) gfx_pixel(x, y++);
}

void gfx_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    gfx_hline(x, y, w);
    gfx_hline(x, y + h - 1, w);
    if (h > 2) {
        gfx_vline(x, y + 1, h - 2);
        gfx_vline(x + w - 1, y + 1, h - 2);
    }
}

void gfx_fill(int x, int y, int w, int h)
{
    int i;
    for (i = 0; i < h; i++) gfx_hline(x, y + i, w);
}

void gfx_circle(int xc, int yc, int r)
{
    int x = r;
    int y = 0;
    int err = 1 - r;

    while (x >= y) {
        gfx_pixel(xc + x, yc + y); gfx_pixel(xc - x, yc + y);
        gfx_pixel(xc + x, yc - y); gfx_pixel(xc - x, yc - y);
        if (x != y) {
            gfx_pixel(xc + y, yc + x); gfx_pixel(xc - y, yc + x);
            gfx_pixel(xc + y, yc - x); gfx_pixel(xc - y, yc - x);
        }
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* ------------------------------------------------------------------------- */
static int glyph_index(char c, const font_t *f)
{
    uint8_t i;
    if (f->map != 0) {
        for (i = 0; i < f->count; i++) {
            if (f->map[i] == c) return i;
        }
        return -1;
    }
    /* unsigned arithmetic: first + count may be 0x100 */
    if ((unsigned)(uint8_t)c < f->first || (unsigned)(uint8_t)c >= (unsigned)f->first + f->count) return -1;
    return (int)((uint8_t)c - f->first);
}

/* Find a glyph in the font or in the fonts chained to it. Unknown codes
 * fall back to the space of the first font. Returns the font holding it. */
static const font_t *glyph_find(char c, const font_t *f, int *idx)
{
    const font_t *p;
    for (p = f; p != 0; p = p->next) {
        *idx = glyph_index(c, p);
        if (*idx >= 0) return p;
    }
    *idx = glyph_index(' ', f);
    return *idx >= 0 ? f : 0;
}

static int glyph_width(int idx, const font_t *f)
{
    return f->widths != 0 ? f->widths[idx] : f->fixed_w;
}

static const uint8_t *glyph_data(int idx, const font_t *f)
{
    uint32_t bytes_per_col = (uint32_t)(f->height + 7U) / 8U;
    if (f->offsets != 0) return &f->data[f->offsets[idx]];
    return &f->data[(uint32_t)idx * f->fixed_w * bytes_per_col];
}

int gfx_char(int x, int y, char c, const font_t *f)
{
    int idx;
    int w;
    int col;
    int row;
    int pages = (f->height + 7) / 8;
    const uint8_t *d;
    const font_t *g = glyph_find(c, f, &idx);

    if (g == 0) return x;
    w = glyph_width(idx, g);
    d = glyph_data(idx, g);

    for (col = 0; col < w; col++) {
        for (row = 0; row < f->height; row++) {
            if (d[col * pages + (row >> 3)] & (1U << (row & 7))) {
                gfx_pixel(x + col, y + row);
            }
        }
    }
    return x + w + f->spacing;
}

int gfx_text(int x, int y, const char *s, const font_t *f)
{
    while (*s) {
        x = gfx_char(x, y, *s++, f);
    }
    return x;
}

int gfx_text_width(const char *s, const font_t *f)
{
    int w = 0;
    int idx;
    const font_t *g;
    while (*s) {
        g = glyph_find(*s++, f, &idx);
        if (g != 0) w += glyph_width(idx, g) + f->spacing;
    }
    return w > 0 ? w - f->spacing : 0;
}

int gfx_text_center(int y, const char *s, const font_t *f)
{
    return gfx_text((OLED_W - gfx_text_width(s, f)) / 2, y, s, f);
}

int gfx_text_right(int xr, int y, const char *s, const font_t *f)
{
    int x = xr - gfx_text_width(s, f);
    gfx_text(x, y, s, f);
    return x;
}

/* ------------------------------------------------------------------------- */
int gfx_text2x(int x, int y, const char *s)
{
    const font_t *f = &font_small;
    const font_t *g;
    int idx;
    int col;
    int row;
    const uint8_t *d;

    while (*s) {
        g = glyph_find(*s++, f, &idx);
        if (g == 0) continue;
        d = glyph_data(idx, g);
        for (col = 0; col < f->fixed_w; col++) {
            for (row = 0; row < 8; row++) {
                if (d[col] & (1U << row)) {
                    gfx_fill(x + col * 2, y + row * 2, 2, 2);
                }
            }
        }
        x += (f->fixed_w + f->spacing) * 2;
    }
    return x;
}

int gfx_text2x_width(const char *s)
{
    int n = (int)strlen(s);
    return n > 0 ? n * (font_small.fixed_w + font_small.spacing) * 2 - 2 : 0;
}
