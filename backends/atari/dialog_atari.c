/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <string.h>
#include <gem.h>
#include "platform.h"
#include "gemcompat.h"   /* OB_SPEC_*: ob_spec is a union on one GEM and a LONG on the other */

/* Real GEM form_do()-based "choose one of N" dialog (M8.5), replacing
   the M8.5-Phase-6 placeholder that always picked the default option.
   form_do() is used nowhere else in this codebase -- genuinely new
   work, not a thin wrapper, though it reuses the array-index ob_next/
   ob_head/ob_tail conventions the hand-built menu tree in app_shell.c
   already established (see docs/atari-platform.md's "Hand-built menu
   OBJECT tree" section). No resource-file compiler exists in this
   project's toolchain (same reason the menu tree is hand-built, not
   loaded from a .rsc), so this OBJECT tree is built at runtime, sized
   to the caller's actual option_count. */

#define WP_DIALOG_MAX_OPTIONS 9
#define WP_DIALOG_OBJ_COUNT   (WP_DIALOG_MAX_OPTIONS + 4) /* root, title, options..., OK, Cancel */

/* The root G_BOX's ob_spec is NOT a pointer. For G_BOX/G_IBOX/G_BOXCHAR
   the low 16 bits are a packed color word and bits 23-16 are a signed
   border thickness (Atari Compendium p.6.18) -- only G_BUTTON/G_STRING/
   G_TITLE put a char* there. This used to be NULL, which is a perfectly
   legal encoding meaning: thickness 0 (no border), and a color word of
   all zeroes = pattern IP_HOLLOW with the transparent bit clear, i.e.
   an unfilled, borderless box. The dialog therefore never erased the
   document underneath it and drew its text straight over the page --
   which is what the "vertical stripes" corruption in the screenshots
   actually was, not a video-mode fault.

   Built out per that same table: border black (bits 15-12), text black
   (bits 11-8), opaque (bit 7), IP_SOLID (bits 6-4), interior white
   (bits 3-0), thickness 2. gemlib spells the AES color constants
   G_WHITE/G_BLACK, not the Compendium's bare WHITE/BLACK. */
#define WP_DIALOG_BOX_SPEC \
    (0x00020000L | ((long)G_BLACK << 12) | ((long)G_BLACK << 8) | 0x0080L | \
     ((long)IP_SOLID << 4) | (long)G_WHITE)

/* WP_AES_NEAR: form_do and objc_draw read the tree and its strings in
   place, by a 16-bit address in bank $00, on gem4xe (gemcompat.h). They
   were plain statics, which the large data model puts in far memory, so
   a choice of two or more there would have drawn whatever sat at the
   same offset in bank $00. */
/* The choice dialog's tree and strings, and the form's (plat_dialog_form,
   below), share one block: both are modal, so they are never up at once,
   and on gem4xe bank $00 is the scarce thing -- the form's own 1.7 KB
   beside the choice's left no room for the application's stack. */
#define WP_FORM_LABEL_W   18        /* characters */
#define WP_FORM_CHOICE_W  20
#define WP_FORM_OBJ_COUNT (2 + 2 * PLAT_FORM_MAX_FIELDS + 2)

static WP_AES_NEAR union {
    struct {
        OBJECT tree[WP_DIALOG_OBJ_COUNT];
        char   title[40];
        char   options[WP_DIALOG_MAX_OPTIONS][32];
    } choice;
    struct {
        OBJECT  tree[WP_FORM_OBJ_COUNT];
        TEDINFO ted[PLAT_FORM_MAX_FIELDS];
        char    text[PLAT_FORM_MAX_FIELDS][PLAT_FORM_TEXT_MAX + 1];
        char    label[PLAT_FORM_MAX_FIELDS][WP_FORM_LABEL_W];
        char    choice[PLAT_FORM_MAX_FIELDS][WP_FORM_CHOICE_W];
        char    title[40];
        char    tmplt[PLAT_FORM_TEXT_MAX + 1];
        char    valid[PLAT_FORM_TEXT_MAX + 1];
    } form;
} g_dlg;
#define g_dialog_tree        g_dlg.choice.tree
#define g_dialog_title_buf   g_dlg.choice.title
#define g_dialog_option_bufs g_dlg.choice.options
#define g_form_tree          g_dlg.form.tree
#define g_form_ted           g_dlg.form.ted
#define g_form_text          g_dlg.form.text
#define g_form_label         g_dlg.form.label
#define g_form_choice        g_dlg.form.choice
#define g_form_title         g_dlg.form.title
#define g_form_tmplt         g_dlg.form.tmplt
#define g_form_valid         g_dlg.form.valid
static WP_AES_NEAR char   g_dialog_ok_buf[] = "  OK  ";
static WP_AES_NEAR char   g_dialog_cancel_buf[] = "Cancel";

/* ASCII-only, same limitation plat_utf8_to_native already documents
   elsewhere in this backend (draw_atari.c) -- a real ISO-8859/ATASCII
   transliteration table is future work, not attempted here. */
