/* clib_gem4xe.c -- the two ISO C functions gem4xe's kit does not carry.
 *
 * The kit writes its own memcpy, memset and str* rather than taking
 * Calypsi's, because Calypsi's come from Apache NuttX and Apache 2.0
 * cannot be linked into a GPLv2 binary -- and a program linked against
 * gem4xe's application kit is GPLv2 (gem4xe/docs/licence.md).
 *
 * It stops at the set its own programs needed.  RetroWP's engine also
 * uses memcmp and memchr, so they are written here for the same reason
 * and under the same terms.  Without them the linker would quietly
 * satisfy both from clib-lc-ld.a -- quietly being the problem: the binary
 * would carry Apache-2.0 object code and nothing would say so.
 *
 * Straightforward implementations on purpose.  These are the two
 * functions whose being subtly wrong would be hardest to attribute later,
 * and neither is on a path where a clever one would pay.
 *
 * memcmp's comparison is on UNSIGNED chars: C89 4.11.4.1 specifies the
 * sign of the result by the unsigned values, and `char` is signed on this
 * compiler, so comparing through char * would order 0x80 below 0x00. */

#include <gem.h>

#ifdef GEM4XE_APP_GEM_H

#include <stddef.h>

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;

    while (n-- > 0) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char        w = (unsigned char)c;

    while (n-- > 0) {
        if (*p == w) return (void *)p;
        p++;
    }
    return (void *)0;
}

#else

typedef int rp_clib_gem4xe_not_this_target;

#endif
