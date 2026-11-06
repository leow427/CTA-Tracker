#!/usr/bin/env python3
"""Builds the devices' pixel art from art/*.txt: Trackie, his faces and extras, the weather overlays, the glyphs,
and the pixel digits. Writes firmware/lib/pet/src/pet_sprites.cpp and 8x PNG previews in art/build/.

The grids are the same art the iPhone app draws (ios/CTATracker/Design/Pixel.swift); test_sprite_build.py checks
that the two copies agree. Standard library only.

    python3 tools/sprite_build.py
"""
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, 'art')
CPP = os.path.join(ROOT, 'firmware', 'lib', 'pet', 'src', 'pet_sprites.cpp')

# The pet states, in PetState order (firmware/lib/pet/src/pet_render.h).
STATES = ['sleepy', 'sad', 'worried', 'happy', 'hyped', 'asleep']
FACE_X, FACE_Y = 11, 12
MONO = ['live0', 'live1', 'alert', 'sun', 'cloud', 'snow', 'wifi', 'off']
FONT_NAMES = {'colon': ':', 'space': ' '}


class ArtError(Exception):
    pass


def read_palette(path):
    """[(char, rgb or None)] in slot order."""
    out = []
    with open(path) as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            parts = line.split(None, 2)
            if len(parts) < 2 or len(parts[0]) != 1:
                raise ArtError(f'{path}:{n}: expected "<char> <#RRGGBB|none> <role>"')
            char, value = parts[0], parts[1]
            if value == 'none':
                rgb = None
            elif len(value) == 7 and value[0] == '#':
                try:
                    rgb = int(value[1:], 16)
                except ValueError:
                    raise ArtError(f'{path}:{n}: bad color {value}')
            else:
                raise ArtError(f'{path}:{n}: bad color {value}')
            if any(c == char for c, _ in out):
                raise ArtError(f'{path}:{n}: duplicate char {char}')
            out.append((char, rgb))
    if not out or out[0][1] is not None:
        raise ArtError(f'{path}: slot 0 must be the transparent "none" entry')
    return out


def read_sections(path):
    """{name: (args, rows, first_line)} from "== name [row]" headers followed by grid rows."""
    out, current = {}, None
    with open(path) as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip('\n')
            if line.startswith('== '):
                words = line[3:].split()
                if not words:
                    raise ArtError(f'{path}:{n}: empty section name')
                args = []
                if len(words) > 1 and words[-1].lstrip('-').isdigit():   # "== umbrella -6": a row argument
                    args = [int(words.pop())]
                name = ' '.join(words)
                if name in out:
                    raise ArtError(f'{path}:{n}: duplicate section {name}')
                current = out[name] = (args, [], n + 1)
            elif not line:
                current = None
            elif line.startswith('# '):   # a comment; grid rows never contain spaces ("#" is ink in font.txt)
                continue
            elif current is None:
                raise ArtError(f'{path}:{n}: grid row outside a section')
            else:
                current[1].append(line)
    return out


def check_grid(path, name, rows, first, allowed, width=None, height=None):
    if not rows:
        raise ArtError(f'{path}:{first}: {name} is empty')
    w = width or len(rows[0])
    for i, row in enumerate(rows):
        if len(row) != w:
            raise ArtError(f'{path}:{first + i}: {name} row is {len(row)} wide, expected {w}')
        bad = set(row) - allowed
        if bad:
            raise ArtError(f'{path}:{first + i}: {name} has unknown characters {"".join(sorted(bad))}')
    if height is not None and len(rows) != height:
        raise ArtError(f'{path}:{first}: {name} has {len(rows)} rows, expected {height}')


def load(art=ART):
    """Reads and validates every art file. Raises ArtError naming the file and line."""
    palette = read_palette(os.path.join(art, 'palette.txt'))
    chars = {c: i for i, (c, _) in enumerate(palette)}
    pet_path = os.path.join(art, 'trackie.txt')
    pet = read_sections(pet_path)
    if 'body' not in pet:
        raise ArtError(f'{pet_path}: missing "== body"')
    _, body, first = pet['body']
    check_grid(pet_path, 'body', body, first, set(chars), 32, 32)
    faces, extras = {}, {}
    for state in STATES:
        key = 'sleepy' if state == 'asleep' else state
        if f'face {key}' not in pet:
            raise ArtError(f'{pet_path}: missing "== face {key}"')
        _, rows, first = pet[f'face {key}']
        check_grid(pet_path, f'face {key}', rows, first, set(chars), 10, 8)
        faces[state] = rows
        if f'extra {key}' in pet:
            _, rows, first = pet[f'extra {key}']
            check_grid(pet_path, f'extra {key}', rows, first, set(chars), 7, 7)
            extras[state] = rows
    ov_path = os.path.join(art, 'overlays.txt')
    ov = read_sections(ov_path)
    overlays = {}
    for name in ('umbrella', 'scarf'):
        if name not in ov or len(ov[name][0]) != 1:
            raise ArtError(f'{ov_path}: missing "== {name} <row>"')
        args, rows, first = ov[name]
        check_grid(ov_path, name, rows, first, set(chars), 32)
        overlays[name] = (args[0], rows)
    gl_path = os.path.join(art, 'glyphs.txt')
    gl = read_sections(gl_path)
    mono = {}
    for name in MONO:
        if name not in gl:
            raise ArtError(f'{gl_path}: missing "== {name}"')
        _, rows, first = gl[name]
        check_grid(gl_path, name, rows, first, {'.', 'K'})
        mono[name] = rows
    font_path = os.path.join(art, 'font.txt')
    font = {}
    for name, (_, rows, first) in read_sections(font_path).items():
        check_grid(font_path, name, rows, first, {'.', '#'}, height=7)
        ch = FONT_NAMES.get(name, name)
        if len(ch) != 1:
            raise ArtError(f'{font_path}:{first}: font section names one character (or colon, space)')
        font[ch] = rows
    return {'palette': palette, 'chars': chars, 'body': body, 'faces': faces, 'extras': extras,
            'overlays': overlays, 'mono': mono, 'font': font}


