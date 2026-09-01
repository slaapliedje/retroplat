#include "platform.h"
#include "metrics_gtk_internal.h"

/* Coordinates here are PIXELS, as they are for every other backend in this
   library -- the caller converts from twips with the same ratio it measured
   with. y is the TOP of the line, not the baseline: Pango lays out from the
   top and the caller's row pitch is a line height, so making the caller
   subtract an ascent it would have to ask for separately would be a trap. */
wp_status plat_draw_text(plat_gc *gc, plat_font *f, i32 x, i32 y,
                          const u8 *utf8, u32 len, u8 r, u8 g, u8 b)
{
    gtk_gc   *ggc = (gtk_gc *)gc;
    gtk_font *gf  = (gtk_font *)f;
    PangoLayout *layout;

    if (ggc == NULL || ggc->cr == NULL || gf == NULL || utf8 == NULL)
        return WP_ERR;

    layout = pango_cairo_create_layout(ggc->cr);
    if (layout == NULL) return WP_NOMEM;
    pango_layout_set_font_description(layout, gf->desc);
    pango_layout_set_text(layout, (const char *)utf8, (int)len);

    cairo_save(ggc->cr);
    cairo_set_source_rgb(ggc->cr, r / 255.0, g / 255.0, b / 255.0);
    cairo_move_to(ggc->cr, (double)x, (double)y);
    pango_cairo_show_layout(ggc->cr, layout);
    cairo_restore(ggc->cr);

    g_object_unref(layout);
    return WP_OK;
}

wp_status plat_fill_rect(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                          u8 r, u8 g, u8 b)
{
    gtk_gc *ggc = (gtk_gc *)gc;

    if (ggc == NULL || ggc->cr == NULL) return WP_ERR;
    cairo_save(ggc->cr);
    cairo_set_source_rgb(ggc->cr, r / 255.0, g / 255.0, b / 255.0);
    cairo_rectangle(ggc->cr, (double)x, (double)y, (double)w, (double)h);
    cairo_fill(ggc->cr);
    cairo_restore(ggc->cr);
    return WP_OK;
}

/* Cairo batches into its surface and the surface is presented by whoever
   owns it -- a widget's draw signal, or a caller writing a PNG. Flushing
   the context is the honest amount of work there is to do here. */
void plat_gc_flush(plat_gc *gc)
{
    gtk_gc *ggc = (gtk_gc *)gc;
    if (ggc != NULL && ggc->cr != NULL) cairo_surface_flush(cairo_get_target(ggc->cr));
}
