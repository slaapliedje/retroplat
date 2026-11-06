/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_WINDOW_HOST_H
#define WP_WINDOW_HOST_H

#include "types.h"

/* Host-only: the next plat_dialog_choice answers `index` (once) instead
   of its default, so a test can take a dialog's other branches. -1 is
   the default again. Never part of the portable surface. */
void window_host_next_choice(i32 index);

/* Host-only: the title the last plat_dialog_choice was shown ("" before
   the first) -- which, for a one-button message (platform/common's
   wp_action_tell), is the message itself. */
const char *window_host_last_title(void);

#endif /* WP_WINDOW_HOST_H */
