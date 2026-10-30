#!/usr/bin/env python3
"""
bdf2font.py - Convert BDF bitmap font(s) into page-packed C source for a
page-based OLED controller (SSD1306/SSD1360 style), matching this struct
layout (defined once in font.h):

    typedef struct {
        uint16_t bitmap_offset;
        uint8_t  width;
        uint8_t  x_advance;
        uint8_t  x_offset;
    } font_glyph_t;

    typedef struct {
        uint8_t  height;
        uint8_t  first_char;
        uint8_t  num_chars;
        const font_glyph_t *glyphs;
        const uint8_t *bitmap;
    } font_t;

Packing: each byte = one vertical "page" (8 rows) of ONE column.
bit0 = TOP pixel of that page, bit7 = BOTTOM pixel. Bitmap data per glyph
is column-major: column 0's pages top-to-bottom, then column 1, etc.
All glyphs in a font share the same fixed `height` (and therefore the
same number of pages = ceil(height/8), computed at runtime, not stored).

Usage (one font):
    python3 bdf2font.py FONT.bdf --name font9 --first 32 --last 126 -o font9.c

Run once per font. Then make sure font.h (generated alongside, or reuse
an existing one) is included by each generated .c file.
"""
import argparse
import re


class Glyph:
    def __init__(self, encoding, dwidth, bbx, bitmap_rows):
        self.encoding = encoding
        self.advance = dwidth
        self.bb_w, self.bb_h, self.bb_xoff, self.bb_yoff = bbx
        self.rows = bitmap_rows


