#include <windows.h>
#include <string.h>

#include "metrics_win32_internal.h"
#include "platform.h"
#include "mem_host.h"

/* GDI speaks UTF-16 and this seam speaks UTF-8, so every string crossing
   into GDI goes through here. MultiByteToWideChar with CP_UTF8 is the only
   conversion in the backend -- there is no code page involved and nothing
   can be unmappable, because UTF-16 covers everything UTF-8 does. */
int rp_w32_widen(const u8 *utf8, u32 len, WCHAR *out, int out_cap)
{
    int n;
    if (utf8 == NULL || out == NULL || out_cap <= 0) return -1;
    if (len == 0) { out[0] = 0; return 0; }
    n = MultiByteToWideChar(CP_UTF8, 0, (const char *)utf8, (int)len,
                            out, out_cap);
    if (n <= 0) return -1;
    return n;
}

wp_status plat_font_open(const u8 *utf8_font_name, u32 name_len,
                         i32 size_twips, u16 style_flags, plat_font **out)
{
    w32_font  *f;
    HDC        dc;
    HGDIOBJ    old;
    TEXTMETRICW tm;
    WCHAR      face[LF_FACESIZE];
    int        n, height;
    LOGFONTW   lf;

    if (out == NULL) return WP_ERR;

    f = (w32_font *)mem_alloc((u32)sizeof(w32_font));
    if (f == NULL) return WP_NOMEM;

    /* An empty or absent name means "whatever this system calls
       monospace", which is what a report of aligned columns wants and what
       every other backend here resolves to -- topaz on the Amiga, Monaco
       on the Mac, the one BIOS cell under DOS, Monospace under Qt.

       Courier New rather than Consolas: it has shipped with every Windows
       since 3.1 and is one of the core fonts Wine substitutes for, where
       Consolas arrived with Vista and is absent from a bare Wine prefix. */
    lstrcpyW(face, L"Courier New");
    if (utf8_font_name != NULL && name_len > 0) {
        n = rp_w32_widen(utf8_font_name, name_len, face, LF_FACESIZE - 1);
        if (n < 0) n = 0;
        face[n] = 0;
        if (n == 0) lstrcpyW(face, L"Courier New");
    }

    dc = GetDC(NULL);
    if (dc == NULL) { mem_free(f); return WP_ERR; }
    f->dpi_y = GetDeviceCaps(dc, LOGPIXELSY);
    if (f->dpi_y <= 0) f->dpi_y = 96;

    /* Negative height asks GDI for a CHARACTER height (em size) rather
       than a cell height, which is what a point size means. */
    height = -MulDiv((int)size_twips, f->dpi_y, RP_TWIPS_PER_INCH);
    if (height == 0) height = -1;

    memset(&lf, 0, sizeof lf);
    lf.lfHeight         = height;
    lf.lfWeight         = (style_flags & PLAT_STYLE_BOLD) ? FW_BOLD : FW_NORMAL;
    lf.lfItalic         = (style_flags & PLAT_STYLE_ITALIC)    ? TRUE : FALSE;
    lf.lfUnderline      = (style_flags & PLAT_STYLE_UNDERLINE) ? TRUE : FALSE;
    lf.lfStrikeOut      = (style_flags & PLAT_STYLE_STRIKE)    ? TRUE : FALSE;
    lf.lfCharSet        = DEFAULT_CHARSET;
    lf.lfOutPrecision   = OUT_TT_PRECIS;
    lf.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
    lf.lfQuality        = DEFAULT_QUALITY;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    lstrcpyW(lf.lfFaceName, face);

    f->hfont = CreateFontIndirectW(&lf);
    if (f->hfont == NULL) {
        ReleaseDC(NULL, dc);
        mem_free(f);
        return WP_ERR;
    }

    /* Measured once, here, against the screen DC. See the header. */
    old = SelectObject(dc, f->hfont);
    if (GetTextMetricsW(dc, &tm)) {
        f->ascent_twips  = (i32)MulDiv((int)tm.tmAscent,
                                       RP_TWIPS_PER_INCH, f->dpi_y);
        f->descent_twips = (i32)MulDiv((int)tm.tmDescent,
                                       RP_TWIPS_PER_INCH, f->dpi_y);
    } else {
        f->ascent_twips  = size_twips;
        f->descent_twips = 0;
    }
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);

    f->size_twips = size_twips;
    *out = (plat_font *)f;
    return WP_OK;
}

void plat_font_close(plat_font *f)
{
    w32_font *wf = (w32_font *)f;
    if (wf == NULL) return;
    if (wf->hfont != NULL) DeleteObject(wf->hfont);
    mem_free(wf);
}

wp_status plat_font_metrics_get(plat_font *f, plat_font_metrics *out)
{
    w32_font *wf = (w32_font *)f;
    if (wf == NULL || out == NULL) return WP_ERR;
    out->ascent  = wf->ascent_twips;
    out->descent = wf->descent_twips;
    /* GDI reports tmExternalLeading separately, but folds nothing into
       ascent/descent that the other backends fold in -- and the shells
       here space lines from ascent+descent. Reporting it as zero keeps
       this backend saying what the Atari, Amiga, Mac and Qt ones say. */
    out->leading = 0;
    return WP_OK;
}

