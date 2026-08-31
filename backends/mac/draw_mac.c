#include <stddef.h>
#include <string.h>
#include <Quickdraw.h>
#include "platform.h"
#include "utf8.h"
#include "metrics_mac_internal.h"

/* plat_utf8_to_native/plat_native_to_utf8 were pulled forward to Phase 1
   because the clipboard and file seams needed them to link -- same
   reasoning draw_amiga.c gives for doing the identical pull-forward in
   M9 Phase 1.

   Unlike Amiga's ISO-8859-1 (a genuine 1:1 identity map for 0x80-0xFF,
   not real extra work), Mac OS Roman is its own idiosyncratic ordering
   with no simple arithmetic relationship to Unicode -- a real mapping
   table remains future work, not assumed here. This follows Atari's own
   precedent instead: ASCII (0x00-0x7F) passes through unchanged, and
   every other codepoint becomes a single '?' fallback byte, counted in
   *unmapped_count. A documented gap, not a silent one. */

wp_status plat_utf8_to_native(const u8 *utf8, u32 len,
                               u8 *out, u32 out_cap, u32 *out_len,
                               u32 *unmapped_count)
{
    u32 pos = 0;
    u32 written = 0;
    u32 unmapped = 0;

    if (utf8 == NULL || out == NULL) return WP_ERR;

    while (pos < len && written < out_cap) {
        u32 cp;
        u32 n = utf8_decode(utf8 + pos, len - pos, &cp);

        if (n == 0) {
            out[written++] = '?';
            unmapped++;
            pos++;
            continue;
        }

        if (cp <= 0x7F) {
            out[written++] = (u8)cp;
        } else {
            out[written++] = '?';
            unmapped++;
        }
        pos += n;
    }

    if (out_len) *out_len = written;
    if (unmapped_count) *unmapped_count = unmapped;
    return WP_OK;
}

wp_status plat_native_to_utf8(const u8 *native, u32 len,
                               u8 *out, u32 out_cap, u32 *out_len)
{
    u32 i;
    u32 written = 0;

    if (native == NULL || out == NULL) return WP_ERR;

    for (i = 0; i < len && written < out_cap; i++) {
        out[written++] = (native[i] <= 0x7F) ? native[i] : (u8)'?';
    }

    if (out_len) *out_len = written;
    return WP_OK;
}


/* -- Glyph draw (Phase 3): real QuickDraw, drawing into whatever real
   window the caller's mac_gc names (app_shell.c's own g_win during a
   real updateEvt repaint). Only ever asked to draw black text on a
   white background so far -- every fixture ROM boots into a 1-bit
   QuickDraw port by default, and color isn't a real feature on any
   backend yet -- so r/g/b are accepted but unused, same simplification
   draw_atari.c's own rgb_to_vdi_color makes explicit for its target. */

wp_status plat_draw_text(plat_gc *gc, plat_font *f, i32 x, i32 y,
                          const u8 *utf8, u32 len, u8 r, u8 g, u8 b)
{
    mac_gc *mgc = (mac_gc *)gc;
    mac_font *mf = (mac_font *)f;
    char buf[256];
    u32 n, out_len, unmapped;
    wp_status st;

    WP_UNUSED(r);
    WP_UNUSED(g);
    WP_UNUSED(b);

    if (mgc == NULL || mf == NULL) return WP_ERR;

    n = (len < (u32)sizeof(buf)) ? len : (u32)sizeof(buf);
    st = plat_utf8_to_native(utf8, n, (u8 *)buf, (u32)sizeof(buf), &out_len, &unmapped);
    if (st != WP_OK) return st;

    SetPort(mgc->window);
    TextFont(mf->fontNum);
    TextSize(mf->size);
    TextFace((short)mf->style);
    MoveTo((short)x, (short)y);
    DrawText(buf, 0, (short)out_len);
    return WP_OK;
}

/* r,g,b -> nearest 1-bit QuickDraw pattern. PaintRect always fills with
   the port's CURRENT pen pattern (default black, set once by InitGraf
   and never otherwise touched by this file) -- a real bug, not a
   theoretical one, found by testing the help pager on-target: it asks
   for a WHITE background (0xFF,0xFF,0xFF, platform/common/help_view.c)
   to clear its viewport, which this function previously painted BLACK
   regardless, since it ignored r/g/b entirely and always used
   whatever pattern was already current. The main document body never
   surfaced this, since it clears its own background via a direct
   EraseRect call (always white, by QuickDraw's own contract) rather
   than through this seam. The pen is always restored to black
   afterward, since draw_document's own cursor-marker PaintRect call
   (and any future direct PaintRect caller) assumes that default.

   TWO tiers, thresholded at 384 -- deliberately the identical rule
   draw_atari.c's rgb_to_vdi_color uses for the ST's 2-tone palette,
   rather than a Mac-specific variant.

   There WAS a third tier here (qd.ltGray for the 96..224 band), added so
   the status line's and help pager's 0xC0 gray bars would "render
   distinctly from both pure black and pure white". It was removed
   because on a 1-bit screen there is no gray: 0xC0 became a dither, and
   BOTH of this project's only two 0xC0 callers draw BLACK TEXT ON TOP OF
   IT (platform/common/statusline.c:44, whose own comment asks for "a
   legible neutral background for black text", and help_view.c:310's
   selected-link highlight). Small black glyphs over a dither on 1-bit
   are mush -- verified on-target under BasiliskII, where the vi mode
   indicator and the ":"/"/" command line were unreadable without
   zooming to 500%. The tier's stated purpose was in direct conflict with
   its only two real uses.

   Dropping it did cost something, which is now paid for properly rather
   than waved away. The help pager's 0xC0 rect was the ONLY cue for which
   link is selected -- its "> " prefix was drawn on every link, not just
   the selected one, so white-filling that rect would have made selection
   invisible here. (Atari, thresholding at this same 384, had that bug
   all along: selection has never been visible on the ST.) So
   help_view.c now puts the selection in the TEXT -- "> " on the selected
   link, spaces on the rest -- which fixes both backends and leaves this
   seam free to do the simple, correct thing. */
