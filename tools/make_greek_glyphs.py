#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""tools/greek_glyphs.txt -> backends/atari/greek_glyphs.h.

Run from retroplat's root:  python3 tools/make_greek_glyphs.py

The header is included by draw_atari.c alone. It holds, for each glyph,
its advance in source pixels and its rows at 8x16 (16 rows, row 0 the
cell's top) and 8x8 (9 rows, from the row above the cell), each row a
byte with bit 7 the leftmost pixel; and the Greek block as a table from
Unicode to what draws it: a character of the ST's own set (a Latin
capital the same shape as the Greek one), one of these glyphs, an
overlay drawn over it, a glyph drawn before it.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'greek_glyphs.txt')
OUT = os.path.join(HERE, '..', 'backends', 'atari', 'greek_glyphs.h')

# Unicode -> (base, overlay, prefix). base is a one-character string (the
# ST set's own character, ASCII here) or a glyph's name; overlay and
# prefix are glyph names or None.
L, G = 'latin', 'glyph'
MAP = {
    0x0384: (G, 'tonos', None, None),
    0x0385: (G, 'dialytikatonos', None, None),
    0x0386: (L, 'A', None, 'tonos'),
    0x0387: (L, '\xfa', None, None),            # ano teleia: the ST set's middle dot
    0x0388: (L, 'E', None, 'tonos'),
    0x0389: (L, 'H', None, 'tonos'),
    0x038A: (L, 'I', None, 'tonos'),
    0x038C: (L, 'O', None, 'tonos'),
    0x038E: (L, 'Y', None, 'tonos'),
    0x038F: (G, 'Omega', None, 'tonos'),
    0x0390: (G, 'iota', 'over_dialytikatonos', None),
    0x0391: (L, 'A', None, None),
    0x0392: (L, 'B', None, None),
    0x0393: (G, 'Gamma', None, None),
    0x0394: (G, 'Delta', None, None),
    0x0395: (L, 'E', None, None),
    0x0396: (L, 'Z', None, None),
    0x0397: (L, 'H', None, None),
    0x0398: (G, 'Theta', None, None),
    0x0399: (L, 'I', None, None),
    0x039A: (L, 'K', None, None),
    0x039B: (G, 'Lambda', None, None),
    0x039C: (L, 'M', None, None),
    0x039D: (L, 'N', None, None),
    0x039E: (G, 'Xi', None, None),
    0x039F: (L, 'O', None, None),
    0x03A0: (G, 'Pi', None, None),
    0x03A1: (L, 'P', None, None),
    0x03A3: (G, 'Sigma', None, None),
    0x03A4: (L, 'T', None, None),
    0x03A5: (L, 'Y', None, None),
    0x03A6: (G, 'Phi', None, None),
    0x03A7: (L, 'X', None, None),
    0x03A8: (G, 'Psi', None, None),
    0x03A9: (G, 'Omega', None, None),
    0x03AA: (L, 'I', 'over_dialytikacap', None),
    0x03AB: (L, 'Y', 'over_dialytikacap', None),
    0x03AC: (G, 'alpha', 'over_tonos', None),
    0x03AD: (G, 'epsilon', 'over_tonos', None),
    0x03AE: (G, 'eta', 'over_tonos', None),
    0x03AF: (G, 'iota', 'over_tonos', None),
    0x03B0: (G, 'upsilon', 'over_dialytikatonos', None),
    0x03B1: (G, 'alpha', None, None),
    0x03B2: (G, 'beta', None, None),
    0x03B3: (G, 'gamma', None, None),
    0x03B4: (G, 'delta', None, None),
    0x03B5: (G, 'epsilon', None, None),
    0x03B6: (G, 'zeta', None, None),
    0x03B7: (G, 'eta', None, None),
    0x03B8: (G, 'theta', None, None),
    0x03B9: (G, 'iota', None, None),
    0x03BA: (G, 'kappa', None, None),
    0x03BB: (G, 'lambda', None, None),
    0x03BC: (G, 'mu', None, None),
    0x03BD: (G, 'nu', None, None),
    0x03BE: (G, 'xi', None, None),
    0x03BF: (L, 'o', None, None),
    0x03C0: (G, 'pi', None, None),
    0x03C1: (G, 'rho', None, None),
    0x03C2: (G, 'finalsigma', None, None),
    0x03C3: (G, 'sigma', None, None),
    0x03C4: (G, 'tau', None, None),
    0x03C5: (G, 'upsilon', None, None),
    0x03C6: (G, 'phi', None, None),
    0x03C7: (G, 'chi', None, None),
    0x03C8: (G, 'psi', None, None),
    0x03C9: (G, 'omega', None, None),
    0x03CA: (G, 'iota', 'over_dialytika', None),
    0x03CB: (G, 'upsilon', 'over_dialytika', None),
    0x03CC: (L, 'o', 'over_tonos', None),
    0x03CD: (G, 'upsilon', 'over_tonos', None),
    0x03CE: (G, 'omega', 'over_tonos', None),
    0x037E: (L, ';', None, None),               # the Greek question mark is a semicolon
    0x2126: (G, 'Omega', None, None),           # the ohm sign is Omega
}


