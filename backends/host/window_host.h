/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_WINDOW_HOST_H
#define WP_WINDOW_HOST_H

#include "types.h"

/* Host-only: the next plat_dialog_choice answers `index` (once) instead
   of its default, so a test can take a dialog's other branches. -1 is
   the default again. Never part of the portable surface. */
void window_host_next_choice(i32 index);

#endif /* WP_WINDOW_HOST_H */
