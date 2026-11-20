/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <string.h>
#include <Dialogs.h>
#include <Memory.h>
#include <Quickdraw.h>
#include "platform.h"

/* "Choose one of N" as a real modal dialog: the title as static text,
   one push button per option, and Cancel under them when there is a
   choice to cancel. A one-option call is a message with an OK button,
   which is how the application's own notices reach the writer.

   Built from an item list (DITL) assembled in memory and handed to
   NewDialog -- the backend has no resource file (docs/mac-platform.md's
   toolchain section), and the Dialog Manager takes items from any
   handle in the DITL layout, the same as one read from a resource.

   This used to be a popup menu (PopUpMenuSelect) with the title thrown
   away. A popup menu tracks the mouse only while the button is down, and
   the call nearly always comes from a menu pick or a key, after the
   button is up -- so it closed at once and answered "cancelled", and
   every message the application tried to show (a failed save, "Printed
   2 pages", "needs a gateway") vanished. Found on a MiSTer Mac Plus core
   with RetroWP's File > Print.

   Return is item 1, the first option, as ModalDialog has it by default.
   SetDialogDefaultItem would honour default_index, but it is System 7's
   and the backend runs on System 6, so the first button is the one
   Return presses. Labels are passed through as bytes: the application's
   are ASCII. */

#define WP_DIALOG_MARGIN   13
#define WP_DIALOG_BTN_H    20
#define WP_DIALOG_BTN_GAP  8
#define WP_DIALOG_LINE_H   16
#define WP_DIALOG_CHAR_W   7    /* Chicago 12, about */

/* One DITL item at h[at]: a placeholder, the rect, the type, the data
   (padded to even). Returns the offset after it. */
static long ditl_item(unsigned char *h, long at, short top, short left, short bottom,
                      short right, unsigned char type, const u8 *data, u32 len)
{
    short r[4];

    if (len > 255) len = 255;
    memset(h + at, 0, 4);
    at += 4;
    r[0] = top; r[1] = left; r[2] = bottom; r[3] = right;
    memcpy(h + at, r, 8);   /* the 68k's own byte order is the DITL's */
    at += 8;
    h[at++] = type;
    h[at++] = (unsigned char)len;
    memcpy(h + at, data, len);
    at += (long)len;
    if (len & 1u) h[at++] = 0;
    return at;
}

/* The choice dialog's keys and default button. ModalDialog's own filter
   maps Return and Enter to item 1, which here is the FIRST OPTION -- for
   "Unsaved changes will be lost." that was "Discard changes", so Return,
   the key a hurried writer presses, threw the work away (found on target
   under System 7.5.3, M20). The caller's default_index is the default
   instead, outlined the way a Mac default button is; Esc and Cmd-period
   cancel. A modal dialog is the only one up, so statics carry it. */
static INTEGER g_choice_default;     /* 1-based item, 0 = none */
static INTEGER g_choice_cancel;      /* 1-based item, 0 = none */

static void choice_outline(DialogPtr d)
{
    INTEGER type;
    Handle h;
    Rect r;
    PenState ps;

    if (g_choice_default == 0) return;
    GetDialogItem(d, g_choice_default, &type, &h, &r);
    GetPenState(&ps);
    PenSize(3, 3);
    InsetRect(&r, -4, -4);
    FrameRoundRect(&r, 16, 16);
    SetPenState(&ps);
}

static pascal Boolean choice_filter(DialogPtr d, EventRecord *e, INTEGER *item)
{
    char ch;

    if (e->what == updateEvt && (DialogPtr)e->message == d) {
        BeginUpdate(d);
        DrawDialog(d);
        choice_outline(d);
        EndUpdate(d);
        *item = 0;
        return true;
    }
    if (e->what != keyDown && e->what != autoKey) return false;
    ch = (char)(e->message & charCodeMask);
    if (ch == 0x0D || ch == 0x03) {                       /* Return, Enter */
        *item = g_choice_default;
        return true;
    }
    if (ch == 0x1B || ((e->modifiers & cmdKey) && ch == '.')) {   /* Esc, Cmd-. */
        *item = g_choice_cancel;
        return true;
    }
    return false;
}

