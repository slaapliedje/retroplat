#include "platform.h"

/* No real drawing surface on the host backend -- glyph draw is a no-op so
   engine/layout code can link and run headless under test. */

wp_status plat_draw_text(plat_gc *gc, plat_font *f, i32 x, i32 y,
                          const u8 *utf8, u32 len, u8 r, u8 g, u8 b)
{
    WP_UNUSED(gc); WP_UNUSED(f); WP_UNUSED(x); WP_UNUSED(y);
    WP_UNUSED(utf8); WP_UNUSED(len);
    WP_UNUSED(r); WP_UNUSED(g); WP_UNUSED(b);
    return WP_OK;
}

wp_status plat_fill_rect(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                          u8 r, u8 g, u8 b)
{
    WP_UNUSED(gc); WP_UNUSED(x); WP_UNUSED(y); WP_UNUSED(w); WP_UNUSED(h);
    WP_UNUSED(r); WP_UNUSED(g); WP_UNUSED(b);
    return WP_OK;
}

void plat_gc_flush(plat_gc *gc)
{
    WP_UNUSED(gc);
}