wp_status plat_measure_text(plat_font *f, const u8 *utf8, u32 len,
                            i32 *width_twips_out)
{
    w32_font *wf = (w32_font *)f;
    WCHAR     stackbuf[512];
    WCHAR    *wide = stackbuf;
    HDC       dc;
    HGDIOBJ   old;
    SIZE      sz;
    int       n;
    BOOL      ok;

    if (wf == NULL || utf8 == NULL || width_twips_out == NULL) return WP_ERR;
    if (len == 0) { *width_twips_out = 0; return WP_OK; }

    /* One UTF-8 byte can never produce more than one UTF-16 code unit, so
       len is a safe upper bound and the stack path covers every report row
       this library's consumers draw. */
    if (len > (u32)(sizeof stackbuf / sizeof stackbuf[0])) {
        wide = (WCHAR *)mem_alloc(len * (u32)sizeof(WCHAR));
        if (wide == NULL) return WP_NOMEM;
    }

    n = rp_w32_widen(utf8, len, wide, (int)len);
    if (n < 0) { if (wide != stackbuf) mem_free(wide); return WP_ERR; }

    dc = GetDC(NULL);
    if (dc == NULL) { if (wide != stackbuf) mem_free(wide); return WP_ERR; }
    old = SelectObject(dc, wf->hfont);
    ok = GetTextExtentPoint32W(dc, wide, n, &sz);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    if (wide != stackbuf) mem_free(wide);

    if (!ok) return WP_ERR;
    *width_twips_out = (i32)MulDiv((int)sz.cx, RP_TWIPS_PER_INCH, wf->dpi_y);
    return WP_OK;
}

/* "Native" on Windows is the process ANSI code page -- what the A-suffixed
   API and a plain text file written by Notepad use. Unlike the Atari,
   Amiga, Mac and DOS backends this one has a REAL converter to hand rather
   than a hand-built table, so the unmappable count comes from GDI's own
   answer rather than from counting misses in a loop. */
wp_status plat_utf8_to_native(const u8 *utf8, u32 len,
                              u8 *out, u32 out_cap, u32 *out_len,
                              u32 *unmapped_count)
{
    WCHAR  stackbuf[512];
    WCHAR *wide = stackbuf;
    int    n, m;
    BOOL   used = FALSE;

    if (utf8 == NULL || out == NULL) return WP_ERR;
    if (len == 0) {
        if (out_len != NULL) *out_len = 0;
        if (unmapped_count != NULL) *unmapped_count = 0;
        return WP_OK;
    }
    if (len > (u32)(sizeof stackbuf / sizeof stackbuf[0])) {
        wide = (WCHAR *)mem_alloc(len * (u32)sizeof(WCHAR));
        if (wide == NULL) return WP_NOMEM;
    }
    n = rp_w32_widen(utf8, len, wide, (int)len);
    if (n < 0) { if (wide != stackbuf) mem_free(wide); return WP_ERR; }

    m = WideCharToMultiByte(CP_ACP, 0, wide, n, (char *)out, (int)out_cap,
                            "?", &used);
    if (wide != stackbuf) mem_free(wide);
    if (m <= 0 && n > 0) return WP_ERR;

    if (out_len != NULL) *out_len = (u32)m;
    /* GDI answers "was anything substituted", not "how many". One is the
       honest report of that: a caller uses it as a flag, and inventing a
       count GDI never gave would be worse than a conservative one. */
    if (unmapped_count != NULL) *unmapped_count = used ? 1u : 0u;
    return WP_OK;
}

wp_status plat_native_to_utf8(const u8 *native, u32 len,
                              u8 *out, u32 out_cap, u32 *out_len)
{
    WCHAR  stackbuf[512];
    WCHAR *wide = stackbuf;
    int    n, m;

    if (native == NULL || out == NULL) return WP_ERR;
    if (len == 0) { if (out_len != NULL) *out_len = 0; return WP_OK; }

    if (len > (u32)(sizeof stackbuf / sizeof stackbuf[0])) {
        wide = (WCHAR *)mem_alloc(len * (u32)sizeof(WCHAR));
        if (wide == NULL) return WP_NOMEM;
    }
    n = MultiByteToWideChar(CP_ACP, 0, (const char *)native, (int)len,
                            wide, (int)len);
    if (n <= 0) { if (wide != stackbuf) mem_free(wide); return WP_ERR; }

    m = WideCharToMultiByte(CP_UTF8, 0, wide, n, (char *)out, (int)out_cap,
                            NULL, NULL);
    if (wide != stackbuf) mem_free(wide);
    if (m <= 0) return WP_ERR;

    if (out_len != NULL) *out_len = (u32)m;
    return WP_OK;
}
