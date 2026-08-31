#include <stddef.h>
#include "platform.h"
#include "utf8.h"

/* Fake fixed-cell monospace metrics: no real font system, deterministic
   round numbers so layout code can hand-verify wrap points against them
   (per T0.5 / later T3.1). plat_font is an opaque handle per platform.h;
   this struct is private to this backend. */
typedef struct {
    i32 size_twips;
    u16 style_flags;
} host_font;

wp_status plat_font_open(const u8 *utf8_font_name, u32 name_len,
                          i32 size_twips, u16 style_flags, plat_font **out)
{
    host_font *f;

    WP_UNUSED(utf8_font_name);
    WP_UNUSED(name_len);

    f = (host_font *)mem_alloc((u32)sizeof(host_font));
    if (f == NULL) return WP_NOMEM;
    f->size_twips = size_twips;
    f->style_flags = style_flags;
    *out = (plat_font *)f;
    return WP_OK;
}

void plat_font_close(plat_font *f)
{
    mem_free(f);
}

wp_status plat_font_metrics_get(plat_font *f, plat_font_metrics *out)
{
    host_font *hf = (host_font *)f;

    if (hf == NULL || out == NULL) return WP_ERR;
    out->ascent = hf->size_twips;
    out->descent = hf->size_twips / 4;
    out->leading = 0;
    return WP_OK;
}

/* Host-only introspection, mirroring mem_host's leak counters: lets
   tests prove "minimal recompute" (T3.3) by counting how many
   plat_measure_text calls a relayout/pagination pass actually makes,
   rather than just trusting that skipped work was skipped. Never called
   from engine/ or services/ code. */
static u32 g_measure_call_count = 0;

wp_status plat_measure_text(plat_font *f, const u8 *utf8, u32 len,
                             i32 *width_twips_out)
{
    host_font *hf = (host_font *)f;
    i32 cell_width;
    u32 cp_count;

    if (hf == NULL || width_twips_out == NULL) return WP_ERR;

    g_measure_call_count++;

    cell_width = hf->size_twips / 2;
    cp_count = utf8_cp_count(utf8, len);
    *width_twips_out = cell_width * (i32)cp_count;
    return WP_OK;
}

u32 metrics_host_measure_call_count(void)
{
    return g_measure_call_count;
}

void metrics_host_reset_measure_count(void)
{
    g_measure_call_count = 0;
}
