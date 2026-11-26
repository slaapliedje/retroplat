/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef WP_WINDOW_HOST_H
#define WP_WINDOW_HOST_H

#include "types.h"

/* Host-only: the next plat_dialog_choice answers `index` (once) instead
   of its default, so a test can take a dialog's other branches; called
   again before those dialogs run, it queues the answers for the dialogs
   after (up to 16), for a walk through several. -1 is the default for
   that one. Never part of the portable surface. */
void window_host_next_choice(i32 index);
/* For window_host_next_choice: the user picks the backend's own Cancel
   (a choice of two or more with no caller "Cancel" has one). */
#define WINDOW_HOST_CANCEL (-2)

/* Host-only: the title the last plat_dialog_choice was shown ("" before
   the first) -- which, for a one-button message (platform/common's
   wp_action_tell), is the message itself. */
const char *window_host_last_title(void);

/* Host-only, for plat_dialog_form: what the coming forms do. Edits
   queue for the next form shown -- text for a PLAT_FORM_TEXT field
   (index ignored), index for a PLAT_FORM_CHOICE (text ignored) -- and
   it answers OK with them; window_host_form_cancel makes it answer
   Cancel and change nothing instead. window_host_form_next moves on:
   what is queued after it is for the form after that, so a test can
   follow a dialog that opens again. A form nothing was queued for
   answers OK, unchanged. window_host_form_count: how many forms have
   been shown; the last one's title is window_host_last_title's. */
void window_host_form_edit(u32 field, const char *text, i32 index);
void window_host_form_cancel(void);
void window_host_form_next(void);
u32  window_host_form_count(void);
/* The fields of the last form shown, copied as they were when it was
   shown (labels and option lists still point into the caller), and how
   many. */
const plat_form_field *window_host_form_last(u32 *count);

#endif /* WP_WINDOW_HOST_H */
