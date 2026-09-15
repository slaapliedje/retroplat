#ifndef RP_GEMCOMPAT_H
#define RP_GEMCOMPAT_H

/* gemcompat.h -- the places two GEMs are not the same shape.
 *
 * This backend builds against two AES bindings: the ST's (mintlib's gem.h
 * over a 68000) and gem4xe's (~/dev/gem4xe, GEM on an Atari 8-bit with a
 * 65816 in it). The AES contract is the same one in both -- the opcodes,
 * the object tree, the resource format -- and almost every line here is
 * written once and compiles for both. What differs is not behaviour but C
 * declaration, in two structures, and both differences are correct for the
 * machine that made them. So the facts live here rather than as #ifdefs
 * scattered through the backend.
 *
 * Detected by gem4xe's own include guard rather than by a build flag,
 * because the thing that actually differs is which gem.h is on the
 * include path.
 *
 * #include <gem.h> before this header. */

#include <gem.h>

#ifdef GEM4XE_APP_GEM_H
#  include <stdint.h>
#else
#  include <osbind.h>
#endif

/* GEMDOS -- Malloc, Fopen and the rest of the ST's osbind names.
 *
 * On the ST they come from mintlib's <osbind.h>, which is a header of
 * inline trap #1 stubs and nothing else.  gem4xe declares the same names
 * in its own <gem.h> (they are one of its three COP faces), so including
 * osbind.h there would redeclare them -- and there is no such header on
 * that toolchain to include in any case.  One include, decided by which
 * GEM is on the path, and the callers below say nothing about it. */

/* MFDB_SET_ADDR -- assign a buffer to an MFDB's fd_addr.
 *
 * The ST's bindings declare the field `void *`, which on a 68000 is the 32
 * bits the VDI's layout wants. gem4xe runs on a 65816 whose small data
 * model makes `void *` SIXTEEN bits, and an MFDB whose first field shrank
 * would shift every field after it and break every application that builds
 * one by hand -- so it declares fd_addr as a uint32_t and says so. */
#ifdef GEM4XE_APP_GEM_H
#  define MFDB_SET_ADDR(m, p)  ((m).fd_addr = (uint32_t)(p))
#else
#  define MFDB_SET_ADDR(m, p)  ((m).fd_addr = (void *)(p))
#endif

/* OBJECT.ob_spec -- a pointer for some object types and a packed word for
 * others, which is why it is 32 bits of nothing in particular.
 *
 * mintlib declares it a UNION, so ST source says which half it means by
 * naming a member: `.free_string` for the char * a G_STRING, G_BUTTON or
 * G_TITLE carries, `.index` for the colour-and-border word a G_BOX,
 * G_IBOX or G_BOXCHAR carries (Compendium p.6.18). gem4xe declares it a
 * bare LONG, which is what the ST's OWN aes.h has and what gem4xe's AES
 * reads -- src/aes/objc.c takes its low 16 bits, everything the AES
 * reaches being in bank $00. Neither is wrong, and making gem4xe's a
 * union to suit one binding library would change a published ABI.
 *
 * The string form stores the WHOLE address, not its low word: a string
 * that is not in bank $00 is the caller's bug, and gem4xe counts a far
 * address (src/sys/abi.c, near_of) rather than following it -- which it
 * cannot do if the library has already thrown the high word away. */
#ifdef GEM4XE_APP_GEM_H
#  define OB_SPEC_SET_STRING(o, p) ((o).ob_spec = (LONG)(uint32_t)(char FAR *)(p))
#  define OB_SPEC_SET_INDEX(o, v)  ((o).ob_spec = (LONG)(v))
#  define OB_SPEC_INDEX(o)         ((long)(o).ob_spec)
#else
#  define OB_SPEC_SET_STRING(o, p) ((o).ob_spec.free_string = (p))
#  define OB_SPEC_SET_INDEX(o, v)  ((o).ob_spec.index = (v))
#  define OB_SPEC_INDEX(o)         ((long)(o).ob_spec.index)
#endif

