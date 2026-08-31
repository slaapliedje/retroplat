#include <stddef.h>

#include <graphics/rastport.h>
#include <graphics/text.h>
#include <graphics/gfx.h>
#include <graphics/view.h>
#include <intuition/screens.h>
#include <utility/tagitem.h>
#include <proto/graphics.h>

#include "platform.h"
#include "utf8.h"
#include "metrics_amiga_internal.h"

/* T9.1a implemented only the encoding edge (plat_utf8_to_native/
   plat_native_to_utf8) -- clipboard_amiga.c needed it to link at all, so
   it couldn't wait for T9.2's real glyph-draw work. T9.2 (this revisit)
   adds plat_draw_text/plat_fill_rect/plat_gc_flush below, RastPort-based
   exactly like draw_atari.c is VDI-based, completing the pair per the
   M9 plan's architecture table.

   Unlike the Atari backend's ASCII-only '?' fallback for every codepoint
   above 0x7F (a documented, deliberate gap -- see docs/atari-platform.md),
   Amiga's native charset genuinely is ISO-8859-1 (Latin-1) from
   Kickstart 2.0's topaz.font onward -- this project's Amiga floor is
   already 2.04+ for MUI (see docs/amiga-platform.md), so codepoints
   0x80-0xFF map identically to native bytes 0x80-0xFF, no lookup table
   needed. Only codepoints above 0xFF (genuinely outside Latin-1) fall
   back to '?'. */

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

        if (cp <= 0xFF) {
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

    for (i = 0; i < len; i++) {
        u8 tmp[4];
        u32 n = utf8_encode((u32)native[i], tmp);
        u32 j;

        if (written + n > out_cap) break;
        for (j = 0; j < n; j++) out[written++] = tmp[j];
    }

    if (out_len) *out_len = written;
    return WP_OK;
}

/* r,g,b -> a real MUI pen index. Pens are screen-relative palette slots,
   not direct RGB, so (like Atari's rgb_to_vdi_color choosing between the
   low-res palette's two fixed G_BLACK/G_WHITE entries) this maps to
   whichever of the window's own DrawInfo TEXTPEN/BACKGROUNDPEN pens is
   closer by the same luminance threshold -- callers (app_shell.c) have
   only ever asked for black text on the theme's normal background so
   far, same scope note draw_atari.c's own version carries. */
static UBYTE rgb_to_pen(struct DrawInfo *dri, u8 r, u8 g, u8 b)
{
    u32 luminance = (u32)r + (u32)g + (u32)b;
    return (UBYTE)dri->dri_Pens[(luminance < 384) ? TEXTPEN : BACKGROUNDPEN];
}

wp_status plat_draw_text(plat_gc *gc, plat_font *f, i32 x, i32 y,
                          const u8 *utf8, u32 len, u8 r, u8 g, u8 b)
{
    amiga_gc *agc = (amiga_gc *)gc;
    amiga_font *af = (amiga_font *)f;
    char buf[256];
    u32 n, out_len, unmapped;
    wp_status st;

    if (agc == NULL || af == NULL || af->tf == NULL) return WP_ERR;

    n = (len < sizeof(buf)) ? len : (u32)sizeof(buf);
    st = plat_utf8_to_native(utf8, n, (u8 *)buf, (u32)sizeof(buf), &out_len, &unmapped);
    if (st != WP_OK) return st;
    if (out_len == 0) return WP_OK;

    /* Font attributes are re-selected here (unlike draw_atari.c, which
       leaves this to the caller's own VDI-attribute state) because a
       RastPort's current font is per-RastPort state that app_shell.c's
       caller doesn't otherwise track between lines of potentially
       different styles -- see paint_document's cached_style pattern,
       mirrored by app_shell.c's own painting loop for this backend. */
    SetFont(agc->rp, af->tf);
    SetAPen(agc->rp, rgb_to_pen(agc->dri, r, g, b));
    SetBPen(agc->rp, (UBYTE)agc->dri->dri_Pens[BACKGROUNDPEN]);
    SetDrMd(agc->rp, JAM2);
    Move(agc->rp, (LONG)x, (LONG)y);
    Text(agc->rp, (STRPTR)buf, (ULONG)out_len);
    return WP_OK;
}

wp_status plat_fill_rect(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                          u8 r, u8 g, u8 b)
{
    amiga_gc *agc = (amiga_gc *)gc;

    if (agc == NULL) return WP_ERR;

    SetAPen(agc->rp, rgb_to_pen(agc->dri, r, g, b));
    RectFill(agc->rp, (LONG)x, (LONG)y, (LONG)(x + w - 1), (LONG)(y + h - 1));
    return WP_OK;
}

void plat_gc_flush(plat_gc *gc)
{
    WP_UNUSED(gc);
}

