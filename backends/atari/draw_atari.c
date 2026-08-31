#include <stddef.h>
#include <string.h>
#include "platform.h"
#include "utf8.h"
#include "metrics_atari_internal.h"

/* No real Atari-ST-high-ASCII mapping table yet -- ASCII (0x00-0x7F)
   passes through unchanged (already charset-compatible), and every
   other codepoint (regardless of its UTF-8 byte length) becomes exactly
   one '?' fallback byte, counted in *unmapped_count. Sufficient and
   correct for this milestone's ASCII sample document; a real high-ASCII
   table is a documented gap, not a silent one -- see
   docs/atari-platform.md. */
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

/* Inverse direction: native Atari charset input (keyboard/clipboard/file)
   to UTF-8. Same ASCII-only scope for now -- bytes >= 0x80 aren't a
   valid single-byte-to-codepoint mapping yet, so they're dropped in
   favor of a literal '?' rather than emitting a wrong codepoint. */
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

/* r,g,b -> nearest ST palette index. Only ever asked to draw black text
   on a white background at this milestone, so a simple luminance
   threshold against the two fixed low-res-palette entries (G_WHITE=0,
   G_BLACK=1) is enough; a real nearest-color search over the full
   16-entry palette is unneeded until color text is a real feature. */
static short rgb_to_vdi_color(u8 r, u8 g, u8 b)
{
    u32 luminance = (u32)r + (u32)g + (u32)b;
    return (luminance < 384) ? G_BLACK : G_WHITE;
}

wp_status plat_draw_text(plat_gc *gc, plat_font *f, i32 x, i32 y,
                          const u8 *utf8, u32 len, u8 r, u8 g, u8 b)
{
    atari_gc *agc = (atari_gc *)gc;
    const atari_font *af = (const atari_font *)f;
    char buf[256];
    u32 n, out_len, unmapped;
    wp_status st;

    if (agc == NULL || af == NULL) return WP_ERR;

    n = (len < sizeof(buf) - 1) ? len : (u32)(sizeof(buf) - 1);
    st = plat_utf8_to_native(utf8, n, (u8 *)buf, (u32)(sizeof(buf) - 1), &out_len, &unmapped);
    if (st != WP_OK) return st;
    buf[out_len] = '\0';

    /* v_gtext draws with ITS OWN handle's (agc->vdi) current text
       attributes -- a real, distinct handle from g_metrics_vdi, which
       is what plat_measure_text/plat_font_metrics_get select during
       layout (VDI text attributes are per-workstation-handle, not
       global). This was NOT actually reaching a different result in
       practice on the real/EmuTOS environments tested (both handles
       reported the identical selected font, so this alone isn't what
       causes the ST-High rendering issue documented in
       docs/atari-platform.md's own ST-High section) -- but it's still
       the only correct way to guarantee agc->vdi draws at the font
       plat_font_open was actually asked for, rather than relying on
       both handles coincidentally agreeing, so it stays. */
    atari_select_font(agc->vdi, af);
    vst_color(agc->vdi, rgb_to_vdi_color(r, g, b));
    v_gtext(agc->vdi, (short)x, (short)y, buf);
    return WP_OK;
}

wp_status plat_fill_rect(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                          u8 r, u8 g, u8 b)
{
    atari_gc *agc = (atari_gc *)gc;
    short pxy[4];

    if (agc == NULL) return WP_ERR;

    pxy[0] = (short)x;
    pxy[1] = (short)y;
    pxy[2] = (short)(x + w - 1);
    pxy[3] = (short)(y + h - 1);

    vsf_interior(agc->vdi, FIS_SOLID);
    vsf_color(agc->vdi, rgb_to_vdi_color(r, g, b));
    v_bar(agc->vdi, pxy);
    return WP_OK;
}

/* M15 Phase 5b: real ST VDI raster-copy blit. Two hard, ST-specific
   constraints shape this (see docs/atari-platform.md and this
   milestone's own research):

   (1) An MFDB source form in "device-specific" format (fd_stand=0) must
   be genuine word-interleaved ST bitplanes -- NOT the packed-nibble-
   per-byte chunky format the wire protocol/gateway actually produce
   (docs/formats/offload-protocol.md's OFFLOAD_IMAGE section) -- so
   every pixel is unpacked and re-planed into a scratch buffer here.
   Standard ST convention: within each 16-pixel word group, bit 15 is
   the LEFTMOST pixel; plane N holds bit N of each pixel's color index
   (plane 0 = LSB).

   (2) The ST's palette is a small, GLOBAL, hardware-indexed table
   (vs_color reprograms real palette registers by index) -- unlike
   Amiga's per-request ObtainBestPen, which allocates from a shared but
   independent pen pool. Indices 0/1 already carry the G_WHITE/G_BLACK
   this backend's own plat_draw_text/plat_fill_rect use for every other
   on-screen pixel (rgb_to_vdi_color) -- reprogramming them would
   recolor existing text/UI everywhere on screen, not just this image's
   box. So this permanently reserves pens 0/1 and only ever programs
   pens 2..(colors-1) for image content. The real color count (2 mono/
   4 Medium/16 Low) is queried at runtime via atari_screen_max_colors()
   (VDI's own work_out[13], the same call metrics_atari.c already makes
   for DPI) rather than assumed -- do_insert_image already sized its
   OFFLOAD_IMAGE request against this same real budget. A genuinely
   mono (2-color) screen has no spare pen at all once 0/1 are reserved,
   so this honestly fails (WP_ERR) rather than corrupting the two
   existing text colors -- the caller already falls back to a solid
   palette[0] rect in that case, the same allowance a missing/unreadable
   cache file already gets. */