/* OB_SPEC_STRING / OB_SPEC_IS_STRING -- reading back what was set.
 *
 * The setters above are only half the seam: the shell's self-checks read
 * a title back to prove it is the string they installed, and one site
 * (the accessory-slot check) indexes into it.  On the ST that is a plain
 * char * out of the union.  On gem4xe it is the LONG widened back to the
 * far pointer it was made from -- the same conversion the setter did,
 * run the other way, so a pointer that went in comes back equal to
 * itself.
 *
 * The comparison has its own macro rather than being spelled
 * `OB_SPEC_STRING(o) == (p)` at each site because the ST's char * and
 * gem4xe's char FAR * are not the same type, and only the macro knows
 * which one the operand has to be widened to. */
#ifdef GEM4XE_APP_GEM_H
#  define OB_SPEC_STRING(o)        ((const char FAR *)(uint32_t)(o).ob_spec)
#  define OB_SPEC_IS_STRING(o, p)  \
       ((o).ob_spec == (LONG)(uint32_t)(const char FAR *)(p))
#else
#  define OB_SPEC_STRING(o)        ((const char *)(o).ob_spec.free_string)
#  define OB_SPEC_IS_STRING(o, p)  ((o).ob_spec.free_string == (p))
#endif

/* FA_ERROR / FA_INFO -- the icon prefix of a form_alert string.
 *
 * These are gemlib's spelling, not the AES's: the alert string itself
 * carries the icon as "[n]" and both GEMs parse the same grammar, so all
 * that is needed here is the constant mintlib's mt_gem.h supplies
 * (FA_ERROR "[1]", FA_INFO "[4]").
 *
 * gem4xe draws three icons -- note, question, stop -- which is what
 * classic GEM has, and its al_icon() (src/aes/alert.c) sends anything
 * above 2 to stop.  "[4]" is a MagiC/XaAES extension, so FA_INFO maps to
 * the note icon here rather than arriving as a stop sign: an
 * informational alert with a stop sign on it says the wrong thing.
 *
 * Worth knowing on the ST as well: "[4]" is outside the 0-3 that plain
 * TOS documents, so what a Falcon under plain TOS draws for FA_INFO is
 * its own business and has not been checked on hardware. */
#ifdef GEM4XE_APP_GEM_H
#  define FA_ERROR "[1]"
#  define FA_INFO  "[1]"
#endif

/* WP_AES_NEAR -- data the AES is going to be handed must live in bank $00.
 *
 * gem4xe's ABI passes an address to the AES as sixteen bits and supplies
 * the bank itself: src/sys/abi.c's near_of() takes the low word and the
 * AES reads it in bank $00.  That is not a limitation it apologises for
 * -- the AES, its object trees and its strings have always lived in one
 * bank, and making the ABI carry a bank byte would cost every call.
 *
 * It collides with --data-model=large, which is the model this port needs
 * for everything else: there, a global goes to far memory unless it says
 * otherwise, so an object tree declared the ordinary way lands in bank
 * $05 and the AES reads whatever is at its offset in bank $00.  Nothing
 * traps; the menu simply does not draw.  gemapp.scm says so in as many
 * words -- "a program that hands the AES a tree or a string must declare
 * those __near".
 *
 * So: anything whose ADDRESS crosses the seam is tagged with this, and
 * anything that merely holds the word processor's own data is not.  On
 * the ST it is nothing at all -- a 68000 has one address space and the
 * question does not arise. */
#ifdef GEM4XE_APP_GEM_H
#  define WP_AES_NEAR __near
#else
#  define WP_AES_NEAR
#endif

/* OB_SPEC_ZERO -- the ob_spec slot of a static OBJECT initializer.
 *
 * `{0}` is what the ST needs (ob_spec is a union there, and a union's
 * initializer is brace-enclosed) and it is valid C89 for gem4xe's bare
 * LONG too -- C89 6.5.7 permits braces around a scalar initializer.
 * Calypsi does not implement that permission: it stops with "internal
 * error: unhandled initializer", which is a limit rather than a
 * disagreement, so the brace is spelled only where it is required. */
#ifdef GEM4XE_APP_GEM_H
#  define OB_SPEC_ZERO 0
#else
#  define OB_SPEC_ZERO {0}
#endif