def parse_bdf(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    font_ascent = 0
    font_descent = 0
    m = re.search(r"FONT_ASCENT\s+(-?\d+)", text)
    if m:
        font_ascent = int(m.group(1))
    m = re.search(r"FONT_DESCENT\s+(-?\d+)", text)
    if m:
        font_descent = int(m.group(1))

    glyphs = {}
    chunks = text.split("STARTCHAR")[1:]
    for chunk in chunks:
        enc_m = re.search(r"ENCODING\s+(-?\d+)", chunk)
        dwidth_m = re.search(r"DWIDTH\s+(-?\d+)\s+(-?\d+)", chunk)
        bbx_m = re.search(r"BBX\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)", chunk)
        if not (enc_m and dwidth_m and bbx_m):
            continue
        encoding = int(enc_m.group(1))
        if encoding < 0:
            continue
        dwidth = int(dwidth_m.group(1))
        bbx = tuple(int(g) for g in bbx_m.groups())
        w, h, xoff, yoff = bbx

        bm_m = re.search(r"BITMAP\s*\n(.*?)ENDCHAR", chunk, re.S)
        rows = []
        if bm_m:
            hex_lines = [l.strip() for l in bm_m.group(1).splitlines() if l.strip()]
            for hl in hex_lines:
                nbytes = (w + 7) // 8
                hl = hl.ljust(nbytes * 2, "0")
                val = int(hl, 16) if hl else 0
                rows.append(val)
        while len(rows) < h:
            rows.append(0)
        glyphs[encoding] = Glyph(encoding, dwidth, bbx, rows[:h])

    return glyphs, font_ascent, font_descent


def get_pixel(glyph, row, col, row_byte_width):
    val = glyph.rows[row]
    total_bits = row_byte_width * 8
    bit_pos = total_bits - 1 - col
    return (val >> bit_pos) & 1


def pack_glyph_pagewise(glyph, cell_height, baseline_from_top):
    w = glyph.bb_w
    h = glyph.bb_h
    row_byte_width = (w + 7) // 8
    pages = (cell_height + 7) // 8
    top_cell_row = baseline_from_top - (glyph.bb_yoff + h - 1)

    out = bytearray()
    for col in range(w):
        for page in range(pages):
            byte = 0
            for bit in range(8):
                cell_row = page * 8 + bit
                glyph_row = cell_row - top_cell_row
                pixel = 0
                if 0 <= glyph_row < h:
                    pixel = get_pixel(glyph, glyph_row, col, row_byte_width)
                if pixel:
                    byte |= (1 << bit)
            out.append(byte)
    return bytes(out)


FONT_H_TEMPLATE = """#pragma once
#include <stdint.h>

// One entry per glyph. All glyphs in a font share the same height
// (see font_t.height), so it isn't repeated here.
typedef struct {
    uint16_t bitmap_offset;  // index into font_t.bitmap[] where this glyph's data starts
    uint8_t  width;          // glyph width, px
    uint8_t  x_advance;      // cursor advance after drawing, px
    uint8_t  x_offset;       // x offset from cursor to left of glyph, px
} font_glyph_t;

typedef struct {
    uint8_t  height;             // fixed height for every glyph, px
    uint8_t  first_char;         // ASCII code of glyphs[0]
    uint8_t  num_chars;          // number of glyphs in the array
    const font_glyph_t *glyphs;  // glyphs[c - first_char]
    const uint8_t *bitmap;       // packed pages, column-major per glyph
                                  // (bit0 = top row of page, bit7 = bottom row)
} font_t;

// pages per glyph = (height + 7) / 8  -- compute at draw time, not stored

__EXTERNS__
"""


def generate_c_file(path, name, glyphs, first, last, cell_height, baseline_from_top):
    bitmap_bytes = bytearray()
    glyph_lines = []

    for code in range(first, last + 1):
        g = glyphs.get(code)
        if g is None:
            glyph_lines.append((0, 0, 0, 0))
            continue
        packed = pack_glyph_pagewise(g, cell_height, baseline_from_top)
        offset = len(bitmap_bytes)
        bitmap_bytes.extend(packed)
        glyph_lines.append((offset, g.bb_w, g.advance, g.bb_xoff))

    lines = []
    lines.append(f'#include "font.h"')
    lines.append('')
    lines.append(f'static const uint8_t {name}_bitmap[] = {{')
    for i in range(0, len(bitmap_bytes), 16):
        chunk = bitmap_bytes[i:i + 16]
        lines.append('    ' + ', '.join(f'0x{b:02X}' for b in chunk) + ',')
    lines.append('};')
    lines.append('')
    lines.append(f'static const font_glyph_t {name}_glyphs[] = {{')
    for (offset, w, adv, xoff) in glyph_lines:
        lines.append(f'    {{ {offset}, {w}, {adv}, {xoff} }},')
    lines.append('};')
    lines.append('')
    lines.append(f'const font_t {name} = {{')
    lines.append(f'    .height     = {cell_height},')
    lines.append(f'    .first_char = {first},')
    lines.append(f'    .num_chars  = {last - first + 1},')
    lines.append(f'    .glyphs     = {name}_glyphs,')
    lines.append(f'    .bitmap     = {name}_bitmap,')
    lines.append('};')
    lines.append('')

    with open(path, 'w') as f:
        f.write('\n'.join(lines))
    print(f'Wrote {path}: {last - first + 1} glyphs, {len(bitmap_bytes)} bitmap bytes')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bdf_file')
    ap.add_argument('--name', default='myfont', help='C identifier, e.g. font9')
    ap.add_argument('--first', type=int, default=32)
    ap.add_argument('--last', type=int, default=126)
    ap.add_argument('--cell-height', type=int, default=None)
    ap.add_argument('-o', '--output', default=None, help='output .c path')
    ap.add_argument('--write-header', default=None,
                     help='also (re)write a font.h with extern declarations for '
                          'these font names, e.g. --write-header font9,font12b')
    args = ap.parse_args()

    glyphs, font_ascent, font_descent = parse_bdf(args.bdf_file)
    cell_height = args.cell_height or (font_ascent + font_descent)
    baseline_from_top = font_ascent - 1 if font_ascent else cell_height - 1

    out_path = args.output or (args.name + '.c')
    generate_c_file(out_path, args.name, glyphs, args.first, args.last,
                     cell_height, baseline_from_top)

    if args.write_header:
        names = [n.strip() for n in args.write_header.split(',') if n.strip()]
        externs = '\n'.join(f'extern const font_t {n};' for n in names)
        with open('font.h', 'w') as f:
            f.write(FONT_H_TEMPLATE.replace('__EXTERNS__', externs))
        print(f'Wrote font.h with externs for: {", ".join(names)}')


if __name__ == '__main__':
    main()
