#include <stddef.h>
#include <string.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include "platform.h"
#include "metrics_mac_internal.h"

/* Phase 3: real QuickDraw font metrics, replacing the Phase 2
   WP_UNSUPPORTED stubs this file used to hold (those existed only so
   linebreak.o's wp_paragraph_relayout could link -- see the old
   comment's own explanation, now obsolete: relayout genuinely IS called
   now, by app_shell.c's relayout_document). */

#define MAC_TWIPS_PER_POINT 20

i32 mac_twips_to_px(i32 twips)
{
    return twips / MAC_TWIPS_PER_POINT;
}

i32 mac_px_to_twips(i32 px)
{
    return px * MAC_TWIPS_PER_POINT;
}

/* A lazily-initialized, minimally-sized (16x16, depth 1) real bitmap-
   backed GrafPort used ONLY for measurement (GetFontInfo/TextWidth),
   never for drawing -- draw_mac.c's plat_draw_text/plat_fill_rect draw
   into the real window's own port instead. Mirrors metrics_amiga.c's
   own g_metrics_rp exactly, including the reasoning: relying on
   whatever port happens to be current (or none at all, before
   app_shell.c's window exists) is a real, previously-hit portability
   risk elsewhere in this project (see docs/amiga-platform.md's account
   of a bitmap-less measuring RastPort corrupting live document bytes on
   that target) -- a real, owned, always-valid bitmap removes the whole
   class of "passing QuickDraw an incompletely-initialized structure"
   risk, not just one call path. Never freed: same process-lifetime-held
   resource shape metrics_atari.c's g_metrics_vdi already has. */
#define WP_METRICS_PORT_W 16
#define WP_METRICS_PORT_H 16
#define WP_METRICS_ROWBYTES 2 /* 16 px wide, 1 bit deep -> 2 bytes/row */

static GrafPort g_metrics_port;
static Ptr g_metrics_bits = NULL;
static wp_bool g_metrics_ready = WP_FALSE;

static wp_bool ensure_metrics_port(void)
{
    BitMap bm;

    if (g_metrics_ready) return (wp_bool)(g_metrics_bits != NULL);
    g_metrics_ready = WP_TRUE; /* set first: a failed attempt below must
                                  not retry (and re-allocate) on every
                                  single call. */

    g_metrics_bits = NewPtr((Size)(WP_METRICS_ROWBYTES * WP_METRICS_PORT_H));
    if (g_metrics_bits == NULL) return WP_FALSE;

    bm.baseAddr = g_metrics_bits;
    bm.rowBytes = WP_METRICS_ROWBYTES;
    SetRect(&bm.bounds, 0, 0, WP_METRICS_PORT_W, WP_METRICS_PORT_H);

    OpenPort(&g_metrics_port);
    SetPortBits(&bm);
    g_metrics_port.portRect = bm.bounds;
    return WP_TRUE;
}

static void select_font(mac_font *f)
{
    TextFont(f->fontNum);
    TextSize(f->size);
    TextFace((short)f->style);
}

wp_status plat_font_open(const u8 *utf8_font_name, u32 name_len,
                          i32 size_twips, u16 style_flags, plat_font **out)
{
    mac_font *f;
    unsigned char pname[256];
    short familyID = 0;
    u32 n;

    if (out == NULL) return WP_ERR;

    /* The engine always requests the platform's default font today (an
       empty name -- see linebreak.c's measure_and_height comment), the
       same boundary metrics_atari.c/metrics_amiga.c both respect --
       GetFNum is only even called when a real name is actually given. */
    n = name_len;
    if (n > 255) n = 255;
    if (n > 0) {
        pname[0] = (unsigned char)n;
        memcpy(pname + 1, utf8_font_name, n);
        GetFNum(pname, &familyID); /* leaves familyID at 0 (systemFont) if not found */
    }

    f = (mac_font *)mem_alloc((u32)sizeof(mac_font));
    if (f == NULL) return WP_NOMEM;

    f->fontNum = familyID;
    f->size = (short)mac_twips_to_px(size_twips);
    if (f->size < 1) f->size = 1;
    f->style = 0;
    if (style_flags & (u16)PLAT_STYLE_BOLD)      f->style |= bold;
    if (style_flags & (u16)PLAT_STYLE_ITALIC)    f->style |= italic;
    if (style_flags & (u16)PLAT_STYLE_UNDERLINE) f->style |= underline;
    /* PLAT_STYLE_STRIKE has no plain QuickDraw Style bit, and the
       document model's own SUPER/SUB never reach this seam -- not mapped,
       the same documented gap metrics_atari.c/metrics_amiga.c each
       already have for their own target's effects bits. */

    *out = (plat_font *)f;
    return WP_OK;
}

void plat_font_close(plat_font *f)
{
    if (f != NULL) mem_free(f);
}

wp_status plat_font_metrics_get(plat_font *f, plat_font_metrics *out)
{
    mac_font *mf = (mac_font *)f;
    GrafPtr savePort;
    FontInfo info;

    if (mf == NULL || out == NULL) return WP_ERR;
    if (!ensure_metrics_port()) return WP_ERR;

    GetPort(&savePort);
    SetPort(&g_metrics_port);
    select_font(mf);
    GetFontInfo(&info);
    SetPort(savePort);

    out->ascent  = mac_px_to_twips((i32)info.ascent);
    out->descent = mac_px_to_twips((i32)info.descent);
    out->leading = mac_px_to_twips((i32)info.leading);
    return WP_OK;
}

wp_status plat_measure_text(plat_font *f, const u8 *utf8, u32 len,
                             i32 *width_twips_out)
{
    mac_font *mf = (mac_font *)f;
    GrafPtr savePort;
    char buf[256];
    u32 n, out_len, unmapped;
    short px;
    wp_status st;

    if (mf == NULL || width_twips_out == NULL) return WP_ERR;
    if (!ensure_metrics_port()) return WP_ERR;

    n = (len < (u32)sizeof(buf)) ? len : (u32)sizeof(buf);
    st = plat_utf8_to_native(utf8, n, (u8 *)buf, (u32)sizeof(buf), &out_len, &unmapped);
    if (st != WP_OK) return st;

    GetPort(&savePort);
    SetPort(&g_metrics_port);
    select_font(mf);
    px = TextWidth(buf, 0, (short)out_len);
    SetPort(savePort);

    *width_twips_out = mac_px_to_twips((i32)px);
    return WP_OK;
}
