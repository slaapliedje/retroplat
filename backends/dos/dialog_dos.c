#include <stddef.h>
#include <i86.h>
#include "platform.h"
#include "metrics_dos_internal.h"

/* Real, modal, blocking dialogs (T14.5) using the bottom screen row as
   a one-line prompt -- the "numbered text prompt on DOS" shape
   engine/include/platform.h's own plat_dialog_choice doc comment
   already anticipated, predating this milestone. There is no form_do()-
   equivalent widget toolkit in plain BIOS text mode, so this reads keys
   directly via INT 16h in its own small blocking loop, the same way
   Atari's form_do() call and Mac's ModalDialog() each block their own
   caller until a choice is made -- a real, expected shape for a modal
   dialog on any backend, not a shortcut unique to DOS. */

#define WP_DOS_DIALOG_ROW (DOS_SCREEN_ROWS - 1)
#define WP_DOS_DIALOG_ATTR 0x70  /* black on light gray -- reads as a prompt bar, matching the title bar's own convention */

typedef unsigned char __far *dos_dialog_video_ptr;

static dos_dialog_video_ptr dialog_video_mem(void)
{
    return (dos_dialog_video_ptr)MK_FP(0xB800, 0);
}

static void dialog_clear_row(void)
{
    dos_dialog_video_ptr vmem = dialog_video_mem();
    int col;
    int offset;

    for (col = 0; col < DOS_SCREEN_COLS; col++) {
        offset = (WP_DOS_DIALOG_ROW * DOS_SCREEN_COLS + col) * 2;
        vmem[offset] = ' ';
        vmem[offset + 1] = WP_DOS_DIALOG_ATTR;
    }
}

/* ASCII-only, same limitation platform/dos/draw_dos.c's own plat_utf8_
   to_native already documents -- a real CP437 table is future work,
   not attempted here. */
static void dialog_print(int col, const u8 *utf8, u32 len)
{
    dos_dialog_video_ptr vmem = dialog_video_mem();
    u32 i;

    for (i = 0; i < len && col + (int)i < DOS_SCREEN_COLS; i++) {
        int offset = (WP_DOS_DIALOG_ROW * DOS_SCREEN_COLS + col + (int)i) * 2;
        u8 c = utf8[i];

        vmem[offset] = (c >= 0x20 && c < 0x7F) ? c : (u8)'?';
        vmem[offset + 1] = WP_DOS_DIALOG_ATTR;
    }
}

static void dialog_print_cstr(int col, const char *s)
{
    const u8 *p = (const u8 *)s;
    u32 len = 0;

    while (p[len] != '\0') len++;
    dialog_print(col, p, len);
}

static u8 dialog_read_key(void)
{
    union REGS regs;

    regs.h.ah = 0x00;
    int86(0x16, &regs, &regs);
    return regs.h.al;
}

wp_status plat_dialog_message(plat_window *parent, plat_dialog_kind kind,
                               const u8 *utf8_msg, u32 len, plat_dialog_result *out)
{
    int col;
    u8 key;

    WP_UNUSED(parent);

    dialog_clear_row();
    dialog_print(0, utf8_msg, len);
    col = (int)len + 1;
    switch (kind) {
    case PLAT_DIALOG_OKCANCEL: dialog_print_cstr(col, "(Enter=OK Esc=Cancel)"); break;
    case PLAT_DIALOG_YESNO:    dialog_print_cstr(col, "(Y/N)"); break;
    case PLAT_DIALOG_OK: default: dialog_print_cstr(col, "(Enter=OK)"); break;
    }

    for (;;) {
        key = dialog_read_key();
        switch (kind) {
        case PLAT_DIALOG_OKCANCEL:
            if (key == 13) { if (out) *out = PLAT_DR_OK; dialog_clear_row(); return WP_OK; }
            if (key == 27) { if (out) *out = PLAT_DR_CANCEL; dialog_clear_row(); return WP_OK; }
            break;
        case PLAT_DIALOG_YESNO:
            if (key == 'y' || key == 'Y') { if (out) *out = PLAT_DR_YES; dialog_clear_row(); return WP_OK; }
            if (key == 'n' || key == 'N' || key == 27) { if (out) *out = PLAT_DR_NO; dialog_clear_row(); return WP_OK; }
            break;
        case PLAT_DIALOG_OK:
        default:
            if (key == 13 || key == 27) { if (out) *out = PLAT_DR_OK; dialog_clear_row(); return WP_OK; }
            break;
        }
    }
}

/* Lists up to 9 options as "1)Label 2)Label ..." on the same bottom-row
   prompt -- a single-digit key (1-9) selects, Escape cancels. Silently
   caps at 9 options (WP_DIALOG_MAX_OPTIONS-shaped limit, matching
   dialog_atari.c's own real cap): nothing in this project's current
   action set (Font Size's 7 entries, Alignment's 3) needs more. */
wp_status plat_dialog_choice(plat_window *parent,
                              const u8 *title_utf8, u32 title_len,
                              const u8 * const *option_labels_utf8,
                              const u32 *option_label_lens,
                              u32 option_count, u32 default_index,
                              wp_bool *out_cancelled, u32 *out_index)
{
    int col;
    u32 i;
    u8 key;

    WP_UNUSED(parent);
    WP_UNUSED(default_index);

    if (option_count == 0 || option_count > 9) return WP_RANGE;

    dialog_clear_row();
    dialog_print(0, title_utf8, title_len);
    col = (int)title_len + 2;

    for (i = 0; i < option_count && col < DOS_SCREEN_COLS - 4; i++) {
        dos_dialog_video_ptr vmem = dialog_video_mem();
        int offset = (WP_DOS_DIALOG_ROW * DOS_SCREEN_COLS + col) * 2;

        vmem[offset] = (u8)('1' + i);
        vmem[offset + 1] = WP_DOS_DIALOG_ATTR;
        vmem[offset + 2] = (u8)')';
        vmem[offset + 3] = WP_DOS_DIALOG_ATTR;
        col += 2;
        dialog_print(col, option_labels_utf8[i], option_label_lens[i]);
        col += (int)option_label_lens[i] + 1;
    }

    for (;;) {
        key = dialog_read_key();
        if (key == 27) {
            if (out_cancelled) *out_cancelled = WP_TRUE;
            dialog_clear_row();
            return WP_OK;
        }
        if (key >= '1' && key <= '9') {
            u32 idx = (u32)(key - '1');
            if (idx < option_count) {
                if (out_cancelled) *out_cancelled = WP_FALSE;
                if (out_index) *out_index = idx;
                dialog_clear_row();
                return WP_OK;
            }
        }
    }
}