wp_status plat_draw_image(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                           const u8 *pixels, i32 src_width_px, i32 src_height_px,
                           const u8 palette[][3], u8 palette_count)
{
    atari_gc *agc = (atari_gc *)gc;
    i32 colors, nplanes;
    i32 wdwidth;
    u8 pen_for[16];
    u8 i;
    i32 px, py;
    u16 *planebuf;
    u32 planebuf_words, row_words, src_row_bytes;
    MFDB src_mfdb, dst_mfdb;
    short pxy[8];

    if (agc == NULL || pixels == NULL) return WP_ERR;
    if (w <= 0 || h <= 0 || src_width_px <= 0 || src_height_px <= 0) return WP_ERR;
    if (palette_count < 1 || palette_count > 16) return WP_ERR;

    colors = atari_screen_max_colors();
    nplanes = 0;
    while ((1L << nplanes) < colors && nplanes < 8) nplanes++;
    if (nplanes < 2) return WP_ERR; /* mono: no spare pen once 0/1 reserved */

    if ((i32)palette_count > colors - 2) palette_count = (u8)(colors - 2);

    for (i = 0; i < palette_count; i++) {
        short rgb[3];
        rgb[0] = (short)(((i32)palette[i][0] * 1000) / 255);
        rgb[1] = (short)(((i32)palette[i][1] * 1000) / 255);
        rgb[2] = (short)(((i32)palette[i][2] * 1000) / 255);
        vs_color(agc->vdi, (short)(i + 2), rgb);
        pen_for[i] = (u8)(i + 2);
    }

    wdwidth = (src_width_px + 15) / 16;
    row_words = (u32)wdwidth * (u32)nplanes;
    planebuf_words = row_words * (u32)src_height_px;
    planebuf = (u16 *)mem_alloc(planebuf_words * (u32)sizeof(u16));
    if (planebuf == NULL) return WP_NOMEM;
    memset(planebuf, 0, planebuf_words * (u32)sizeof(u16));

    src_row_bytes = ((u32)src_width_px + 1u) / 2u;
    for (py = 0; py < src_height_px; py++) {
        const u8 *src_row = pixels + (u32)py * src_row_bytes;
        u16 *plane_row = planebuf + (u32)py * row_words;

        for (px = 0; px < src_width_px; px++) {
            u8 byte = src_row[px / 2];
            u8 nibble = (u8)((px % 2 == 0) ? (byte >> 4) : (byte & 0x0F));
            u8 pen;
            i32 word_idx = px / 16;
            i32 bit_idx = 15 - (px % 16);
            i32 plane;

            if (nibble >= palette_count) nibble = 0;
            pen = pen_for[nibble];

            for (plane = 0; plane < nplanes; plane++) {
                if (pen & (1 << plane)) {
                    u16 *w_ptr = &plane_row[(u32)word_idx * (u32)nplanes + (u32)plane];
                    *w_ptr = (u16)(*w_ptr | (1u << bit_idx));
                }
            }
        }
    }

    memset(&src_mfdb, 0, sizeof(src_mfdb));
    src_mfdb.fd_addr = planebuf;
    src_mfdb.fd_w = (short)src_width_px;
    src_mfdb.fd_h = (short)src_height_px;
    src_mfdb.fd_wdwidth = (short)wdwidth;
    src_mfdb.fd_stand = 0;
    src_mfdb.fd_nplanes = (short)nplanes;

    memset(&dst_mfdb, 0, sizeof(dst_mfdb)); /* fd_addr=0 -> real screen, VDI fills in the rest */

    pxy[0] = 0;
    pxy[1] = 0;
    pxy[2] = (short)(src_width_px - 1);
    pxy[3] = (short)(src_height_px - 1);
    pxy[4] = (short)x;
    pxy[5] = (short)y;
    pxy[6] = (short)(x + w - 1);
    pxy[7] = (short)(y + h - 1);

    vro_cpyfm(agc->vdi, S_ONLY, pxy, &src_mfdb, &dst_mfdb);

    mem_free(planebuf);
    return WP_OK;
}

void plat_gc_flush(plat_gc *gc)
{
    WP_UNUSED(gc);
}