/* The AES's small change: flag values that gemlib names and gem4xe, which
 * declares only the calls, leaves as the numbers the AES documents.  All
 * six are the ST's own (mt_gem.h) and none of them is in dispute. */
#ifdef GEM4XE_APP_GEM_H
#  define MENU_REMOVE   0
#  define MENU_INSTALL  1
#  define UNCHECK       0
#  define CHECK         1
#  define HIGHLIGHT     0
#  define UNHIGHLIGHT   1

/* MFORM -- gemlib declares graf_mouse's form as a struct of the mouse
 * form's fields; gem4xe declares it as the bare WORD array the AES
 * actually reads.  Everything in this tree passes a null form (the shape
 * is chosen by `mode`, and only USER_DEF reads the pointer), so the alias
 * carries every use there is.  Code that wants to BUILD a form should use
 * gem4xe's own shape rather than this name. */
typedef WORD MFORM;
#endif

/* wind_create_grect / wind_open_grect -- two gemlib wrappers gem4xe has
 * not got yet.
 *
 * gem4xe already carries most of this family (wind_get_grect,
 * wind_set_grect, wind_calc_grect, form_center_grect, objc_draw_grect)
 * for exactly the reason its gem.h gives: they are the difference between
 * a real ST application compiling and not.  Create and open are the two
 * it stopped short of, and both are one line over the call it does have.
 *
 * They are here rather than in gem4xe's gemlib.c only so that RetroWP
 * builds against an unmodified kit.  Upstream is the better home for
 * them, beside their five siblings. */
#ifdef GEM4XE_APP_GEM_H
#  define wind_create_grect(kind, r) \
       wind_create((kind), (r)->g_x, (r)->g_y, (r)->g_w, (r)->g_h)
#  define wind_open_grect(handle, r) \
       wind_open((handle), (r)->g_x, (r)->g_y, (r)->g_w, (r)->g_h)
#endif

/* WP_EVNT_MULTI -- one wait, two spellings.
 *
 * The AES call is the same underneath; the bindings disagree about how to
 * hand it the two mouse rectangles and the timer.  gemlib takes ten loose
 * words and one LONG; gem4xe takes two MOBLK pointers and the timer
 * already split into its low and high words.  Neither is a different
 * event loop, so the call site spells it once and this decides which
 * binding it lands on.
 *
 * Fixed arity rather than a variadic macro: this tree is C89, which has
 * no __VA_ARGS__. */
#ifdef GEM4XE_APP_GEM_H
WORD rp_evnt_multi_st(UWORD flags, WORD bclk, UWORD bmsk, UWORD bst,
                      WORD m1f, WORD m1x, WORD m1y, WORD m1w, WORD m1h,
                      WORD m2f, WORD m2x, WORD m2y, WORD m2w, WORD m2h,
                      WORD *msg, LONG timer,
                      WORD *mx, WORD *my, WORD *mb,
                      WORD *ks, WORD *kr, WORD *br);
#  define WP_EVNT_MULTI(fl,bc,bm,bs, a1,a2,a3,a4,a5, b1,b2,b3,b4,b5, \
                        msg,tm, mx,my,mb,ks,kr,br)                    \
       rp_evnt_multi_st((fl),(bc),(bm),(bs),                          \
                        (a1),(a2),(a3),(a4),(a5),                     \
                        (b1),(b2),(b3),(b4),(b5), (msg),(tm),         \
                        (mx),(my),(mb),(ks),(kr),(br))
#else
#  define WP_EVNT_MULTI(fl,bc,bm,bs, a1,a2,a3,a4,a5, b1,b2,b3,b4,b5, \
                        msg,tm, mx,my,mb,ks,kr,br)                    \
       evnt_multi((fl),(bc),(bm),(bs),                                \
                  (a1),(a2),(a3),(a4),(a5),                           \
                  (b1),(b2),(b3),(b4),(b5), (msg),(tm),               \
                  (mx),(my),(mb),(ks),(kr),(br))
#endif

#endif /* RP_GEMCOMPAT_H */