/* M15 T15.6b: real graphics.library indexed-bitmap blit. WritePixelArray8
   writes raw screen PEN NUMBERS, not RGB or local palette indices (it does
   no color translation of its own) -- so the image's own <=16-entry
   palette must first be resolved into real pens on the REAL, runtime-
   queried screen (agc->screen, never fixed at compile time -- M9's own
   finding, see amiga_screen_color_count()) via ObtainBestPen, and every
   packed-nibble pixel remapped through that table before the call.
   WritePixelArray8 also requires a temporary scratch RastPort matching the
   destination's own depth (a real, mandatory parameter, not optional) --
   built once here via AllocBitMap/InitRastPort and freed afterward.
   Nearest-neighbor samples from src_width_px/src_height_px into the
   requested w x h box -- in practice these are always equal in this
   milestone (display_width_twips is derived directly from width_px with
   no additional scaling), but a caller passing a slightly different w/h
   after twips<->px rounding still gets a correct, if approximate, result,
   the same allowance plat_measure_text's own callers already have. */
wp_status plat_draw_image(plat_gc *gc, i32 x, i32 y, i32 w, i32 h,
                           const u8 *pixels, i32 src_width_px, i32 src_height_px,
                           const u8 palette[][3], u8 palette_count)
{
    amiga_gc *agc = (amiga_gc *)gc;
    struct ColorMap *cm;
    LONG pen_map[16];
    UBYTE *array;
    struct RastPort temprp;
    struct BitMap *temp_bm;
    ULONG depth;
    i32 out_x, out_y;
    u8 i;
    wp_status result = WP_OK;

    if (agc == NULL || agc->screen == NULL || pixels == NULL) return WP_ERR;
    if (w <= 0 || h <= 0 || src_width_px <= 0 || src_height_px <= 0) return WP_ERR;
    if (palette_count < 1 || palette_count > 16) return WP_ERR;

    cm = agc->screen->ViewPort.ColorMap;

    for (i = 0; i < palette_count; i++) {
        ULONG r32 = ((ULONG)palette[i][0] << 24) | ((ULONG)palette[i][0] << 16) |
                    ((ULONG)palette[i][0] << 8) | (ULONG)palette[i][0];
        ULONG g32 = ((ULONG)palette[i][1] << 24) | ((ULONG)palette[i][1] << 16) |
                    ((ULONG)palette[i][1] << 8) | (ULONG)palette[i][1];
        ULONG b32 = ((ULONG)palette[i][2] << 24) | ((ULONG)palette[i][2] << 16) |
                    ((ULONG)palette[i][2] << 8) | (ULONG)palette[i][2];

        pen_map[i] = ObtainBestPen(cm, r32, g32, b32, TAG_DONE);
        if (pen_map[i] < 0) {
            u8 j;
            for (j = 0; j < i; j++) ReleasePen(cm, pen_map[j]);
            return WP_ERR;
        }
    }

    array = (UBYTE *)mem_alloc((u32)w * (u32)h);
    if (array == NULL) {
        for (i = 0; i < palette_count; i++) ReleasePen(cm, pen_map[i]);
        return WP_NOMEM;
    }

    for (out_y = 0; out_y < h; out_y++) {
        i32 src_y = (out_y * src_height_px) / h;
        u32 src_row_bytes = ((u32)src_width_px + 1u) / 2u;
        const u8 *src_row = pixels + (u32)src_y * src_row_bytes;

        for (out_x = 0; out_x < w; out_x++) {
            i32 src_x = (out_x * src_width_px) / w;
            u8 byte = src_row[src_x / 2];
            u8 nibble = (u8)((src_x % 2 == 0) ? (byte >> 4) : (byte & 0x0F));

            if (nibble >= palette_count) nibble = 0;
            array[out_y * w + out_x] = (UBYTE)pen_map[nibble];
        }
    }

    depth = GetBitMapAttr(agc->rp->BitMap, BMA_DEPTH);
    temp_bm = AllocBitMap((ULONG)w, 1, depth, 0, agc->rp->BitMap);
    if (temp_bm == NULL) {
        mem_free(array);
        for (i = 0; i < palette_count; i++) ReleasePen(cm, pen_map[i]);
        return WP_NOMEM;
    }
    InitRastPort(&temprp);
    temprp.BitMap = temp_bm;

    /* WritePixelArray8 does NOT return a status code -- per the Amiga ROM
       Kernel Reference Manual it returns the actual pixel count copied,
       which should equal (xstop-xstart+1)*(ystop-ystart+1) == w*h on a
       full success. A prior version of this code checked "!= 0", which
       misread every successful call (any nonzero pixel count) as a
       failure. */
    if ((u32)WritePixelArray8(agc->rp, (ULONG)x, (ULONG)y, (ULONG)(x + w - 1),
                               (ULONG)(y + h - 1), array, &temprp) !=
        (u32)w * (u32)h) {
        result = WP_ERR;
    }

    FreeBitMap(temp_bm);
    mem_free(array);
    for (i = 0; i < palette_count; i++) ReleasePen(cm, pen_map[i]);

    return result;
}
