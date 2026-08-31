#include <string.h>
#include <Menus.h>
#include "platform.h"

/* Real classic Mac "choose one of N" implementation: a temporary popup
   menu built from the option list and shown via PopUpMenuSelect --
   InsertMenu(menu, -1) is the standard, well-documented way to make a
   menu known to the Menu Manager without adding it to the visible menu
   bar (the same mechanism real hierarchical submenus use), and
   PopUpMenuSelect then displays and tracks it modally at a given
   screen position, returning 0 if the user dismissed it without
   choosing (click outside, or Escape). No DLOG/DITL resource needed --
   platform/mac deliberately has no .r resource file at all (see
   docs/mac-platform.md's toolchain section), and a popup menu is a
   real, historically authentic classic-Mac UI pattern for exactly this
   "choose one of a small enumerated list" shape (early word processors
   commonly used popup/pulldown menus for font size pickers, not modal
   dialogs).

   plat_dialog_message/plat_window_create/plat_window_destroy/
   plat_window_get_gc are deliberately NOT implemented here -- neither
   platform/atari nor platform/amiga implements any of the four either;
   nothing in the current engine/platform/common code calls them. */

#define WP_DIALOG_CHOICE_MENU_ID 200

wp_status plat_dialog_choice(plat_window *parent,
                              const u8 *title_utf8, u32 title_len,
                              const u8 * const *option_labels_utf8,
                              const u32 *option_label_lens,
                              u32 option_count, u32 default_index,
                              wp_bool *out_cancelled, u32 *out_index)
{
    MenuHandle menu;
    unsigned char pitem[256];
    long result;
    u32 i;

    WP_UNUSED(parent);
    WP_UNUSED(title_utf8);
    WP_UNUSED(title_len);

    menu = NewMenu(WP_DIALOG_CHOICE_MENU_ID, (ConstStringPtr) "\p");
    if (menu == NULL) return WP_ERR;

    for (i = 0; i < option_count; i++) {
        u32 n = option_label_lens[i];

        if (n > 250) n = 250;
        pitem[0] = (unsigned char)n;
        memcpy(pitem + 1, option_labels_utf8[i], n);
        InsertMenuItem(menu, pitem, (short)i);
    }
    InsertMenu(menu, -1);

    result = PopUpMenuSelect(menu, 100, 100, (short)(default_index + 1));

    DeleteMenu(WP_DIALOG_CHOICE_MENU_ID);
    DisposeMenu(menu);

    if ((result & 0xFFFFL) == 0) {
        if (out_cancelled) *out_cancelled = WP_TRUE;
        return WP_OK;
    }
    if (out_cancelled) *out_cancelled = WP_FALSE;
    if (out_index) *out_index = (u32)(result & 0xFFFFL) - 1;
    return WP_OK;
}
