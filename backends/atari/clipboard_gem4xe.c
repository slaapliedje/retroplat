/* clipboard_gem4xe.c -- plat_clipboard_* on gem4xe.
 *
 * The convention is classic GEM's and is unchanged from the ST
 * (clipboard_atari.c): the clipboard is a directory, and the content is a
 * plain file called SCRAP.TXT inside it.  Two things differ.
 *
 * FILES.  gem4xe has no stdio -- deliberately, because Calypsi's comes
 * from Apache NuttX and Apache 2.0 cannot be linked into the GPLv2 binary
 * gem4xe produces (gem4xe/docs/licence.md).  GEMDOS is what stdio would
 * have called anyway, so it is called directly, exactly as file_gem4xe.c
 * does for the plat_file seam.
 *
 * SCRP_READ / SCRP_WRITE.  gem4xe does not serve these two AES calls, and
 * nothing is lost by their absence here: they exist so that SEPARATE GEM
 * applications can agree on which directory the scrap is in, and on this
 * machine RetroWP is both the producer and the consumer.  The ST side
 * already treats a scrp_read() answer as a hint and falls back to the
 * fixed directory when it is empty -- so this is that same code path with
 * the hint removed, not a different policy.  If gem4xe grows the two
 * calls, this file should start asking.
 *
 * BUFFERS ARE STATIC, NOT AUTOMATIC.  An application's near memory on
 * gem4xe -- which is where its stack is, because the 65816 stack is in
 * bank $00 by hardware -- comes out of a 2 KB pool.  The ST's version of
 * this file puts a 1 KB scrap buffer on the stack, which is unremarkable
 * on a machine with a 32 KB one and fatal here.  Declared static, they go
 * to far BSS under --data-model=large and cost the pool nothing. */

#include <gem.h>

#ifdef GEM4XE_APP_GEM_H

#include "platform.h"

#define CLIP_FILE "C:\\SCRAP.TXT"
#define SCRAP_MAX 1024

static u8 g_native[SCRAP_MAX];

wp_status plat_clipboard_set_text(const u8 *utf8, u32 len)
{
    u32 native_len, unmapped;
    wp_status st;
    LONG h, wrote;

    st = plat_utf8_to_native(utf8, len, g_native, (u32)SCRAP_MAX,
                             &native_len, &unmapped);
    if (st != WP_OK) return st;

    h = Fcreate(CLIP_FILE, 0);
    if (h < 0) return WP_ERR;

    wrote = Fwrite((WORD)h, (LONG)native_len, g_native);
    (void)Fclose((WORD)h);

    if (wrote != (LONG)native_len) return WP_ERR;
    return WP_OK;
}

wp_status plat_clipboard_get_text(u8 *out, u32 out_cap, u32 *out_len)
{
    LONG h, got;

    h = Fopen(CLIP_FILE, 0);            /* 0 = read only */
    if (h < 0) {
        if (out_len) *out_len = 0;
        return WP_ERR;
    }

    got = Fread((WORD)h, (LONG)SCRAP_MAX, g_native);
    (void)Fclose((WORD)h);

    if (got < 0) {
        if (out_len) *out_len = 0;
        return WP_ERR;
    }

    return plat_native_to_utf8(g_native, (u32)got, out, out_cap, out_len);
}

wp_bool plat_clipboard_has_text(void)
{
    LONG h = Fopen(CLIP_FILE, 0);

    if (h < 0) return WP_FALSE;
    (void)Fclose((WORD)h);
    return WP_TRUE;
}

#else

typedef int rp_clipboard_gem4xe_not_this_target;

#endif