wp_status plat_dialog_choice(plat_window *parent,
                              const u8 *title_utf8, u32 title_len,
                              const u8 * const *option_labels_utf8,
                              const u32 *option_label_lens,
                              u32 option_count, u32 default_index,
                              wp_bool *out_cancelled, u32 *out_index)
{
    static const u8 k_cancel[] = "Cancel";
    Rect screen = qd.screenBits.bounds;
    Rect box;
    Handle items;
    DialogPtr dlg;
    GrafPtr saved;
    wp_bool with_cancel = (wp_bool)(option_count > 1);
    u32 buttons, i, widest = 0, text_lines;
    short w, h, text_h, btn_w, y;
    long size, at;
    INTEGER hit = 0;

    WP_UNUSED(parent);
    if (option_count == 0) return WP_RANGE;
    if (title_len > 255) title_len = 255;

    /* A caller that already offers "Cancel" (the unsaved-changes
       question does) gets no second one: two Cancel buttons were drawn
       under System 7.5.3. Its own button is then the one Esc presses,
       and it answers cancelled (platform.h's Cancel rule). */
    g_choice_cancel = 0;
    for (i = 0; i < option_count; i++) {
        if (option_label_lens[i] == 6u && memcmp(option_labels_utf8[i], k_cancel, 6) == 0) {
            with_cancel = WP_FALSE;
            g_choice_cancel = (INTEGER)(i + 1u);
            break;
        }
    }

    /* As many options as the screen has room for (a Mac Plus's 342
       lines take about ten). */
    buttons = option_count + (with_cancel ? 1u : 0u);
    while (buttons > 1 &&
           (long)buttons * (WP_DIALOG_BTN_H + WP_DIALOG_BTN_GAP) + 120 >
           (long)(screen.bottom - screen.top)) {
        buttons--;
    }
    if (with_cancel && buttons < 2) return WP_RANGE;
    option_count = with_cancel ? buttons - 1u : buttons;

    for (i = 0; i < option_count; i++) {
        if (option_label_lens[i] > widest) widest = option_label_lens[i];
    }
    w = 300;
    if ((short)(widest * WP_DIALOG_CHAR_W + 2 * WP_DIALOG_MARGIN + 20) > w) {
        w = (short)(widest * WP_DIALOG_CHAR_W + 2 * WP_DIALOG_MARGIN + 20);
    }
    if (w > screen.right - screen.left - 20) w = (short)(screen.right - screen.left - 20);
    text_lines = (title_len * WP_DIALOG_CHAR_W) / (u32)(w - 2 * WP_DIALOG_MARGIN) + 1u;
    text_h = (short)(text_lines * WP_DIALOG_LINE_H);
    btn_w = (option_count == 1 && !with_cancel) ? 70 : (short)(w - 2 * WP_DIALOG_MARGIN);
    h = (short)(WP_DIALOG_MARGIN + text_h + WP_DIALOG_BTN_GAP +
                (short)buttons * (WP_DIALOG_BTN_H + WP_DIALOG_BTN_GAP) + WP_DIALOG_MARGIN - WP_DIALOG_BTN_GAP);

    /* The item list: 2 + (4 + 8 + 2 + 256) bytes an item at most. */
    size = 2 + (long)(buttons + 1u) * 270;
    items = NewHandle(size);
    if (items == NULL) return WP_NOMEM;
    HLock(items);
    {
        unsigned char *p = (unsigned char *)*items;
        short count_minus_1 = (short)buttons;   /* buttons + the text, less one */

        memcpy(p, &count_minus_1, 2);
        at = 2;
        y = (short)(WP_DIALOG_MARGIN + text_h + WP_DIALOG_BTN_GAP);
        for (i = 0; i < buttons; i++) {
            const u8 *label = (with_cancel && i == option_count) ? k_cancel : option_labels_utf8[i];
            u32 len = (with_cancel && i == option_count) ? 6u : option_label_lens[i];
            short left = (short)(w - WP_DIALOG_MARGIN - btn_w);

            at = ditl_item(p, at, y, left, (short)(y + WP_DIALOG_BTN_H), (short)(left + btn_w),
                           (unsigned char)(ctrlItem + btnCtrl), label, len);
            y = (short)(y + WP_DIALOG_BTN_H + WP_DIALOG_BTN_GAP);
        }
        at = ditl_item(p, at, WP_DIALOG_MARGIN, WP_DIALOG_MARGIN,
                       (short)(WP_DIALOG_MARGIN + text_h), (short)(w - WP_DIALOG_MARGIN),
                       (unsigned char)(statText + itemDisable), title_utf8, title_len);
    }
    HUnlock(items);
    SetHandleSize(items, at);

    /* Centred across, a third of the way down, as alerts sit. */
    box.left = (short)((screen.left + screen.right - w) / 2);
    box.right = (short)(box.left + w);
    box.top = (short)(screen.top + (screen.bottom - screen.top - h) / 3);
    if (box.top < screen.top + 40) box.top = (short)(screen.top + 40);
    box.bottom = (short)(box.top + h);

    GetPort(&saved);
    dlg = NewDialog(NULL, &box, (ConstStringPtr) "\p", true, dBoxProc, (WindowPtr)-1L,
                    false, 0, items);
    if (dlg == NULL) {
        DisposeHandle(items);
        return WP_NOMEM;
    }
    SetPort(dlg);
    if (with_cancel) g_choice_cancel = (INTEGER)buttons;
    else if (g_choice_cancel > (INTEGER)buttons) g_choice_cancel = 0;   /* trimmed off */
    g_choice_default = (default_index < buttons) ? (INTEGER)(default_index + 1u) : 0;
    do {
        ModalDialog(NewModalFilterUPP(choice_filter), &hit);
    } while (hit < 1 || hit > (INTEGER)buttons);
    DisposeDialog(dlg);   /* the item list with it */
    SetPort(saved);

    if (hit == g_choice_cancel) {         /* ours, or the caller's "Cancel" */
        if (out_cancelled) *out_cancelled = WP_TRUE;
        return WP_OK;
    }
    if (out_cancelled) *out_cancelled = WP_FALSE;
    if (out_index) *out_index = (u32)hit - 1u;
    return WP_OK;
}