static void select_fill_pattern(u8 r, u8 g, u8 b)
{
    u32 luminance = (u32)r + (u32)g + (u32)b;

    if (luminance < 384u) {
        PenPat(&qd.black);
    } else {
        PenPat(&qd.white);
    }
}

wp_status plat_fill_rect(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                          u8 r, u8 g, u8 b)
{
    mac_gc *mgc = (mac_gc *)gc;
    Rect rect;

    if (mgc == NULL) return WP_ERR;

    SetPort(mgc->window);
    SetRect(&rect, (short)x, (short)y, (short)(x + w), (short)(y + h));
    select_fill_pattern(r, g, b);
    PaintRect(&rect);
    PenPat(&qd.black);
    return WP_OK;
}

/* M15 Phase 5b: real QuickDraw CopyBits blit, dithered down to this
   backend's hard-capped 1-bit tier (a plain NewWindow, not NewCWindow --
   true color is out of scope). Each pixel's palette RGB is reduced to
   black/white via a 4x4 ordered (Bayer) dither -- extending
   select_fill_pattern's own per-solid-fill luminance-threshold trick to
   run per pixel instead of a single flat threshold, so a multi-color
   source image still reads as recognizable shading rather than one flat
   gray. Builds a scratch, manually-populated 1-bit BitMap (mirrors
   metrics_mac.c's own ensure_metrics_port() precedent for constructing
   a BitMap by hand) and blits it via CopyBits -- WindowPtr IS a GrafPtr,
   so &mgc->window->portBits is directly usable as the destination
   BitMap*, no Carbon-only GetPortBitMapForCopyBits needed. */
static const u8 g_mac_bayer4x4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

wp_status plat_draw_image(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                           const u8 *pixels, i32 src_width_px, i32 src_height_px,
                           const u8 palette[][3], u8 palette_count)
{
    mac_gc *mgc = (mac_gc *)gc;
    BitMap src_bm;
    Rect src_rect, dst_rect;
    u32 row_bytes, buf_len, src_row_bytes;
    u8 *bits;
    i32 px, py;

    if (mgc == NULL || mgc->window == NULL || pixels == NULL) return WP_ERR;
    if (w <= 0 || h <= 0 || src_width_px <= 0 || src_height_px <= 0) return WP_ERR;
    if (palette_count < 1 || palette_count > 16) return WP_ERR;

    /* Word-aligned rowBytes -- a real QuickDraw 1-bit BitMap requirement,
       same "round up, no partial words" shape do_insert_image's own
       packed-4-bit row math already follows for a different bit depth. */
    row_bytes = (u32)(((src_width_px + 15) / 16) * 2);
    buf_len = row_bytes * (u32)src_height_px;
    bits = (u8 *)mem_alloc(buf_len);
    if (bits == NULL) return WP_NOMEM;
    memset(bits, 0, buf_len);

    src_row_bytes = ((u32)src_width_px + 1u) / 2u;
    for (py = 0; py < src_height_px; py++) {
        const u8 *src_row = pixels + (u32)py * src_row_bytes;
        u8 *dst_row = bits + (u32)py * row_bytes;

        for (px = 0; px < src_width_px; px++) {
            u8 byte = src_row[px / 2];
            u8 nibble = (u8)((px % 2 == 0) ? (byte >> 4) : (byte & 0x0F));
            u32 luminance_avg, threshold;

            if (nibble >= palette_count) nibble = 0;
            luminance_avg = ((u32)palette[nibble][0] + (u32)palette[nibble][1] +
                              (u32)palette[nibble][2]) / 3u;
            /* Standard 0-15 -> 0-255 Bayer scaling (val*16+8): keeps the
               max threshold at 248, strictly below a pure-white pixel's
               luminance of 255, so solid white regions never misfire
               black at any of the 16 dither cells. */
            threshold = (u32)g_mac_bayer4x4[py & 3][px & 3] * 16u + 8u;

            /* 1-bit QuickDraw convention: a set bit is BLACK. */
            if (luminance_avg <= threshold) {
                dst_row[px / 8] |= (u8)(0x80 >> (px % 8));
            }
        }
    }

    src_bm.baseAddr = (Ptr)bits;
    src_bm.rowBytes = (short)row_bytes;
    SetRect(&src_bm.bounds, 0, 0, (short)src_width_px, (short)src_height_px);

    SetPort(mgc->window);
    SetRect(&src_rect, 0, 0, (short)src_width_px, (short)src_height_px);
    SetRect(&dst_rect, (short)x, (short)y, (short)(x + w), (short)(y + h));

    CopyBits(&src_bm, &mgc->window->portBits, &src_rect, &dst_rect, srcCopy, NULL);

    mem_free(bits);
    return WP_OK;
}

void plat_gc_flush(plat_gc *gc)
{
    WP_UNUSED(gc);
}
