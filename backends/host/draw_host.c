/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stddef.h>
#include "platform.h"
#include "draw_host.h"

/* No real drawing surface on the host backend -- glyph draw only notes
   what it was asked for, so engine/layout code can link and run headless
   under test and a test can see where text went. */

static struct {
    i32  x, y;
    char text[48];
} g_texts[DRAW_HOST_TEXT_MAX];
static u32 g_text_count;

wp_status plat_draw_text(plat_gc *gc, plat_font *f, i32 x, i32 y,
                          const u8 *utf8, u32 len, u8 r, u8 g, u8 b)
{
    WP_UNUSED(gc); WP_UNUSED(f);
    WP_UNUSED(r); WP_UNUSED(g); WP_UNUSED(b);
    if (g_text_count < DRAW_HOST_TEXT_MAX) {
        u32 n = len < 47u ? len : 47u, i;
        g_texts[g_text_count].x = x;
        g_texts[g_text_count].y = y;
        for (i = 0; i < n; i++) g_texts[g_text_count].text[i] = (char)utf8[i];
        g_texts[g_text_count].text[n] = '\0';
    }
    g_text_count++;
    return WP_OK;
}

u32 draw_host_text_count(void)
{
    return g_text_count;
}

const char *draw_host_text_at(u32 index, i32 *x_out, i32 *y_out)
{
    if (index >= g_text_count || index >= DRAW_HOST_TEXT_MAX) return NULL;
    if (x_out != NULL) *x_out = g_texts[index].x;
    if (y_out != NULL) *y_out = g_texts[index].y;
    return g_texts[index].text;
}

void draw_host_reset_text(void)
{
    g_text_count = 0;
}

static u32 g_grey_fills[256];
static u32 g_mono_calls;

u32 draw_host_grey_fills(u8 level)
{
    return g_grey_fills[level];
}

u32 draw_host_mono_calls(void)
{
    return g_mono_calls;
}

void draw_host_reset_fills(void)
{
    u32 i;
    for (i = 0; i < 256; i++) g_grey_fills[i] = 0;
    g_mono_calls = 0;
}

wp_status plat_draw_mono(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                         const u16 *rows, u32 row_words, u8 r, u8 g, u8 b)
{
    g_mono_calls++;
    return plat_mono_by_runs(gc, x, y, w, h, rows, row_words, r, g, b);
}

wp_status plat_fill_rect(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                          u8 r, u8 g, u8 b)
{
    WP_UNUSED(gc); WP_UNUSED(x); WP_UNUSED(y); WP_UNUSED(w); WP_UNUSED(h);
    if (r == g && g == b) g_grey_fills[r]++;
    return WP_OK;
}

void plat_gc_flush(plat_gc *gc)
{
    WP_UNUSED(gc);
}
