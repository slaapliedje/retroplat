#include "platform.h"
#include "metrics_gtk_internal.h"
#include "mem_host.h"

/* No plat_window implementation, for the same reason the Amiga and Mac
   backends have none: a GTK application owns its own GtkWindow, its menu
   bar and its drawing area through direct calls, and builds a gtk_gc from
   the Cairo context the draw signal hands it. A placeholder is kept only so
   a caller that wants the generic shape has something non-NULL to hold. */
typedef struct { int dummy; } gtk_window;

wp_status plat_window_create(const u8 *title_utf8, u32 title_len,
                              i32 w, i32 h, plat_window **out)
{
    gtk_window *win;
    WP_UNUSED(title_utf8); WP_UNUSED(title_len); WP_UNUSED(w); WP_UNUSED(h);
    if (out == NULL) return WP_ERR;
    win = (gtk_window *)mem_alloc((u32)sizeof(gtk_window));
    if (win == NULL) return WP_NOMEM;
    *out = (plat_window *)win;
    return WP_OK;
}

void plat_window_destroy(plat_window *w) { mem_free(w); }

wp_status plat_window_get_gc(plat_window *w, plat_gc **out)
{
    WP_UNUSED(w);
    if (out == NULL) return WP_ERR;
    /* There is no context without a draw signal to get one from. */
    *out = NULL;
    return WP_ERR;
}
