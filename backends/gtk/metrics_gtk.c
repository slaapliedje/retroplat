#include <string.h>
#include "platform.h"
#include "metrics_gtk_internal.h"
#include "mem_host.h"

/* A twip is 1/20 point and Pango counts in points x PANGO_SCALE, so both
   conversions are exact. */
static int twips_to_pango(i32 twips)
{
    return (int)((twips * PANGO_SCALE) / GTK_TWIPS_PER_POINT);
}

wp_status plat_font_open(const u8 *utf8_font_name, u32 name_len,
                          i32 size_twips, u16 style_flags, plat_font **out)
{
    gtk_font *f;
    char name[128];
    u32 n;
    PangoFontMap *map;

    if (out == NULL) return WP_ERR;

    f = (gtk_font *)mem_alloc((u32)sizeof(gtk_font));
    if (f == NULL) return WP_NOMEM;

    /* An empty or absent name means "whatever this system calls
       monospace", which is what a report of aligned columns wants and
       what every other backend in this library resolves to as well --
       topaz on the Amiga, Monaco on the Mac, the one BIOS cell on DOS. */
    n = name_len;
    if (utf8_font_name == NULL || n == 0) {
        strcpy(name, "Monospace");
    } else {
        if (n > (u32)sizeof(name) - 1) n = (u32)sizeof(name) - 1;
        memcpy(name, utf8_font_name, n);
        name[n] = '\0';
    }

    f->desc = pango_font_description_new();
    if (f->desc == NULL) { mem_free(f); return WP_NOMEM; }
    pango_font_description_set_family(f->desc, name);
    pango_font_description_set_size(f->desc, twips_to_pango(size_twips));
    if (style_flags & PLAT_STYLE_BOLD)
        pango_font_description_set_weight(f->desc, PANGO_WEIGHT_BOLD);
    if (style_flags & PLAT_STYLE_ITALIC)
        pango_font_description_set_style(f->desc, PANGO_STYLE_ITALIC);

    map = pango_cairo_font_map_get_default();
    f->ctx = pango_font_map_create_context(map);
    f->size_twips = size_twips;

    *out = (plat_font *)f;
    return WP_OK;
}

void plat_font_close(plat_font *f)
{
    gtk_font *gf = (gtk_font *)f;
    if (gf == NULL) return;
    if (gf->ctx != NULL)  g_object_unref(gf->ctx);
    if (gf->desc != NULL) pango_font_description_free(gf->desc);
    mem_free(gf);
}

wp_status plat_font_metrics_get(plat_font *f, plat_font_metrics *out)
{
    gtk_font *gf = (gtk_font *)f;
    PangoFontMetrics *m;

    if (gf == NULL || out == NULL) return WP_ERR;
    m = pango_context_get_metrics(gf->ctx, gf->desc, NULL);
    if (m == NULL) return WP_ERR;

    out->ascent  = (i32)((pango_font_metrics_get_ascent(m) * GTK_TWIPS_PER_POINT)
                         / PANGO_SCALE);
    out->descent = (i32)((pango_font_metrics_get_descent(m) * GTK_TWIPS_PER_POINT)
                         / PANGO_SCALE);
    out->leading = 0;   /* Pango folds it into ascent/descent, as the Atari
                           and Amiga backends' own comments record doing */
    pango_font_metrics_unref(m);
    return WP_OK;
}

wp_status plat_measure_text(plat_font *f, const u8 *utf8, u32 len,
                             i32 *width_twips_out)
{
    gtk_font *gf = (gtk_font *)f;
    PangoLayout *layout;
    int w = 0, h = 0;

    if (gf == NULL || utf8 == NULL || width_twips_out == NULL) return WP_ERR;
    layout = pango_layout_new(gf->ctx);
    if (layout == NULL) return WP_NOMEM;
    pango_layout_set_font_description(layout, gf->desc);
    pango_layout_set_text(layout, (const char *)utf8, (int)len);
    pango_layout_get_size(layout, &w, &h);
    g_object_unref(layout);

    *width_twips_out = (i32)((w * GTK_TWIPS_PER_POINT) / PANGO_SCALE);
    return WP_OK;
}

/* Everything here is already UTF-8: GTK, Pango and Cairo all take it
   directly, so unlike the Atari, Amiga, Mac and DOS backends there is no
   code page to fall out of and nothing is ever unmappable. */
wp_status plat_utf8_to_native(const u8 *utf8, u32 len,
                               u8 *out, u32 out_cap, u32 *out_len,
                               u32 *unmapped_count)
{
    u32 n = len;
    if (utf8 == NULL || out == NULL) return WP_ERR;
    if (n > out_cap) n = out_cap;
    memcpy(out, utf8, n);
    if (out_len != NULL) *out_len = n;
    if (unmapped_count != NULL) *unmapped_count = 0;
    return WP_OK;
}

wp_status plat_native_to_utf8(const u8 *native, u32 len,
                               u8 *out, u32 out_cap, u32 *out_len)
{
    u32 n = len;
    if (native == NULL || out == NULL) return WP_ERR;
    if (n > out_cap) n = out_cap;
    memcpy(out, native, n);
    if (out_len != NULL) *out_len = n;
    return WP_OK;
}