static void copy_bounded(char *out, u32 out_cap, const u8 *utf8, u32 len)
{
    u32 i, n;

    n = (len < out_cap - 1) ? len : out_cap - 1;
    for (i = 0; i < n; i++) {
        u8 c = utf8[i];
        out[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    out[n] = '\0';
}

static void set_obj(short index, short next, short head, short tail,
                     unsigned short type, unsigned short flags, unsigned short state,
                     char *text, short x, short y, short w, short h)
{
    g_dialog_tree[index].ob_next = next;
    g_dialog_tree[index].ob_head = head;
    g_dialog_tree[index].ob_tail = tail;
    g_dialog_tree[index].ob_type = type;
    g_dialog_tree[index].ob_flags = flags;
    g_dialog_tree[index].ob_state = state;
    OB_SPEC_SET_STRING(g_dialog_tree[index], text);
    g_dialog_tree[index].ob_x = x;
    g_dialog_tree[index].ob_y = y;
    g_dialog_tree[index].ob_width = w;
    g_dialog_tree[index].ob_height = h;
}

/* Builds g_dialog_tree for a "choose one of N" dialog and computes its
   final screen position via form_center() -- everything
   plat_dialog_choice needs EXCEPT the genuinely interactive part
   (form_dial/objc_draw/form_do). Split out so wp_atari_dialog_selfcheck
   below can exercise the tree-construction logic (the part with real
   index arithmetic that can go wrong -- see child_chain_ok()'s own
   rationale in app_shell.c) without blocking on user input, the same
   boundary every other wp_atari_*_selfcheck in this codebase already
   respects. form_center() itself is a pure screen-geometry computation,
   not an interactive call -- safe to run here. */
static wp_status build_dialog_tree(const u8 *title_utf8, u32 title_len,
                                    const u8 * const *option_labels_utf8,
                                    const u32 *option_label_lens,
                                    u32 option_count, u32 default_index,
                                    wp_bool cancel_default,
                                    short *out_first_option, short *out_ok_obj,
                                    short *out_cancel_obj,
                                    short *out_cx, short *out_cy, short *out_cw, short *out_ch)
{
    short root = 0, title_obj = 1, first_option = 2;
    short ok_obj, cancel_obj;
    short char_w, char_h, box_w, box_h;
    short content_w, y;
    u32 i;

    if (option_count == 0 || option_count > WP_DIALOG_MAX_OPTIONS) return WP_RANGE;

    /* A caller with no meaningful default (or a stale one) must not
       leave the tree with zero selected radio buttons -- see
       plat_dialog_choice's own out_index handling below. */
    if (default_index >= option_count) default_index = 0;

    /* Wipe the whole array, not just the objects this call populates.
       g_dialog_tree is static and every dialog reuses it at a DIFFERENT
       size, so a 7-option Font Size followed by a 2-option Dictate would
       otherwise leave object 10 carrying the previous call's OF_LASTOB
       while the new tree ends at object 5. The AES stops its linear scan
       at the first OF_LASTOB it finds, so it would have walked objects
       6..10 -- stale ones belonging to a dialog that is no longer on
       screen. This also clears leftover OS_SELECTED from a previous
       run's buttons. */
    memset(g_dialog_tree, 0, sizeof(g_dialog_tree));

    /* Real character cell size for the current resolution -- the same
       call app_shell_run() already makes for the screen-width check
       (see the "80 column" startup warning), here used for real this
       time instead of discarded into a dummy. Unlike document text
       layout (which needs plat_measure_text's real DPI-correct
       metrics), a picker dialog's own box sizing only needs to be
       "roughly right," so the system font's own cell size is good
       enough without opening a real font handle. */
    graf_handle(&char_w, &char_h, &box_w, &box_h);
    WP_UNUSED(box_w);
    WP_UNUSED(box_h);

    copy_bounded(g_dialog_title_buf, (u32)sizeof(g_dialog_title_buf), title_utf8, title_len);

    ok_obj = (short)(first_option + (short)option_count);
    cancel_obj = (short)(ok_obj + 1);

    content_w = (short)(((short)strlen(g_dialog_title_buf) + 4) * char_w);
    for (i = 0; i < option_count; i++) {
        short w;

        copy_bounded(g_dialog_option_bufs[i], (u32)sizeof(g_dialog_option_bufs[i]),
                     option_labels_utf8[i], option_label_lens[i]);
        w = (short)(((short)strlen(g_dialog_option_bufs[i]) + 6) * char_w);
        if (w > content_w) content_w = w;
    }
    /* The floor is set by the BUTTON ROW, not by a round number. OK sits
       at 2 chars in and is 8 wide (right edge 10); Cancel is 8 wide with
       its right edge 2 from the far side, i.e. its left edge is at
       content_w - 10. Anything under 22 chars overlaps them, and the old
       floor of 16 did exactly that -- the two buttons printed on top of
       each other as "Q Cancel", swallowing OK, on every dialog whose
       title and options were short. Format > Alignment, whose widest
       string is "Alignment" (13 with padding), hit it every time. */
    if (content_w < 22 * char_w) content_w = (short)(22 * char_w);

    set_obj(root, -1, title_obj, cancel_obj, G_BOX, OF_NONE, OS_OUTLINED, NULL,
            0, 0, content_w, 0 /* height finalized below, once y is known */);
    /* Assigned through .index (the union's long member) rather than
       through set_obj's char* parameter, because for a G_BOX this field
       is a packed value and not a pointer at all -- see
       WP_DIALOG_BOX_SPEC. set_obj keeps its char* signature since every
       OTHER object in this tree really does store a string there. */
    OB_SPEC_SET_INDEX(g_dialog_tree[root], WP_DIALOG_BOX_SPEC);

    y = (short)(char_h / 2);
    set_obj(title_obj, first_option, -1, -1, G_STRING, OF_NONE, OS_NORMAL,
            g_dialog_title_buf, char_w, y, (short)(content_w - 2 * char_w), char_h);
    y = (short)(y + char_h + char_h / 2);

    for (i = 0; i < option_count; i++) {
        short obj = (short)(first_option + (short)i);
        short next = (short)((i + 1 < option_count) ? obj + 1 : ok_obj);
        unsigned short state = (i == default_index) ? OS_SELECTED : OS_NORMAL;

        set_obj(obj, next, -1, -1, G_BUTTON, (unsigned short)(OF_SELECTABLE | OF_RBUTTON), state,
                g_dialog_option_bufs[i], (short)(2 * char_w), y, (short)(content_w - 4 * char_w), char_h);
        /* Pitch is height + 2, not height. A G_BUTTON is drawn with a
           border ON its own bounds, so rows stacked at exactly char_h
           share edges and the neighbouring border cuts through the
           glyphs -- clearly visible on a real 640x200 screenshot, where
           char_h is 8 and a line of 8-pixel text leaves no room. */
        y = (short)(y + char_h + 2);
    }
    y = (short)(y + char_h / 2);

    /* The default button is OK, or Cancel when the caller made its own
       "Cancel" the default (plat_dialog_choice): then Return cancels. */
    set_obj(ok_obj, cancel_obj, -1, -1, G_BUTTON,
            (unsigned short)(OF_SELECTABLE | OF_EXIT | (cancel_default ? 0 : OF_DEFAULT)), OS_NORMAL,
            g_dialog_ok_buf, (short)(2 * char_w), y, (short)(8 * char_w), char_h);
    /* OF_LASTOB on the final object of the ARRAY. The Compendium marks
       this "(Required!)" (p.6.16) and it was simply absent here: the AES
       determines a tree's extent by scanning for it, so without it
       objc_draw/form_do ran off the end of g_dialog_tree into whatever
       BSS follows -- which is g_dialog_title_buf and g_dialog_option_bufs,
       label text reinterpreted as OBJECT structs.

       This is the identical defect #134 found in the hand-built menu
       tree, where OF_LASTOB sat on the wrong object and hid every
       dropdown past it. wp_menu_tree_selfcheck gained a whole-array
       "exactly one, and it is the final object" assertion then; this
       tree, built by different code, never got the same guard. It has
       one now -- see wp_atari_dialog_selfcheck. */
    set_obj(cancel_obj, root, -1, -1, G_BUTTON,
            (unsigned short)(OF_SELECTABLE | OF_EXIT | OF_LASTOB | (cancel_default ? OF_DEFAULT : 0)),
            OS_NORMAL, g_dialog_cancel_buf, (short)(content_w - 10 * char_w), y, (short)(8 * char_w), char_h);

    g_dialog_tree[root].ob_height = (short)(y + char_h + char_h / 2);

    /* form_center() positions the root itself; the x/y/w/h it hands back
       are deliberately NOT the object's own. "These values take into
       account negative borders, outlining, and shadowing. This is meant
       to provide a suitable clipping rectangle for objc_draw()"
       (Compendium p.6.80). This used to copy them back into the root's
       ob_x/ob_y/ob_width/ob_height, which -- because this dialog carries
       OS_OUTLINED -- moved the box up and left by the outline allowance
       and grew it by twice that, while the children stayed where they
       were, since their coordinates are relative to the root. So the
       frame no longer matched its own contents. They are used below for
       exactly what they are for: form_dial's reserved area and
       objc_draw's clip rectangle. */
    form_center(g_dialog_tree, out_cx, out_cy, out_cw, out_ch);

    *out_first_option = first_option;
    *out_ok_obj = ok_obj;
    *out_cancel_obj = cancel_obj;
    return WP_OK;
}

/* A message -- a choice of one -- is a GEM alert, as a GEM program's
   messages are: form_alert saves and restores what is under it and
   wraps the text, where the choice form below put the message on its
   title line (cut at 39 characters), showed the one option as an
   inverted list row above OK and Cancel, and left the last dialog's
   frame behind it when the caller had not been back to its event loop
   in between. Seen on a MiSTer ST core, "Printed 1 page on
   HP-Color-LaserJet-MFP" in a Print-on dialog's clothes.

   The alert's own limits: five lines of at most WP_ALERT_LINE
   characters, a button of at most ten, and '[', ']' and '|' are its
   syntax, so they are shown as '(', ')' and '/'. Lines break at spaces;
   a word longer than a line is cut. */
#define WP_ALERT_LINE  30
#define WP_ALERT_LINES 5
static char g_alert_buf[16 + WP_ALERT_LINES * (WP_ALERT_LINE + 1) + 16];

static char alert_char(u8 c)
{
    if (c == '[') return '(';
    if (c == ']') return ')';
    if (c == '|') return '/';
    if (c < 0x20) return ' ';
    return (char)c;
}

static void show_alert(const u8 *msg, u32 len, const u8 *button, u32 button_len)
{
    u32 n = 0, i = 0, lines = 0;

    memcpy(g_alert_buf, "[1][", 4);
    n = 4;
    while (i < len && lines < WP_ALERT_LINES) {
        u32 take, brk;

        while (i < len && msg[i] == ' ') i++;          /* no line starts with a space */
        if (i >= len) break;
        take = len - i;
        if (take > WP_ALERT_LINE) {
            take = WP_ALERT_LINE;
            for (brk = take; brk > 0 && msg[i + brk] != ' '; brk--) { }
            if (brk > 0) take = brk;                   /* else: one long word, cut */
        }
        if (lines > 0) g_alert_buf[n++] = '|';
        for (brk = 0; brk < take; brk++) g_alert_buf[n++] = alert_char(msg[i + brk]);
        i += take;
        lines++;
    }
    if (lines == 0) g_alert_buf[n++] = ' ';
    g_alert_buf[n++] = ']';
    g_alert_buf[n++] = '[';
    if (button_len == 0 || button_len > 10) {
        memcpy(g_alert_buf + n, "OK", 2);
        n += 2;
    } else {
        for (i = 0; i < button_len; i++) g_alert_buf[n++] = alert_char(button[i]);
    }
    g_alert_buf[n++] = ']';
    g_alert_buf[n] = '\0';
    form_alert(1, g_alert_buf);
}

wp_status plat_dialog_choice(plat_window *parent,
                              const u8 *title_utf8, u32 title_len,
                              const u8 * const *option_labels_utf8,
                              const u32 *option_label_lens,
                              u32 option_count, u32 default_index,
                              wp_bool *out_cancelled, u32 *out_index)
{
    short first_option, ok_obj, cancel_obj;
    short cx, cy, cw, ch;
    short chosen_obj;
    wp_status st;
    u32 i;
    /* platform.h's Cancel rule. A caller's own "Cancel" (the unsaved-
       changes question has one) would otherwise be a radio row above a
       Cancel button: it comes off the list, the button stands for it,
       and the button is the default when the caller's Cancel was. */
    const u8 *labels[WP_DIALOG_MAX_OPTIONS];
    u32 lens[WP_DIALOG_MAX_OPTIONS];
    u32 caller_cancel = option_count;     /* none */
    u32 shown = 0, shown_default = 0;

    WP_UNUSED(parent);

    if (option_count == 1) {
        show_alert(title_utf8, title_len, option_labels_utf8[0], option_label_lens[0]);
        if (out_cancelled) *out_cancelled = WP_FALSE;
        if (out_index) *out_index = 0;
        return WP_OK;
    }

    if (option_count > WP_DIALOG_MAX_OPTIONS) return WP_RANGE;
    for (i = 0; i < option_count; i++) {
        if (caller_cancel == option_count && option_label_lens[i] == 6u &&
            memcmp(option_labels_utf8[i], "Cancel", 6) == 0) {
            caller_cancel = i;
            continue;
        }
        if (i == default_index) shown_default = shown;
        labels[shown] = option_labels_utf8[i];
        lens[shown] = option_label_lens[i];
        shown++;
    }

    st = build_dialog_tree(title_utf8, title_len, labels, lens,
                            shown, shown_default,
                            (wp_bool)(caller_cancel < option_count && default_index == caller_cancel),
                            &first_option, &ok_obj, &cancel_obj, &cx, &cy, &cw, &ch);
    if (st != WP_OK) return st;

    form_dial(FMD_START, cx, cy, 0, 0, cx, cy, cw, ch);
    objc_draw(g_dialog_tree, 0, MAX_DEPTH, cx, cy, cw, ch);

    /* 0, NOT first_option. This is the bus error (#177). form_do's second
       argument is "the object index ... of the edit cursor (the object
       must be flagged as EDITABLE). If the form has no text editable
       fields, you should use 0" (Compendium p.6.81). This form has no
       editable fields at all -- every object is a G_BUTTON or G_STRING --
       so passing first_option pointed the AES at object 2 and told it to
       start text editing there. The AES duly read that object's ob_spec
       as a TEDINFO*, but on a G_BUTTON ob_spec is a plain char* to the
       label, so te_ptext (TEDINFO's first field, itself a char*) came
       back as the first four BYTES OF THE LABEL, and dereferencing that
       faulted.

       Which is why the reported fault address was literally the label
       text: Format > Alignment's first option is "Left", and the crash
       was "Bus Error reading at address $4c656674" -- 'L','e','f','t'.
       Font Size would have read $38207074 ("8 pt"). */
    chosen_obj = (short)(form_do(g_dialog_tree, 0) & 0x7FFF);

    /* Deselect the EXIT button the user struck before the area under the
       dialog is restored, so it is not left drawn inverted (the
       Compendium's own do_dialog example does exactly this). */
    if (chosen_obj >= 0 && chosen_obj < WP_DIALOG_OBJ_COUNT) {
        g_dialog_tree[chosen_obj].ob_state &= (unsigned short)~OS_SELECTED;
    }

    form_dial(FMD_FINISH, cx, cy, 0, 0, cx, cy, cw, ch);

    if (out_cancelled) *out_cancelled = (wp_bool)(chosen_obj == cancel_obj);
    /* Always write a defined index. The scan below finds nothing if no
       radio button is selected, and callers index straight into their own
       option arrays with the result (act_format_align does aligns[chosen])
       -- so leaving it untouched would hand them an uninitialised
       subscript. default_index is the honest fallback: it is what the
       dialog was showing as selected. */
    if (out_index) *out_index = (default_index < option_count) ? default_index : 0;
    if (chosen_obj != cancel_obj && out_index) {
        for (i = 0; i < shown; i++) {
            if (g_dialog_tree[first_option + i].ob_state & OS_SELECTED) {
                /* Back to the caller's numbering, past its "Cancel". */
                *out_index = (i >= caller_cancel) ? i + 1u : i;
                break;
            }
        }
        /* The fallback must not answer the Cancel row as a choice. */
        if (*out_index == caller_cancel) *out_index = (caller_cancel == 0) ? 1u : 0u;
    }

    return WP_OK;
}

/* Proves build_dialog_tree's index arithmetic for a representative
   option_count (the largest real caller, Font Size's 7 options) without
   ever calling the blocking, interactive form_do() -- same boundary
   every other selfcheck in this codebase respects. Bounded-step walk
   (never more than WP_DIALOG_OBJ_COUNT hops) matches
   wp_menu_tree_selfcheck's own child_chain_ok() rationale: catches an
   accidental ob_next cycle or broken chain on host/CI, before ever
   booting real Hatari. */
wp_bool wp_atari_dialog_selfcheck(void)
{
    static const char *const labels_c[] = { "8 pt", "10 pt", "12 pt", "14 pt", "18 pt", "24 pt", "36 pt" };
    const u8 *labels[7];
    u32 lens[7];
    short first_option, ok_obj, cancel_obj;
    short cx, cy, cw, ch;
    u32 i;
    short steps;
    short cur;

    for (i = 0; i < 7; i++) {
        labels[i] = (const u8 *)labels_c[i];
        lens[i] = (u32)strlen(labels_c[i]);
    }

    if (build_dialog_tree((const u8 *)"Font Size", 9, labels, lens, 7, 2, WP_FALSE,
                           &first_option, &ok_obj, &cancel_obj, &cx, &cy, &cw, &ch) != WP_OK) {
        return WP_FALSE;
    }

    if (first_option != 2) return WP_FALSE;
    if (ok_obj != 9) return WP_FALSE;      /* first_option(2) + 7 options */
    if (cancel_obj != 10) return WP_FALSE;
    if (g_dialog_tree[0].ob_next != -1) return WP_FALSE;   /* true root */
    if (g_dialog_tree[0].ob_head != 1) return WP_FALSE;    /* title */
    if (g_dialog_tree[0].ob_tail != cancel_obj) return WP_FALSE;
    if (cw <= 0 || ch <= 0) return WP_FALSE;

    /* exactly one option starts OS_SELECTED (the default_index=2 seed) */
    {
        u32 selected_count = 0;
        for (i = 0; i < 7; i++) {
            if (g_dialog_tree[first_option + i].ob_state & OS_SELECTED) selected_count++;
        }
        if (selected_count != 1) return WP_FALSE;
        if (!(g_dialog_tree[first_option + 2].ob_state & OS_SELECTED)) return WP_FALSE;
    }

    /* bounded-step ob_next walk from the root's head to its tail --
       must terminate by wrapping back to the root within a bounded
       number of hops, never looping forever on a bad index */
    cur = g_dialog_tree[0].ob_head;
    for (steps = 0; steps < WP_DIALOG_OBJ_COUNT; steps++) {
        if (cur == g_dialog_tree[0].ob_tail) {
            if (g_dialog_tree[cur].ob_next != 0) return WP_FALSE;
            break;
        }
        cur = g_dialog_tree[cur].ob_next;
    }
    if (steps >= WP_DIALOG_OBJ_COUNT) return WP_FALSE; /* did not terminate */

    /* --- #177: the three things that made this tree crash the AES ---

       Everything above proves index arithmetic, which is what this
       selfcheck was built for and which was never wrong. The defects
       that actually bus-errored the machine were in the OBJECT CONTENTS,
       so they lived entirely outside what was being checked. Asserted
       here as invariants of the tree rather than of any one caller. */

    /* 1. Exactly one OF_LASTOB, on the final object. Same whole-array
          form wp_menu_tree_selfcheck uses, and for the same reason: the
          AES stops scanning there, so "somewhere" is not good enough. */
    {
        short lastob_count = 0;
        for (i = 0; i < (u32)WP_DIALOG_OBJ_COUNT; i++) {
            if (g_dialog_tree[i].ob_flags & OF_LASTOB) lastob_count++;
        }
        if (lastob_count != 1) return WP_FALSE;
        if ((g_dialog_tree[cancel_obj].ob_flags & OF_LASTOB) == 0) return WP_FALSE;
    }

    /* 2. No object is EDITABLE, which is what makes form_do(tree, 0)
          the correct call. If a future editable field is ever added
          here, this fires and forces form_do's second argument to be
          revisited deliberately instead of silently faulting again. */
    for (i = 0; i < (u32)WP_DIALOG_OBJ_COUNT; i++) {
        if (g_dialog_tree[i].ob_flags & OF_EDITABLE) return WP_FALSE;
    }

    /* 3. The root box carries a real packed color word, not a pointer
          and not zero -- an opaque, solid-filled interior, so the dialog
          erases the document behind it. */
    if (OB_SPEC_INDEX(g_dialog_tree[0]) != WP_DIALOG_BOX_SPEC) return WP_FALSE;
    if ((OB_SPEC_INDEX(g_dialog_tree[0]) & 0x0080L) == 0) return WP_FALSE; /* opaque */

    /* 4. OK and Cancel do not overlap, and both sit inside the box.
          Caught on a real screenshot, not here, because nothing was
          checking geometry -- the two buttons were drawing on top of
          each other and OK was unreadable. Every object's coordinates
          are relative to the root, so this is pure arithmetic and needs
          no AES call. */
    {
        short ok_l = g_dialog_tree[ok_obj].ob_x;
        short ok_r = (short)(ok_l + g_dialog_tree[ok_obj].ob_width);
        short ca_l = g_dialog_tree[cancel_obj].ob_x;
        short ca_r = (short)(ca_l + g_dialog_tree[cancel_obj].ob_width);

        if (ok_l < 0 || ca_l < 0) return WP_FALSE;
        if (ok_r > ca_l) return WP_FALSE;                        /* overlap */
        if (ca_r > g_dialog_tree[0].ob_width) return WP_FALSE;   /* off the box */
    }

    /* 5. Rebuild SMALLER and re-assert. g_dialog_tree is static and
          shared, so a 2-option dialog following this 7-option one must
          not inherit the bigger tree's OF_LASTOB at object 10 -- the AES
          would scan five stale objects past the real end. This is the
          regression that a per-call "set LASTOB on cancel_obj" alone
          would NOT catch. */
    {
        static const char *const two_c[] = { "Speak now", "Cancel" };
        const u8 *two[2];
        u32 two_lens[2];
        short f2, ok2, cancel2;
        short lastob_count = 0;

        for (i = 0; i < 2; i++) {
            two[i] = (const u8 *)two_c[i];
            two_lens[i] = (u32)strlen(two_c[i]);
        }
        if (build_dialog_tree((const u8 *)"Dictate", 7, two, two_lens, 2, 0, WP_FALSE,
                               &f2, &ok2, &cancel2, &cx, &cy, &cw, &ch) != WP_OK) {
            return WP_FALSE;
        }
        if (cancel2 != 5) return WP_FALSE;   /* first_option(2) + 2 options + OK */
        for (i = 0; i < (u32)WP_DIALOG_OBJ_COUNT; i++) {
            if (g_dialog_tree[i].ob_flags & OF_LASTOB) lastob_count++;
        }
        if (lastob_count != 1) return WP_FALSE;
        if ((g_dialog_tree[cancel2].ob_flags & OF_LASTOB) == 0) return WP_FALSE;
    }

    return WP_TRUE;
}

/* -- plat_dialog_form: a GEM dialog with edit fields ---------------- */

/* One row per field: its label as a G_STRING, then its value -- a
   G_FTEXT edit field (the AES's own editing: typing, Backspace, Esc to
   clear, the cursor keys and Tab between fields) or, for a choice, a
   TOUCHEXIT button showing the current option that steps to the next
   when clicked, the cycle button GEM programs used where TOS 1.x has no
   popup menu. OK (the default) and Cancel under them. Built at run time
   like the choice dialog's tree, for the same reason (no resource
   compiler), and checked the same way (wp_atari_form_selfcheck). */
/* A TEDINFO's three pointers: char * in gemlib, a LONG holding the
   bank-$00 address in gem4xe's kit. */
#ifdef GEM4XE_APP_GEM_H
#  define TED_PTR(p) ((LONG)(p))
#else
#  define TED_PTR(p) (p)
#endif

static u32 g_form_index[PLAT_FORM_MAX_FIELDS];

static void form_obj(short index, short next, unsigned short type, unsigned short flags,
                     short x, short y, short w, short h)
{
    g_form_tree[index].ob_next = next;
    g_form_tree[index].ob_head = -1;
    g_form_tree[index].ob_tail = -1;
    g_form_tree[index].ob_type = type;
    g_form_tree[index].ob_flags = flags;
    g_form_tree[index].ob_state = OS_NORMAL;
    g_form_tree[index].ob_x = x;
    g_form_tree[index].ob_y = y;
    g_form_tree[index].ob_width = w;
    g_form_tree[index].ob_height = h;
}

static void form_choice_text(u32 i, const plat_form_field *f)
{
    const char *o = (f->options != NULL && g_form_index[i] < f->option_count) ? f->options[g_form_index[i]] : "";
    copy_bounded(g_form_choice[i], WP_FORM_CHOICE_W, (const u8 *)o, (u32)strlen(o));
}

/* The value object of field i, and back: 2 + 2i + 1. */
#define FORM_VALUE_OBJ(i) ((short)(3 + 2 * (short)(i)))

static wp_status build_form_tree(const u8 *title_utf8, u32 title_len,
                                 const plat_form_field *fields, u32 count,
                                 short *out_first_edit, short *out_ok, short *out_cancel,
                                 short *out_cx, short *out_cy, short *out_cw, short *out_ch)
{
    short char_w, char_h, box_w, box_h, content_w, y, row_h, ok, cancel, first_edit = 0;
    u32 i;

    if (count == 0 || count > PLAT_FORM_MAX_FIELDS || fields == NULL) return WP_RANGE;
    memset(g_form_tree, 0, sizeof(g_form_tree));   /* no stale OF_LASTOB (see build_dialog_tree) */
    graf_handle(&char_w, &char_h, &box_w, &box_h);
    WP_UNUSED(box_w);
    WP_UNUSED(box_h);

    for (i = 0; i < PLAT_FORM_TEXT_MAX; i++) {
        g_form_tmplt[i] = '_';
        g_form_valid[i] = 'X';                     /* any character */
    }
    g_form_tmplt[PLAT_FORM_TEXT_MAX] = '\0';
    g_form_valid[PLAT_FORM_TEXT_MAX] = '\0';
    copy_bounded(g_form_title, (u32)sizeof(g_form_title), title_utf8, title_len);

    ok = (short)(2 + 2 * (short)count);
    cancel = (short)(ok + 1);
    content_w = (short)((2 + WP_FORM_LABEL_W + PLAT_FORM_TEXT_MAX + 2) * char_w);
    row_h = (short)(char_h + 6);                   /* room for a button's border */

    form_obj(0, -1, G_BOX, OF_NONE, 0, 0, content_w, 0);
    g_form_tree[0].ob_head = 1;
    g_form_tree[0].ob_tail = cancel;
    g_form_tree[0].ob_state = OS_OUTLINED;
    OB_SPEC_SET_INDEX(g_form_tree[0], WP_DIALOG_BOX_SPEC);

    y = (short)(char_h / 2);
    form_obj(1, 2, G_STRING, OF_NONE, char_w, y, (short)(content_w - 2 * char_w), char_h);
    OB_SPEC_SET_STRING(g_form_tree[1], g_form_title);
    y = (short)(y + 2 * char_h);

    for (i = 0; i < count; i++) {
        short label = (short)(2 + 2 * (short)i), value = (short)(label + 1);
        short next = (short)(value + 1);
        const plat_form_field *f = &fields[i];
        const char *l = f->label != NULL ? f->label : "";

        copy_bounded(g_form_label[i], WP_FORM_LABEL_W, (const u8 *)l, (u32)strlen(l));
        form_obj(label, value, G_STRING, OF_NONE, (short)(2 * char_w), (short)(y + 3),
                 (short)((WP_FORM_LABEL_W - 1) * char_w), char_h);
        OB_SPEC_SET_STRING(g_form_tree[label], g_form_label[i]);
        g_form_index[i] = f->index;
        if (f->kind == PLAT_FORM_CHOICE) {
            form_choice_text(i, f);
            form_obj(value, next, G_BUTTON, (unsigned short)(OF_SELECTABLE | OF_TOUCHEXIT),
                     (short)((2 + WP_FORM_LABEL_W) * char_w), (short)(y + 2),
                     (short)(WP_FORM_CHOICE_W * char_w), char_h);
            OB_SPEC_SET_STRING(g_form_tree[value], g_form_choice[i]);
        } else {
            TEDINFO *t = &g_form_ted[i];
            u32 n = (f->text != NULL) ? (u32)strlen(f->text) : 0u;

            if (n > PLAT_FORM_TEXT_MAX) n = PLAT_FORM_TEXT_MAX;
            copy_bounded(g_form_text[i], PLAT_FORM_TEXT_MAX + 1, (const u8 *)(f->text != NULL ? f->text : ""), n);
            memset(t, 0, sizeof(*t));
            t->te_ptext = TED_PTR(g_form_text[i]);
            t->te_ptmplt = TED_PTR(g_form_tmplt);
            t->te_pvalid = TED_PTR(g_form_valid);
            t->te_font = 3;                        /* IBM: the system font */
            t->te_just = 0;                        /* TE_LEFT */
            t->te_color = 0x1180;                  /* black on white, opaque */
            t->te_thickness = 0;
            t->te_txtlen = PLAT_FORM_TEXT_MAX + 1;
            t->te_tmplen = PLAT_FORM_TEXT_MAX + 1;
            form_obj(value, next, G_FTEXT, OF_EDITABLE,
                     (short)((2 + WP_FORM_LABEL_W) * char_w), (short)(y + 3),
                     (short)(PLAT_FORM_TEXT_MAX * char_w), char_h);
            g_form_tree[value].ob_spec.tedinfo = t;
            if (first_edit == 0) first_edit = value;
        }
        y = (short)(y + row_h);
    }
    y = (short)(y + char_h / 2);
    form_obj(ok, cancel, G_BUTTON, (unsigned short)(OF_SELECTABLE | OF_EXIT | OF_DEFAULT),
             (short)(2 * char_w), y, (short)(8 * char_w), char_h);
    OB_SPEC_SET_STRING(g_form_tree[ok], g_dialog_ok_buf);
    form_obj(cancel, 0, G_BUTTON, (unsigned short)(OF_SELECTABLE | OF_EXIT | OF_LASTOB),
             (short)(content_w - 10 * char_w), y, (short)(8 * char_w), char_h);
    OB_SPEC_SET_STRING(g_form_tree[cancel], g_dialog_cancel_buf);
    g_form_tree[0].ob_height = (short)(y + char_h + char_h / 2);

    form_center(g_form_tree, out_cx, out_cy, out_cw, out_ch);
    *out_first_edit = first_edit;
    *out_ok = ok;
    *out_cancel = cancel;
    return WP_OK;
}

wp_status plat_dialog_form(plat_window *parent,
                            const u8 *title_utf8, u32 title_len,
                            plat_form_field *fields, u32 count,
                            wp_bool *out_cancelled)
{
    short first_edit, ok, cancel, cx, cy, cw, ch, hit;
    u32 i;
    wp_status st;

    WP_UNUSED(parent);
    st = build_form_tree(title_utf8, title_len, fields, count, &first_edit, &ok, &cancel,
                         &cx, &cy, &cw, &ch);
    if (st != WP_OK) return st;

    form_dial(FMD_START, cx, cy, 0, 0, cx, cy, cw, ch);
    objc_draw(g_form_tree, 0, MAX_DEPTH, cx, cy, cw, ch);
    for (;;) {
        /* first_edit is an OF_EDITABLE G_FTEXT, or 0 when there is none:
           form_do's second argument must be one or the other (#177). */
        hit = (short)(form_do(g_form_tree, first_edit) & 0x7FFF);
        if (hit >= 0 && hit < WP_FORM_OBJ_COUNT) g_form_tree[hit].ob_state &= (unsigned short)~OS_SELECTED;
        if (hit == ok || hit == cancel || hit < 3 || hit > FORM_VALUE_OBJ(count - 1)) break;
        /* A choice's button: the next option, drawn in place. */
        i = (u32)((hit - 3) / 2);
        if (fields[i].kind == PLAT_FORM_CHOICE && fields[i].option_count > 0) {
            g_form_index[i] = (g_form_index[i] + 1u) % fields[i].option_count;
            form_choice_text(i, &fields[i]);
        }
        objc_draw(g_form_tree, hit, MAX_DEPTH, cx, cy, cw, ch);
    }
    form_dial(FMD_FINISH, cx, cy, 0, 0, cx, cy, cw, ch);

    *out_cancelled = (wp_bool)(hit != ok);
    if (hit == ok) {
        for (i = 0; i < count; i++) {
            if (fields[i].kind == PLAT_FORM_CHOICE) {
                fields[i].index = g_form_index[i];
            } else if (fields[i].text != NULL && fields[i].text_cap > 0) {
                u32 n = (u32)strlen(g_form_text[i]);
                if (n > fields[i].text_cap - 1) n = fields[i].text_cap - 1;
                memcpy(fields[i].text, g_form_text[i], n);
                fields[i].text[n] = '\0';
            }
        }
    }
    return WP_OK;
}

/* build_form_tree's invariants, without form_do (the boundary every
   selfcheck here keeps): one OF_LASTOB, on the last object; the chain
   from the root's head ends at its tail; exactly the text fields are
   editable, each a G_FTEXT whose TEDINFO holds its own buffer, and the
   first of them is form_do's start; every choice is a TOUCHEXIT button;
   OK and Cancel apart and inside the box; and a smaller form after a
   bigger one inherits nothing. */
wp_bool wp_atari_form_selfcheck(void)
{
    static const char *const opts[] = { "Off", "5 minutes" };
    plat_form_field f[4];
    static char a[PLAT_FORM_TEXT_MAX + 1], b[PLAT_FORM_TEXT_MAX + 1], c[PLAT_FORM_TEXT_MAX + 1];
    short first_edit, ok, cancel, cx, cy, cw, ch, cur, steps, lastob;
    u32 i, n;

    strcpy(a, "C:\\LETTERS");
    b[0] = '\0';
    strcpy(c, "serial:aux:9600");
    memset(f, 0, sizeof(f));
    f[0].kind = PLAT_FORM_CHOICE; f[0].label = "Autosave"; f[0].options = opts; f[0].option_count = 2; f[0].index = 1;
    f[1].kind = PLAT_FORM_TEXT; f[1].label = "Documents folder"; f[1].text = a; f[1].text_cap = sizeof(a);
    f[2].kind = PLAT_FORM_TEXT; f[2].label = "Dictionary folder"; f[2].text = b; f[2].text_cap = sizeof(b);
    f[3].kind = PLAT_FORM_TEXT; f[3].label = "Offload link"; f[3].text = c; f[3].text_cap = sizeof(c);

    for (n = 4; n >= 2; n -= 2) {               /* four fields, then two */
        if (build_form_tree((const u8 *)"Settings", 8, f, n, &first_edit, &ok, &cancel,
                            &cx, &cy, &cw, &ch) != WP_OK) return WP_FALSE;
        if (ok != (short)(2 + 2 * n) || cancel != ok + 1) return WP_FALSE;
        lastob = 0;
        for (i = 0; i < (u32)WP_FORM_OBJ_COUNT; i++) {
            if (g_form_tree[i].ob_flags & OF_LASTOB) lastob++;
        }
        if (lastob != 1 || (g_form_tree[cancel].ob_flags & OF_LASTOB) == 0) return WP_FALSE;
        if (g_form_tree[0].ob_head != 1 || g_form_tree[0].ob_tail != cancel) return WP_FALSE;
        cur = 1;
        for (steps = 0; steps < WP_FORM_OBJ_COUNT && cur != cancel; steps++) cur = g_form_tree[cur].ob_next;
        if (cur != cancel || g_form_tree[cancel].ob_next != 0) return WP_FALSE;
        if (first_edit != FORM_VALUE_OBJ(1)) return WP_FALSE;      /* field 0 is the choice */
        if ((g_form_tree[first_edit].ob_flags & OF_EDITABLE) == 0) return WP_FALSE;
        for (i = 0; i < n; i++) {
            OBJECT *o = &g_form_tree[FORM_VALUE_OBJ(i)];
            wp_bool editable = (wp_bool)((o->ob_flags & OF_EDITABLE) != 0);
            if (f[i].kind == PLAT_FORM_TEXT) {
                if (!editable || o->ob_type != G_FTEXT) return WP_FALSE;
                if (o->ob_spec.tedinfo != &g_form_ted[i]) return WP_FALSE;
                if (g_form_ted[i].te_ptext != TED_PTR(g_form_text[i])) return WP_FALSE;
                if (strcmp(g_form_text[i], f[i].text) != 0) return WP_FALSE;
            } else {
                if (editable || o->ob_type != G_BUTTON) return WP_FALSE;
                if ((o->ob_flags & OF_TOUCHEXIT) == 0) return WP_FALSE;
                if (strcmp(g_form_choice[i], "5 minutes") != 0) return WP_FALSE;
            }
            if (o->ob_x + o->ob_width > g_form_tree[0].ob_width) return WP_FALSE;
        }
        for (i = 0; i < (u32)WP_FORM_OBJ_COUNT; i++) {          /* nothing else editable */
            if ((g_form_tree[i].ob_flags & OF_EDITABLE) && (i < 3 || i > (u32)FORM_VALUE_OBJ(n - 1) || (i & 1u) == 0))
                return WP_FALSE;
        }
        if (g_form_tree[ok].ob_x + g_form_tree[ok].ob_width > g_form_tree[cancel].ob_x) return WP_FALSE;
        if (g_form_tree[cancel].ob_x + g_form_tree[cancel].ob_width > g_form_tree[0].ob_width) return WP_FALSE;
        if (OB_SPEC_INDEX(g_form_tree[0]) != WP_DIALOG_BOX_SPEC) return WP_FALSE;
    }
    return WP_TRUE;
}