def parse(path):
    glyphs = {}
    order = []
    name = None
    block = None
    with open(path, encoding='utf-8') as f:
        for n, raw in enumerate(f, 1):
            line = raw.rstrip('\n')
            # A comment is "#" alone or "# text"; a glyph row never is.
            if not line or line == '#' or line.startswith('# '):
                continue
            if line.startswith('== '):
                name = line[3:].strip()
                if name in glyphs:
                    sys.exit('%s:%d: %s twice' % (path, n, name))
                glyphs[name] = {'16': [], '8': []}
                order.append(name)
                block = None
            elif line in ('16:', '8:'):
                block = line[:-1]
            else:
                if name is None or block is None or set(line) - set('#.'):
                    sys.exit('%s:%d: not a glyph row: %r' % (path, n, line))
                glyphs[name][block].append(line)
    for name in order:
        g = glyphs[name]
        if len(g['16']) != 16 or len(g['8']) != 9:
            sys.exit('%s: %d rows at 16 and %d at 8, not 16 and 9' % (name, len(g['16']), len(g['8'])))
        widths = set(len(r) for r in g['16'] + g['8'])
        if len(widths) != 1 or widths.pop() > 8:
            sys.exit('%s: rows of different widths, or wider than 8' % name)
    return glyphs, order


def byte(row):
    v = 0
    for i, c in enumerate(row):
        if c == '#':
            v |= 0x80 >> i
    return v


def main():
    glyphs, order = parse(SRC)
    used = set()
    for cp, (kind, base, over, pre) in MAP.items():
        for nm in ([base] if kind == G else []) + [over, pre]:
            if nm is not None:
                if nm not in glyphs:
                    sys.exit('U+%04X: no glyph %s' % (cp, nm))
                used.add(nm)
    unused = [nm for nm in order if nm not in used]
    if unused:
        sys.exit('drawn but never used: %s' % ', '.join(unused))
    index = {nm: i for i, nm in enumerate(order)}

    out = []
    out.append('/* SPDX-License-Identifier: LGPL-2.1-or-later */')
    out.append('/* GENERATED by tools/make_greek_glyphs.py from tools/greek_glyphs.txt;')
    out.append('   edit those and regenerate, not this. Included by draw_atari.c alone. */')
    out.append('#ifndef RP_GREEK_GLYPHS_H')
    out.append('#define RP_GREEK_GLYPHS_H')
    out.append('')
    out.append('#define GK_GLYPHS %d' % len(order))
    out.append('')
    out.append('/* Advance in source pixels; 0 for an overlay, which draws over the')
    out.append('   character before it. */')
    out.append('static const u8 gk_advance[GK_GLYPHS] = {')
    out.append('    ' + ', '.join(str(0 if nm.startswith('over_') else len(glyphs[nm]['16'][0])) for nm in order))
    out.append('};')
    out.append('')
    out.append('/* 8x16: row 0 the cell\'s top, the baseline row 13. */')
    out.append('static const u8 gk_rows16[GK_GLYPHS][16] = {')
    for nm in order:
        out.append('    { %s },   /* %s */' % (', '.join('0x%02X' % byte(r) for r in glyphs[nm]['16']), nm))
    out.append('};')
    out.append('')
    out.append('/* 8x8: from the row ABOVE the cell (-1), the baseline row 6. */')
    out.append('static const u8 gk_rows8[GK_GLYPHS][9] = {')
    for nm in order:
        out.append('    { %s },   /* %s */' % (', '.join('0x%02X' % byte(r) for r in glyphs[nm]['8']), nm))
    out.append('};')
    out.append('')
    out.append('/* What draws a Greek character: base, the ST set\'s own character when')
    out.append('   glyph is 0; glyph, over and pre, a glyph number plus one, or 0. */')
    out.append('typedef struct { u16 cp; u8 base; u8 glyph; u8 over; u8 pre; } gk_char;')
    out.append('')
    out.append('#define GK_CHARS %d' % len(MAP))
    out.append('static const gk_char gk_chars[GK_CHARS] = {   /* by cp, for a binary search */')
    for cp in sorted(MAP):
        kind, base, over, pre = MAP[cp]
        b = ord(base) if kind == L else 0
        gl = index[base] + 1 if kind == G else 0
        ov = index[over] + 1 if over else 0
        pr = index[pre] + 1 if pre else 0
        out.append('    { 0x%04X, 0x%02X, %2d, %2d, %2d },   /* %s */' % (cp, b, gl, ov, pr, chr(cp).encode('ascii', 'backslashreplace').decode()))
    out.append('};')
    out.append('')
    out.append('#endif')
    with open(OUT, 'w', encoding='ascii') as f:
        f.write('\n'.join(out) + '\n')
    print('%s: %d glyphs, %d characters' % (os.path.relpath(OUT), len(order), len(MAP)))


if __name__ == '__main__':
    main()
