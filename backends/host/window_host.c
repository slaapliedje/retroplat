#include <stddef.h>
#include "platform.h"

/* No real GUI at this stage -- enough surface to link and let a test
   harness exercise the seam. plat_window/plat_gc are opaque handles per
   platform.h; this struct is private to this backend. */
typedef struct {
    i32 w;
    i32 h;
} host_window;

wp_status plat_window_create(const u8 *title_utf8, u32 title_len,
                              i32 w, i32 h, plat_window **out)
{
    host_window *win;

    WP_UNUSED(title_utf8);
    WP_UNUSED(title_len);

    win = (host_window *)mem_alloc((u32)sizeof(host_window));
    if (win == NULL) return WP_NOMEM;
    win->w = w;
    win->h = h;
    *out = (plat_window *)win;
    return WP_OK;
}

void plat_window_destroy(plat_window *w)
{
    mem_free(w);
}

wp_status plat_window_get_gc(plat_window *w, plat_gc **out)
{
    if (w == NULL || out == NULL) return WP_ERR;
    /* No real drawing surface: the window handle itself doubles as a
       non-null opaque gc handle for draw_host.c's no-op functions. */
    *out = (plat_gc *)w;
    return WP_OK;
}

wp_status plat_dialog_message(plat_window *parent, plat_dialog_kind kind,
                               const u8 *utf8_msg, u32 len,
                               plat_dialog_result *out)
{
    WP_UNUSED(parent);
    WP_UNUSED(kind);
    WP_UNUSED(utf8_msg);
    WP_UNUSED(len);
    if (out) *out = PLAT_DR_OK;
    return WP_OK;
}

wp_status plat_dialog_choice(plat_window *parent,
                              const u8 *title_utf8, u32 title_len,
                              const u8 * const *option_labels_utf8,
                              const u32 *option_label_lens,
                              u32 option_count, u32 default_index,
                              wp_bool *out_cancelled, u32 *out_index)
{
    /* No real UI at this stage -- always "picks" the default, never
       cancels. Enough to let platform/common code (and host tests
       exercising it) link and run without a real GUI, same reasoning
       as plat_dialog_message above. */
    WP_UNUSED(parent);
    WP_UNUSED(title_utf8);
    WP_UNUSED(title_len);
    WP_UNUSED(option_labels_utf8);
    WP_UNUSED(option_label_lens);
    if (option_count == 0) return WP_RANGE;
    if (out_cancelled) *out_cancelled = WP_FALSE;
    if (out_index) *out_index = (default_index < option_count) ? default_index : 0;
    return WP_OK;
}
