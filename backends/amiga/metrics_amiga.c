#include <stddef.h>
#include <string.h>

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/text.h>
#include <graphics/rastport.h>
#include <graphics/displayinfo.h>
#include <intuition/screens.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/diskfont.h>

#include "platform.h"
#include "metrics_amiga_internal.h"

/* T9.2: real graphics.library/diskfont.library metrics, replacing the
   T9.1a WP_UNSUPPORTED stubs this file used to hold (those existed only
   so linebreak.o's wp_paragraph_relayout could link -- see the old
   comment's own explanation, now obsolete: relayout genuinely IS called
   now, by app_shell.c's relayout_document). */

static i32 g_dpi_x = 72;
static i32 g_dpi_y = 72;
static wp_bool g_dpi_queried = WP_FALSE;

/* The current public (Workbench) screen's own default font name, cached
   at first query -- used as the ta_Name for every plat_font_open request
   below (the engine never sets a real font name yet -- see linebreak.c's
   measure_and_height comment, "always requests the platform's default
   font", the same boundary metrics_atari.c respects). Falls back to the
   literal "topaz.font" (guaranteed ROM-resident on every real Amiga)
   if the screen or its Font pointer can't be queried. */
#define WP_DEFAULT_FONT_NAME_CAP 32
static char g_default_font_name[WP_DEFAULT_FONT_NAME_CAP] = "";

static void ensure_metrics_queried(void)
{
    struct Screen *screen;

    if (g_dpi_queried) return;
    g_dpi_queried = WP_TRUE; /* set first: a failed query below must not
                                 retry (and re-lock a screen) on every
                                 single call -- same defensive-caching
                                 shape as metrics_atari.c's g_metrics_vdi. */

    screen = LockPubScreen(NULL);
    if (screen == NULL) return;

    {
        LONG modeid = GetVPModeID(&screen->ViewPort);
        DisplayInfoHandle handle = FindDisplayInfo((ULONG)modeid);

        if (handle != NULL) {
            struct DisplayInfo di;

            memset(&di, 0, sizeof(di));
            if (GetDisplayInfoData(handle, (APTR)&di, (ULONG)sizeof(di),
                                    DTAG_DISP, (ULONG)modeid) != 0
                && di.Resolution.x > 0 && di.Resolution.y > 0) {
                /* DisplayInfo.Resolution is real, mode-derived
                   ticks-per-pixel per axis -- see
                   metrics_amiga_internal.h's own comment for why this,
                   not a hardcoded ratio, is used for X. Fewer ticks per
                   pixel on an axis means each pixel covers LESS physical
                   width there, i.e. a HIGHER dots-per-inch -- so X_DPI
                   scales inversely to X's own tick count, relative to Y
                   pinned at 72. */
                i32 x = (i32)(((i32)72 * (i32)di.Resolution.y) / (i32)di.Resolution.x);
                if (x > 0) g_dpi_x = x;
            }
        }
    }

    if (screen->Font != NULL && screen->Font->ta_Name != NULL) {
        u32 n = (u32)strlen((char *)screen->Font->ta_Name);
        if (n >= (u32)sizeof(g_default_font_name)) n = (u32)sizeof(g_default_font_name) - 1;
        memcpy(g_default_font_name, screen->Font->ta_Name, n);
        g_default_font_name[n] = '\0';
    }

    UnlockPubScreen(NULL, screen);
}

i32 amiga_twips_to_px_x(i32 twips)
{
    ensure_metrics_queried();
    return (twips * g_dpi_x) / 1440;
}

i32 amiga_twips_to_px_y(i32 twips)
{
    ensure_metrics_queried();
    return (twips * g_dpi_y) / 1440;
}

i32 amiga_px_to_twips_x(i32 px)
{
    ensure_metrics_queried();
    return (px * 1440) / g_dpi_x;
}

i32 amiga_px_to_twips_y(i32 px)
{
    ensure_metrics_queried();
    return (px * 1440) / g_dpi_y;
}

