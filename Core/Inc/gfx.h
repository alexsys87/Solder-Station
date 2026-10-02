/**
 * @file    gfx.h
 * @brief   Minimal drawing primitives on the OLED frame buffer.
 */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include <stdbool.h>
#include "fonts.h"

typedef enum {
    GFX_SET = 0,        /* pixels on                   */
    GFX_CLR,            /* pixels off                  */
    GFX_XOR             /* invert pixels               */
} gfx_mode_t;

void gfx_clear(void);
void gfx_set_mode(gfx_mode_t mode);
void gfx_pixel(int x, int y);
void gfx_hline(int x, int y, int w);
void gfx_vline(int x, int y, int h);
void gfx_rect(int x, int y, int w, int h);          /* outline */
void gfx_fill(int x, int y, int w, int h);          /* filled  */
void gfx_circle(int xc, int yc, int r);

int  gfx_char(int x, int y, char c, const font_t *f);
int  gfx_text(int x, int y, const char *s, const font_t *f);
int  gfx_text_width(const char *s, const font_t *f);
int  gfx_text_center(int y, const char *s, const font_t *f);
int  gfx_text_right(int xr, int y, const char *s, const font_t *f);

/* Same as gfx_text, the small font scaled x2 */
int  gfx_text2x(int x, int y, const char *s);
int  gfx_text2x_width(const char *s);

#endif /* GFX_H */
