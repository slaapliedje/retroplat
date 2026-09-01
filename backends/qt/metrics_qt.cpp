#include <QString>
#include <QByteArray>
#include <cstring>

#include "metrics_qt_internal.h"

extern "C" {
#include "platform.h"
#include "mem_host.h"
}

extern "C" {

wp_status plat_font_open(const u8 *utf8_font_name, u32 name_len,
                         i32 size_twips, u16 style_flags, plat_font **out)
{
    if (out == NULL) return WP_ERR;

    qt_font *f = (qt_font *)mem_alloc((u32)sizeof(qt_font));
    if (f == NULL) return WP_NOMEM;

    /* An empty or absent name means "whatever this system calls monospace",
       which is what a report of aligned columns wants and what every other
       backend here resolves to -- topaz on the Amiga, Monaco on the Mac,
       the one BIOS cell under DOS. */
    QString family = QStringLiteral("Monospace");
    if (utf8_font_name != NULL && name_len > 0)
        family = QString::fromUtf8((const char *)utf8_font_name, (int)name_len);

    f->font = new QFont(family);
    f->font->setPointSizeF((qreal)size_twips / QT_TWIPS_PER_POINT);
    f->font->setStyleHint(QFont::TypeWriter);
    if (style_flags & PLAT_STYLE_BOLD)      f->font->setBold(true);
    if (style_flags & PLAT_STYLE_ITALIC)    f->font->setItalic(true);
    if (style_flags & PLAT_STYLE_UNDERLINE) f->font->setUnderline(true);
    if (style_flags & PLAT_STYLE_STRIKE)    f->font->setStrikeOut(true);

    f->fm = new QFontMetrics(*f->font);
    f->size_twips = size_twips;

    *out = (plat_font *)f;
    return WP_OK;
}

void plat_font_close(plat_font *f)
{
    qt_font *qf = (qt_font *)f;
    if (qf == NULL) return;
    delete qf->fm;
    delete qf->font;
    mem_free(qf);
}

wp_status plat_font_metrics_get(plat_font *f, plat_font_metrics *out)
{
    qt_font *qf = (qt_font *)f;
    if (qf == NULL || qf->fm == NULL || out == NULL) return WP_ERR;
    out->ascent  = (i32)qf->fm->ascent()  * QT_TWIPS_PER_POINT;
    out->descent = (i32)qf->fm->descent() * QT_TWIPS_PER_POINT;
    /* Qt folds it into ascent/descent, the same documented choice the Atari
       and Amiga backends record for their own font info. */
    out->leading = 0;
    return WP_OK;
}

wp_status plat_measure_text(plat_font *f, const u8 *utf8, u32 len,
                            i32 *width_twips_out)
{
    qt_font *qf = (qt_font *)f;
    if (qf == NULL || qf->fm == NULL || utf8 == NULL || width_twips_out == NULL)
        return WP_ERR;
    QString s = QString::fromUtf8((const char *)utf8, (int)len);
    *width_twips_out = (i32)qf->fm->horizontalAdvance(s) * QT_TWIPS_PER_POINT;
    return WP_OK;
}

/* Everything here is already UTF-8: Qt takes it directly, so unlike the
   Atari, Amiga, Mac and DOS backends there is no code page to fall out of
   and nothing is ever unmappable. */
wp_status plat_utf8_to_native(const u8 *utf8, u32 len,
                              u8 *out, u32 out_cap, u32 *out_len,
                              u32 *unmapped_count)
{
    if (utf8 == NULL || out == NULL) return WP_ERR;
    u32 n = (len > out_cap) ? out_cap : len;
    std::memcpy(out, utf8, n);
    if (out_len != NULL) *out_len = n;
    if (unmapped_count != NULL) *unmapped_count = 0;
    return WP_OK;
}

wp_status plat_native_to_utf8(const u8 *native, u32 len,
                              u8 *out, u32 out_cap, u32 *out_len)
{
    if (native == NULL || out == NULL) return WP_ERR;
    u32 n = (len > out_cap) ? out_cap : len;
    std::memcpy(out, native, n);
    if (out_len != NULL) *out_len = n;
    return WP_OK;
}

}  /* extern "C" */