/* A lazily-initialized RastPort used ONLY for TextLength() measurement --
   never Text()/RectFill(). Verified on-target under amiberry (see
   docs/amiga-platform.md) that a bare InitRastPort() with no BitMap
   attached is NOT safe here, despite "TextLength only reads the font's
   own advance tables" being the commonly-cited reasoning for a
   bitmap-less measuring RastPort: real repeated redraws produced actual
   heap corruption (NUL bytes overwriting live wp_strpool content,
   visible as garbled glyphs), tracing back to this RastPort's
   rp_BitMap/rp_Layer being NULL/zeroed rather than pointing at anything
   real. A real, minimally-sized (1x1, depth 1) BitMap allocated via
   AllocBitMap -- the same graphics.library primitive any real Amiga
   program uses for an off-screen measuring surface -- removes the whole
   class of "passing graphics.library an incompletely-initialized
   structure" risk, not just the one call path that happened to crash
   first. Never freed: same process-lifetime-held-resource shape
   metrics_atari.c's own g_metrics_vdi already has (v_clsvwk is never
   called there either). */
static struct RastPort g_metrics_rp;
static struct BitMap *g_metrics_bitmap = NULL;
static wp_bool g_metrics_rp_ready = WP_FALSE;

/* Pre-V39 fallback storage for the measuring bitmap -- see
   ensure_metrics_rp. Process-lifetime, never freed, exactly like the
   AllocBitMap path above it. */
static struct BitMap g_metrics_bitmap_v37;

static void ensure_metrics_rp(void)
{
    if (g_metrics_rp_ready) return;
    g_metrics_rp_ready = WP_TRUE; /* set first -- see ensure_metrics_queried's
                                      own comment on why a failed attempt
                                      below must not retry every call */

    /* AllocBitMap is graphics.library V39 (Kickstart 3.0) -- verified
       against the NDK's own graphics_lib.sfd, where it appears after
       the "==version 39" marker, while InitBitMap/AllocRaster sit above
       "==version 36" and so exist on every Amiga.

       Kickstart 2.04/2.05 ship graphics.library V37, where calling
       AllocBitMap jumps past the end of the library's jump table --
       a wild jump, not a failed call, so it faults rather than
       returning NULL. That is the whole reason RetroWP could not run on
       those Kickstarts (T164): the very first paint calls relayout,
       which measures text, which lands here. Two separate lessons in
       one bug -- the failure was an illegal instruction at a garbage
       address, and the guard has to be a VERSION CHECK, because there
       is no return value to test. */
    if (GfxBase != NULL && GfxBase->LibNode.lib_Version >= 39) {
        g_metrics_bitmap = AllocBitMap(1, 1, 1, 0, NULL);
    } else {
        PLANEPTR plane = AllocRaster(1, 1);

        if (plane != NULL) {
            InitBitMap(&g_metrics_bitmap_v37, 1, 1, 1);
            g_metrics_bitmap_v37.Planes[0] = plane;
            g_metrics_bitmap = &g_metrics_bitmap_v37;
        }
    }
    if (g_metrics_bitmap == NULL) return;

    InitRastPort(&g_metrics_rp);
    g_metrics_rp.BitMap = g_metrics_bitmap;
}

static UWORD map_style(u16 style_flags)
{
    UWORD style = FS_NORMAL;

    if (style_flags & PLAT_STYLE_BOLD)      style |= FSF_BOLD;
    if (style_flags & PLAT_STYLE_ITALIC)    style |= FSF_ITALIC;
    if (style_flags & PLAT_STYLE_UNDERLINE) style |= FSF_UNDERLINED;
    /* PLAT_STYLE_STRIKE has no FS_* equivalent, and the document model's
       own SUPER/SUB never reach this seam -- not mapped, the same
       documented gap metrics_atari.c's map_effects already has for VDI's
       TXT_* effects. */
    return style;
}

