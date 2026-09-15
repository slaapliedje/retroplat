/* gemcompat_gem4xe.c -- the parts of the two-GEM seam that need code.
 *
 * gemcompat.h carries the seam as macros wherever a macro will do.  This
 * file is for the one place it will not: gemlib's evnt_multi takes the
 * two mouse rectangles as ten loose words and the timer as a LONG, and
 * gem4xe's takes two MOBLK pointers and the timer already split.  Turning
 * one into the other needs somewhere to put the MOBLKs, and a C89 macro
 * has nowhere -- no compound literals, no statement expressions.
 *
 * Guarded like file_gem4xe.c, and for the same reason: this directory is
 * compiled whole by the ST build and by retroplat's own seam gate, and
 * neither can be asked to know which files are whose.
 *
 * On the ST, WP_EVNT_MULTI goes straight to evnt_multi and nothing here
 * is reached or wanted. */

#include <gem.h>

#ifdef GEM4XE_APP_GEM_H

#include "gemcompat.h"

WORD rp_evnt_multi_st(UWORD flags, WORD bclk, UWORD bmsk, UWORD bst,
                      WORD m1f, WORD m1x, WORD m1y, WORD m1w, WORD m1h,
                      WORD m2f, WORD m2x, WORD m2y, WORD m2w, WORD m2h,
                      WORD *msg, LONG timer,
                      WORD *mx, WORD *my, WORD *mb,
                      WORD *ks, WORD *kr, WORD *br)
{
    MOBLK m1, m2;

    /* gemlib's m1flag/m2flag IS gem4xe's m_out: "report leaving the
       rectangle" rather than entering it.  Same word, same meaning, one
       is named after the structure it sits in and the other after the
       argument position it occupied. */
    m1.m_out = m1f; m1.m_x = m1x; m1.m_y = m1y; m1.m_w = m1w; m1.m_h = m1h;
    m2.m_out = m2f; m2.m_x = m2x; m2.m_y = m2y; m2.m_w = m2w; m2.m_h = m2h;

    /* The AES has always taken the timer as two words; gemlib is the one
       doing the joining, so undo exactly that and no more.  The cast to
       ULONG before shifting is the point: `timer` is signed, and a
       right-shift of a negative LONG is implementation-defined (C89
       6.3.7).  No caller in this tree passes one, which is precisely why
       it must not be left to chance. */
    return evnt_multi(flags, bclk, bmsk, bst, &m1, &m2, msg,
                      (UWORD)((unsigned long)timer & 0xFFFFuL),
                      (UWORD)(((unsigned long)timer >> 16) & 0xFFFFuL),
                      mx, my, mb, ks, kr, br);
}

#else

typedef int rp_gemcompat_not_this_target;

#endif