/* -- plat_dialog_form: a modal dialog with editText items ----------- */

/* OK and Cancel are items 1 and 2; then each field is two items, its
   label (static text) and its value -- an editText item (the Dialog
   Manager's own editing, Tab between them), or for a choice a push
   button titled with the current option that steps to the next when
   clicked. Return and Enter are OK (outlined as the default), Esc and
   Cmd-period Cancel, through the choice dialog's filter. */
#define WP_FORM_LABEL_PX 124
#define WP_FORM_ROW_H    24

static Str255 g_form_p;

static ConstStringPtr form_pstr(const char *s)
{
    u32 n = (u32)strlen(s);
    if (n > 255) n = 255;
    g_form_p[0] = (unsigned char)n;
    memcpy(g_form_p + 1, s, n);
    return g_form_p;
}

wp_status plat_dialog_form(plat_window *parent,
                            const u8 *title_utf8, u32 title_len,
                            plat_form_field *fields, u32 count,
                            wp_bool *out_cancelled)
{
    static const u8 k_ok[] = "OK";
    static const u8 k_cancel[] = "Cancel";
    static u32 index[PLAT_FORM_MAX_FIELDS];
    Rect screen = qd.screenBits.bounds;
    Rect box;
    Handle items;
    DialogPtr dlg;
    GrafPtr saved;
    short w, h, y, vx, vw;
    long size, at;
    INTEGER hit = 0, type;
    Handle ih;
    Rect r;
    u32 i;

    WP_UNUSED(parent);
    if (count == 0 || count > PLAT_FORM_MAX_FIELDS || fields == NULL) return WP_RANGE;
    if (title_len > 255) title_len = 255;
    w = 470;
    if (w > screen.right - screen.left - 20) w = (short)(screen.right - screen.left - 20);
    vx = (short)(WP_DIALOG_MARGIN + WP_FORM_LABEL_PX);
    vw = (short)(w - vx - WP_DIALOG_MARGIN);
    h = (short)(WP_DIALOG_MARGIN + WP_DIALOG_LINE_H + WP_DIALOG_BTN_GAP +
                (short)count * WP_FORM_ROW_H + WP_DIALOG_BTN_GAP + WP_DIALOG_BTN_H + WP_DIALOG_MARGIN);

    size = 2 + (long)(3 + 2 * count) * 270;
    items = NewHandle(size);
    if (items == NULL) return WP_NOMEM;
    HLock(items);
    {
        unsigned char *p = (unsigned char *)*items;
        short count_minus_1 = (short)(2 + 2 * count);     /* OK, Cancel, title, 2 a field, less one */
        short by = (short)(h - WP_DIALOG_MARGIN - WP_DIALOG_BTN_H);

        memcpy(p, &count_minus_1, 2);
        at = 2;
        at = ditl_item(p, at, by, (short)(w - WP_DIALOG_MARGIN - 70), (short)(by + WP_DIALOG_BTN_H),
                       (short)(w - WP_DIALOG_MARGIN), (unsigned char)(ctrlItem + btnCtrl), k_ok, 2);
        at = ditl_item(p, at, by, (short)(w - WP_DIALOG_MARGIN - 160), (short)(by + WP_DIALOG_BTN_H),
                       (short)(w - WP_DIALOG_MARGIN - 90), (unsigned char)(ctrlItem + btnCtrl), k_cancel, 6);
        y = (short)(WP_DIALOG_MARGIN + WP_DIALOG_LINE_H + WP_DIALOG_BTN_GAP);
        for (i = 0; i < count; i++) {
            const char *l = fields[i].label != NULL ? fields[i].label : "";
            at = ditl_item(p, at, (short)(y + 3), WP_DIALOG_MARGIN, (short)(y + 3 + WP_DIALOG_LINE_H),
                           (short)(vx - 4), (unsigned char)(statText + itemDisable), (const u8 *)l, (u32)strlen(l));
            index[i] = fields[i].index;
            if (fields[i].kind == PLAT_FORM_CHOICE) {
                const char *o = (fields[i].options != NULL && index[i] < fields[i].option_count)
                                ? fields[i].options[index[i]] : "";
                at = ditl_item(p, at, y, vx, (short)(y + WP_DIALOG_BTN_H), (short)(vx + 150),
                               (unsigned char)(ctrlItem + btnCtrl), (const u8 *)o, (u32)strlen(o));
            } else {
                const char *t = fields[i].text != NULL ? fields[i].text : "";
                u32 n = (u32)strlen(t);
                if (n > PLAT_FORM_TEXT_MAX) n = PLAT_FORM_TEXT_MAX;
                /* An editText's frame is drawn 3 pixels outside its rect. */
                at = ditl_item(p, at, (short)(y + 3), (short)(vx + 3), (short)(y + 3 + WP_DIALOG_LINE_H),
                               (short)(vx + vw - 3), (unsigned char)editText, (const u8 *)t, n);
            }
            y = (short)(y + WP_FORM_ROW_H);
        }
        at = ditl_item(p, at, WP_DIALOG_MARGIN, WP_DIALOG_MARGIN, (short)(WP_DIALOG_MARGIN + WP_DIALOG_LINE_H),
                       (short)(w - WP_DIALOG_MARGIN), (unsigned char)(statText + itemDisable), title_utf8, title_len);
    }
    HUnlock(items);
    SetHandleSize(items, at);

    box.left = (short)((screen.left + screen.right - w) / 2);
    box.right = (short)(box.left + w);
    box.top = (short)(screen.top + (screen.bottom - screen.top - h) / 3);
    if (box.top < screen.top + 40) box.top = (short)(screen.top + 40);
    box.bottom = (short)(box.top + h);

    GetPort(&saved);
    dlg = NewDialog(NULL, &box, (ConstStringPtr) "\p", true, dBoxProc, (WindowPtr)-1L,
                    false, 0, items);
    if (dlg == NULL) {
        DisposeHandle(items);
        return WP_NOMEM;
    }
    SetPort(dlg);
    for (i = 0; i < count; i++) {                       /* the first text field takes the typing */
        if (fields[i].kind == PLAT_FORM_TEXT) {
            SelectDialogItemText(dlg, (INTEGER)(4 + 2 * i), 0, 32767);
            break;
        }
    }
    g_choice_default = 1;
    g_choice_cancel = 2;
    for (;;) {
        ModalDialog(NewModalFilterUPP(choice_filter), &hit);
        if (hit == 1 || hit == 2) break;
        if (hit >= 4 && ((hit - 4) & 1) == 0) {
            i = (u32)(hit - 4) / 2u;
            if (i < count && fields[i].kind == PLAT_FORM_CHOICE && fields[i].option_count > 0) {
                index[i] = (index[i] + 1u) % fields[i].option_count;
                GetDialogItem(dlg, hit, &type, &ih, &r);
                SetControlTitle((ControlHandle)ih, form_pstr(fields[i].options[index[i]]));
            }
        }
    }
    if (hit == 1) {
        for (i = 0; i < count; i++) {
            if (fields[i].kind == PLAT_FORM_CHOICE) {
                fields[i].index = index[i];
            } else if (fields[i].text != NULL && fields[i].text_cap > 0) {
                Str255 got;
                u32 n;
                GetDialogItem(dlg, (INTEGER)(4 + 2 * i), &type, &ih, &r);
                GetDialogItemText(ih, got);
                n = got[0];
                if (n > PLAT_FORM_TEXT_MAX) n = PLAT_FORM_TEXT_MAX;
                if (n > fields[i].text_cap - 1) n = fields[i].text_cap - 1;
                memcpy(fields[i].text, got + 1, n);
                fields[i].text[n] = '\0';
            }
        }
    }
    DisposeDialog(dlg);
    SetPort(saved);
    *out_cancelled = (wp_bool)(hit != 1);
    return WP_OK;
}
