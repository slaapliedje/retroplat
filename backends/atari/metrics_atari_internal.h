#ifndef WP_ATARI_METRICS_INTERNAL_H
#define WP_ATARI_METRICS_INTERNAL_H

#include <gem.h>
#include "types.h"

/* Private to platform/atari -- shared between metrics_atari.c (owns the
   twips<->pixel conversion and the lazily-opened metrics-only VDI
   workstation), draw_atari.c (the plat_gc implementation, drawing in
   pixels), and app_shell.c (which lays out text in twips via the engine
   and must convert to pixels with the SAME ratio before calling
   plat_draw_text/plat_fill_rect, so line breaks measured by the engine
   land exactly where they're drawn).

   Real device DPI is queried per-axis from v_opnvwk()'s own work_out[3]/
   [4] (pixel width/height in microns -- a standard VDI open-workstation
   output, NOT GDOS-specific; verified empirically via a real Hatari +
   EmuTOS boot with no GDOS driver attached at all, see
   docs/atari-platform.md). X and Y are queried and converted separately
   on purpose: low-res ST pixels are not square (confirmed empirically:
   75 DPI horizontal vs 68 DPI vertical at 320x200), so a single shared
   ratio would distort measurements relative to what's actually drawn.
   ATARI_ASSUMED_DPI is now only a defensive fallback (used per-axis if a
   driver ever reports a 0 or nonsensical pixel size, which shouldn't
   happen on real hardware but must never divide by zero) -- not the
   primary source of truth it was before. Because plat_measure_text and
   the pixel coordinates handed to plat_draw_text still go through the
   SAME per-axis conversion, wrap correctness (T4.2's AC) remains an
   internal-consistency property even though the ratio is now real. */
#define ATARI_ASSUMED_DPI 96

i32 atari_twips_to_px_x(i32 twips);
i32 atari_twips_to_px_y(i32 twips);
i32 atari_px_to_twips_x(i32 px);
i32 atari_px_to_twips_y(i32 px);

/* Real, runtime-queried total color count for the current VDI device
   (2 mono/4 Medium/16 Low -- see docs/atari-platform.md's "Boot
   resolution: Medium, not Low" -- never assumed, same reasoning this
   header already gives for DPI not being hardcoded). M15: needed by
   both do_insert_image (to size the real OFFLOAD_IMAGE request against
   what this screen can actually show) and plat_draw_image (to know how
   many VDI palette registers/bitplanes are real). */
i32 atari_screen_max_colors(void);

/* plat_font's real backing type -- shared here (not private to
   metrics_atari.c) because VDI text attributes are per-VDI-HANDLE
   state, not global: a size/style selected on g_metrics_vdi (via
   plat_measure_text/plat_font_metrics_get, during layout) has zero
   effect on a window's own separate drawing handle's later v_gtext
   calls. draw_atari.c needs the real point_size/style_flags to
   re-select on ITS handle before drawing, not just an opaque pointer
   it can't read. */
typedef struct {
    short point_size;
    u16   style_flags;
} atari_font;

/* Applies point size + effects (bold/italic/underline) to vdi's own
   CURRENT text attributes -- must be called on ANY handle (metrics or
   drawing) before that handle's own vst_point-dependent calls
   (vqt_extent/vqt_fontinfo/v_gtext) will agree with what plat_font_open
   was actually asked for. A real, found-on-real-hardware bug this
   fixes: draw_atari.c's plat_draw_text used to discard its plat_font
   parameter entirely, on the false assumption that whatever the
   layout pass selected on g_metrics_vdi would carry over to the
   window's own vdi handle -- it never does, VDI attributes don't
   cross handles. The mismatch was invisible at ST Low/Medium (close
   enough between the two handles' default vs requested sizes to not
   visibly overlap) but produced real, visible double-struck/ghosted
   text at ST High, where the DPI difference made the gap large. */
void atari_select_font(VdiHdl vdi, const atari_font *f);

/* One-time-per-handle: if a scalable-font GDOS is resident, loads its
   fonts for vdi so atari_select_font's vst_point can hit the exact
   requested size on THIS handle too, instead of silently snapping to
   the built-in bitmap font's coarser sizes the way an unregistered
   handle does -- see ensure_metrics_vdi's identical call for
   g_metrics_vdi. Every real drawing handle (the app's one window vdi,
   reused by paint_document/paint_status_line/paint_help) needs this
   called on it once, the same reason it needs atari_select_font before
   every draw -- a fresh v_opnvwk handle starts with neither. */
void atari_ensure_gdos_fonts(VdiHdl vdi);

/* Asserts the font picked for real body sizes is text-shaped in PHYSICAL
   units on the current screen -- the invariant ST Medium was violating
   (a 12pt document drawn in a cell 4.4x taller than wide). Lives with
   the metrics code rather than in app_shell.c's selfchecks because it
   needs the micron figures and the discovered size table, both private
   to metrics_atari.c. */
wp_bool wp_atari_font_selfcheck(void);

/* plat_gc for this backend: a real window's VDI handle plus the AES
   window handle it belongs to (needed for wind_get_grect-based clipping
   during redraw). Callers (app_shell.c) construct one directly -- there
   is no plat_window_create/plat_window_get_gc implementation for Atari
   yet, since app_shell.c already owns its window through direct AES
   calls (CLAUDE.md: "platform/ owns UI + rendering") and nothing in
   engine/ calls those platform.h functions. */
typedef struct {
    VdiHdl vdi;
    short  window_handle;
} atari_gc;

#endif /* WP_ATARI_METRICS_INTERNAL_H */