def compose(art, state):
    """The 32x32 pet for a state: the body with its face on the door."""
    rows = [list(r) for r in art['body']]
    for dy, row in enumerate(art['faces'][state]):
        for dx, c in enumerate(row):
            if c != '.':
                rows[FACE_Y + dy][FACE_X + dx] = c
    return [''.join(r) for r in rows]


def c_bytes(rows, index):
    flat = [index[c] for row in rows for c in row]
    lines = []
    for i in range(0, len(flat), 32):
        lines.append('    ' + ', '.join(str(v) for v in flat[i:i + 32]) + ',')
    return '\n'.join(lines)


def cpp(art):
    idx = art['chars']
    mono_idx = {'.': 0, 'K': 1, '#': 1}
    out = ['// Generated by tools/sprite_build.py from art/*.txt. Do not edit; edit the grids and rerun.',
           '#include "pet_sprites.h"', '', 'namespace pet {', '']
    pal = ', '.join('0x000000' if rgb is None else f'0x{rgb:06X}' for _, rgb in art['palette'])
    out.append(f'const uint32_t kPalette[kPaletteSize] = {{{pal}}};')
    out.append(f'static_assert(kPaletteSize == {len(art["palette"])}, "palette size matches art/palette.txt");')
    out.append('')

    def sprite(name, rows, index, kind='uint8_t'):
        out.append(f'static const {kind} {name}_px[] = {{')
        out.append(c_bytes(rows, index))
        out.append('};')
        return f'{{{len(rows[0])}, {len(rows)}, {name}_px}}'

    pets = [sprite(f'pet_{s}', compose(art, s), idx) for s in STATES]
    out.append(f'const Sprite kPets[6] = {{{", ".join(pets)}}};')
    ext = [sprite(f'extra_{s}', art['extras'][s], idx) if s in art['extras'] else '{0, 0, nullptr}' for s in STATES]
    out.append(f'const Sprite kExtras[6] = {{{", ".join(ext)}}};')
    for name, (row, rows) in art['overlays'].items():
        out.append(f'const Overlay kOverlay{name.capitalize()} = {{{sprite("overlay_" + name, rows, idx)}, {row}}};')
    for name, rows in art['mono'].items():
        out.append(f'const Sprite kGlyph{name[0].upper() + name[1:]} = {sprite("glyph_" + name, rows, mono_idx)};')
    font = sorted(art['font'].items())
    entries = []
    for i, (ch, rows) in enumerate(font):
        s = sprite(f'font_{i}', rows, mono_idx)
        entries.append(f"{{'{ch}', {s}}}" if ch not in "'\\" else f"{{'\\{ch}', {s}}}")
    out.append(f'const FontGlyph kFont[] = {{{", ".join(entries)}}};')
    out.append(f'const size_t kFontCount = {len(font)};')
    out += ['', '}  // namespace pet', '']
    return '\n'.join(out)


def png(rows, palette_rgb, scale=8):
    """An RGBA PNG of index rows (slot 0 transparent)."""
    w, h = len(rows[0]) * scale, len(rows) * scale
    raw = bytearray()
    for row in rows:
        line = bytearray()
        for i in row:
            rgb = palette_rgb[i]
            px = bytes([0, 0, 0, 0]) if rgb is None else bytes([rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255, 255])
            line += px * scale
        for _ in range(scale):
            raw += b'\x00' + line

    def chunk(tag, data):
        c = tag + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)

    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def previews(art, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    idx = art['chars']
    pal = [rgb for _, rgb in art['palette']]
    written = []
    for s in STATES:
        rows = [[idx[c] for c in r] for r in compose(art, s)]
        if s != 'asleep':   # show the umbrella above, like the tile
            row, umb = art['overlays']['umbrella']
            rows = [[idx[c] for c in r] for r in umb[:-row]] + rows
        path = os.path.join(out_dir, f'trackie_{s}.png')
        with open(path, 'wb') as f:
            f.write(png(rows, pal))
        written.append(path)
    for name, rows in art['mono'].items():
        path = os.path.join(out_dir, f'glyph_{name}.png')
        with open(path, 'wb') as f:
            f.write(png([[0 if c == '.' else 1 for c in r] for r in rows], [None, 0xFFFFFF]))
        written.append(path)
    return written


def main():
    try:
        art = load()
    except ArtError as e:
        print(f'error: {e}', file=sys.stderr)
        return 1
    with open(CPP, 'w') as f:
        f.write(cpp(art))
    written = previews(art, os.path.join(ART, 'build'))
    print(f'wrote {os.path.relpath(CPP, ROOT)} and {len(written)} previews in art/build/')
    return 0


if __name__ == '__main__':
    sys.exit(main())
