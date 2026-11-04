/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_DRAW_HOST_H
#define WP_DRAW_HOST_H

#include "types.h"

/* Host-only introspection into plat_fill_rect: how many fills of each
   grey (r == g == b) have been asked for since the last reset, so a
   test can tell which look a painter chose without a screen to look
   at. Like metrics_host.h, never part of the portable surface. */

u32  draw_host_grey_fills(u8 level);
void draw_host_reset_fills(void);

#endif /* WP_DRAW_HOST_H */