wp_status plat_font_open(const u8 *utf8_font_name, u32 name_len,
                          i32 size_twips, u16 style_flags, plat_font **out)
{
    amiga_font *f;
    struct TextAttr ta;
    i32 px;

    WP_UNUSED(utf8_font_name);
    WP_UNUSED(name_len);

    ensure_metrics_queried();

    f = (amiga_font *)mem_alloc((u32)sizeof(amiga_font));
    if (f == NULL) return WP_NOMEM;

    px = (size_twips * g_dpi_y) / 1440;
    if (px < 1) px = 1;

    ta.ta_Name = (STRPTR)(g_default_font_name[0] != '\0' ? g_default_font_name : (char *)"topaz.font");
    ta.ta_YSize = (UWORD)px;
    ta.ta_Style = (UBYTE)map_style(style_flags);
    ta.ta_Flags = 0;

    f->tf = OpenDiskFont(&ta);
    f->owns_font = (wp_bool)(f->tf != NULL ? WP_TRUE : WP_FALSE);

    if (f->tf == NULL) {
        /* OpenDiskFont failing at all would be a genuine surprise for a
           topaz.font request (always ROM-resident on real hardware) --
           fall back to the exec-guaranteed-non-NULL system default
           rather than failing the whole request. Not owned: never
           CloseFont a font this code didn't open. */
        f->tf = GfxBase->DefaultFont;
        f->owns_font = WP_FALSE;
    }

    *out = (plat_font *)f;
    return WP_OK;
}

void plat_font_close(plat_font *f)
{
    amiga_font *af = (amiga_font *)f;

    if (af == NULL) return;
    if (af->owns_font && af->tf != NULL) CloseFont(af->tf);
    mem_free(af);
}

wp_status plat_font_metrics_get(plat_font *f, plat_font_metrics *out)
{
    amiga_font *af = (amiga_font *)f;

    if (af == NULL || af->tf == NULL || out == NULL) return WP_ERR;

    /* tf_Baseline: distance from the top of the character cell to the
       baseline, in pixels (real TextFont field, graphics/text.h) --
       ascent. tf_YSize - tf_Baseline is the remainder below the
       baseline -- descent. No separate leading field on TextFont, same
       leading=0 choice metrics_atari.c makes for VDI's own font info. */
    out->ascent = amiga_px_to_twips_y((i32)af->tf->tf_Baseline);
    out->descent = amiga_px_to_twips_y((i32)af->tf->tf_YSize - (i32)af->tf->tf_Baseline);
    if (out->descent < 0) out->descent = 0;
    out->leading = 0;
    return WP_OK;
}

wp_status plat_measure_text(plat_font *f, const u8 *utf8, u32 len,
                             i32 *width_twips_out)
{
    amiga_font *af = (amiga_font *)f;
    char buf[256];
    u32 n, out_len, unmapped;
    wp_status st;
    WORD px_width;

    if (af == NULL || af->tf == NULL || width_twips_out == NULL) return WP_ERR;

    ensure_metrics_rp();
    if (g_metrics_bitmap == NULL) return WP_ERR;

    n = (len < sizeof(buf)) ? len : (u32)sizeof(buf);
    /* Same real Latin-1 mapping draw_amiga.c's plat_utf8_to_native
       already implements -- reused here so measurement and drawing
       agree on exactly what bytes are being sized/rendered, the same
       consistency metrics_atari.c's own comment calls out. */
    st = plat_utf8_to_native(utf8, n, (u8 *)buf, (u32)sizeof(buf), &out_len, &unmapped);
    if (st != WP_OK) return st;

    if (out_len == 0) {
        *width_twips_out = 0;
        return WP_OK;
    }

    SetFont(&g_metrics_rp, af->tf);
    px_width = TextLength(&g_metrics_rp, (CONST_STRPTR)buf, (ULONG)out_len);

    *width_twips_out = amiga_px_to_twips_x((i32)px_width);
    return WP_OK;
}
