#ifndef RP_GTK_METRICS_INTERNAL_H
#define RP_GTK_METRICS_INTERNAL_H

#include <cairo.h>
#include <pango/pangocairo.h>
#include "types.h"

/* Private to backends/gtk -- shared between metrics_gtk.c (which owns the
   twips<->points conversion and the Pango font description) and draw_gtk.c
   (the plat_gc implementation, drawing through a Cairo context). The same
   shared-internal-header pattern metrics_atari_internal.h,
   metrics_amiga_internal.h and metrics_mac_internal.h each use.
   
   Pango works in points scaled by PANGO_SCALE, and a twip is 1/20 point,
   so the conversion is exact in both directions with no rounding. That is
   the same happy accident QuickDraw's 72-dpi convention gives the Mac
   backend, and for the same underlying reason: both are built on the
   printer's point rather than on a screen pixel. */

#define GTK_TWIPS_PER_POINT 20

typedef struct {
    PangoFontDescription *desc;
    i32                   size_twips;
    /* Measuring needs a layout, and a layout needs a context. One
       per-font context is kept rather than one per measurement:
       plat_measure_text is called once per text run during layout, and
       creating a PangoContext each time is the difference between a
       redraw that is free and one that is visible. */
    PangoContext         *ctx;
} gtk_font;

/* plat_gc for this backend: the Cairo context of whatever surface is being
   drawn to -- a widget's during a draw signal, or an image surface when
   rendering off-screen. Callers construct one directly, the way the Amiga
   and Mac backends have their own callers build an amiga_gc/mac_gc,
   because the shell already owns its window. */
typedef struct {
    cairo_t *cr;
} gtk_gc;

#endif /* RP_GTK_METRICS_INTERNAL_H */
