"""Build Trackie's sprites: art/ text grids to firmware/lib/pet/src/pet_sprites.cpp and PNG previews.

Plan 1, task 11. Standard library only. The art is the app's Trackie (ios/CTATracker/Design/Pixel.swift):
one 32x32 body, a 10x8 face per state stamped on the door at columns 11-20, rows 12-19, a 7x7 extra that
floats beside him, and umbrella and scarf overlays. Grid files: one row per line, one palette character per
pixel; lines starting with "#" are comments; extras and overlays have one "@ dx dy" line placing the grid relative
to the body's top left.

    python3 tools/sprite_build.py            # writes pet_sprites.cpp and art/build/*.png
"""
import argparse
import os
import re
import struct
import sys
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
STATES = ["sleepy", "sad", "worried", "happy", "hyped", "asleep"]  # PetState order
EXTRA_FILES = ["happy", "hyped", "worried", "sleepy"]  # Sad has none; Asleep reuses Sleepy's
EXTRA_FOR = {"sleepy": "sleepy", "worried": "worried", "happy": "happy", "hyped": "hyped", "asleep": "sleepy"}
BODY_SIZE = (32, 32)
FACE_SIZE = (10, 8)
FACE_AT = (11, 12)
EXTRA_SIZE = (7, 7)
OVERLAY_WIDTH = 32
PALETTE_SIZE = 17  # must match kPaletteSize in pet_sprites.h


class ArtError(Exception):
    """An invalid art file; the message names the file and, where it can, the line."""


class Grid:
    def __init__(self, rows, dx=0, dy=0):
        self.rows, self.dx, self.dy = rows, dx, dy

    @property
    def w(self):
        return len(self.rows[0]) if self.rows else 0

    @property
    def h(self):
        return len(self.rows)


class Art:
    def __init__(self, chars, colors, states, extras, umbrella, scarf):
        self.chars = chars  # palette characters by slot
        self.colors = colors  # 0xRRGGBB by slot (None for transparent)
        self.states = states  # state -> 32 rows of slot indexes
        self.extras = extras  # extra name -> Grid of slot indexes
        self.umbrella, self.scarf = umbrella, scarf


def load_palette(path):
    chars, colors = [], []
    with open(path) as f:
        for n, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 3:
                raise ArtError(f"{path}:{n}: expected '<char> <slot> <#RRGGBB|-> <role>'")
            ch, slot, color = parts[0], parts[1], parts[2]
            if len(ch) != 1 or not slot.isdigit() or int(slot) != len(chars):
                raise ArtError(f"{path}:{n}: slots must be single characters numbered 0, 1, 2, ... in order")
            if ch in chars:
                raise ArtError(f"{path}:{n}: '{ch}' is listed twice")
            if int(slot) == 0:
                if ch != "." or color != "-":
                    raise ArtError(f"{path}:{n}: slot 0 must be '. 0 -' (transparent)")
                colors.append(None)
            else:
                if not re.fullmatch(r"#[0-9A-Fa-f]{6}", color):
                    raise ArtError(f"{path}:{n}: '{color}' is not #RRGGBB")
                colors.append(int(color[1:], 16))
            chars.append(ch)
    if not chars:
        raise ArtError(f"{path}: no palette entries")
    return chars, colors


def load_grid(path, chars, size=None, width=None, placed=False):
    """A grid of slot indexes. size = (w, h) exactly, or width alone (overlays: any height 1-255).

    placed: the file must have exactly one "@ dx dy" line (extras and overlays); otherwise it must have none.
    Blank lines before or after the grid are ignored; a blank line inside it is an error.
    """
    index = {c: i for i, c in enumerate(chars)}
    rows, dx, dy, at_line, blank_line = [], 0, 0, None, None
    with open(path) as f:
        for n, raw in enumerate(f, 1):
            line = raw.rstrip("\r\n")
            if line.startswith("#"):
                continue
            if not line.strip():
                if rows and blank_line is None:
                    blank_line = n
                continue
            if line.startswith("@"):
                if not placed:
                    raise ArtError(f"{path}:{n}: '@ dx dy' only places extras and overlays")
                if at_line is not None:
                    raise ArtError(f"{path}:{n}: a second '@' line (the first is line {at_line})")
                try:
                    _, sdx, sdy = line.split()
                    dx, dy = int(sdx), int(sdy)
                except ValueError:
                    raise ArtError(f"{path}:{n}: expected '@ dx dy'") from None
                if not (-128 <= dx <= 127 and -128 <= dy <= 127):
                    raise ArtError(f"{path}:{n}: dx and dy must be within -128..127")
                at_line = n
                continue
            if blank_line is not None:
                raise ArtError(f"{path}:{blank_line}: blank line inside the grid")
            row = []
            for ch in line:
                if ch not in index:
                    raise ArtError(f"{path}:{n}: '{ch}' is not in palette.txt")
                row.append(index[ch])
            rows.append((n, row))
    if placed and at_line is None:
        raise ArtError(f"{path}: missing its '@ dx dy' line")
    expect_w = size[0] if size else width
    for n, row in rows:
        if len(row) != expect_w:
            raise ArtError(f"{path}:{n}: {len(row)} columns, expected {expect_w}")
    if size and len(rows) != size[1]:
        raise ArtError(f"{path}: {len(rows)} rows, expected {size[1]} rows")
    if not size and not 1 <= len(rows) <= 255:
        raise ArtError(f"{path}: {len(rows)} rows, expected 1-255")
    return Grid([row for _, row in rows], dx, dy)


def stamp(body, face):
    """The body with the face's non-transparent pixels on the door."""
    out = [row[:] for row in body.rows]
    fx, fy = FACE_AT
    for j, row in enumerate(face.rows):
        for i, v in enumerate(row):
            if v != 0:
                out[fy + j][fx + i] = v
    return out


def build(art_dir):
    p = lambda name: os.path.join(art_dir, name)  # noqa: E731
    chars, colors = load_palette(p("palette.txt"))
    body = load_grid(p("body.txt"), chars, BODY_SIZE)
    states = {s: stamp(body, load_grid(p(f"face_{s}.txt"), chars, FACE_SIZE)) for s in STATES}
    extras = {e: load_grid(p(f"extra_{e}.txt"), chars, EXTRA_SIZE, placed=True) for e in EXTRA_FILES}
    umbrella = load_grid(p("overlay_umbrella.txt"), chars, width=OVERLAY_WIDTH, placed=True)
    scarf = load_grid(p("overlay_scarf.txt"), chars, width=OVERLAY_WIDTH, placed=True)
    return Art(chars, colors, states, extras, umbrella, scarf)


def rgb565(rgb):
    r, g, b = (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def _array(name, rows):
    flat = [v for row in rows for v in row]
    lines = []
    for k in range(0, len(flat), 32):
        lines.append("    " + ", ".join(str(v) for v in flat[k:k + 32]) + ",")
    return f"const uint8_t {name}[{len(flat)}] = {{\n" + "\n".join(lines) + "\n};\n"


def cpp_source(art):
    out = ["// Generated by tools/sprite_build.py from art/. Do not edit; edit the grids and rerun it.",
           '#include "pet_sprites.h"', "", "namespace pet {", "", "namespace {", ""]
    for s in STATES:
        out.append(_array(f"kPix_{s}", art.states[s]))
    for e in EXTRA_FILES:
        out.append(_array(f"kPix_extra_{e}", art.extras[e].rows))
    out.append(_array("kPix_umbrella", art.umbrella.rows))
    out.append(_array("kPix_scarf", art.scarf.rows))
    for e in EXTRA_FILES:
        g = art.extras[e]
        out.append(f"const Sprite kExtra_{e} = {{kPix_extra_{e}, {g.w}, {g.h}, {g.dx}, {g.dy}}};")
    out += ["", f"constexpr size_t kGeneratedPaletteSize = {len(art.colors)};", "", "}  // namespace", ""]
    out.append("static_assert(kGeneratedPaletteSize == kPaletteSize, \"art/palette.txt and pet_sprites.h disagree\");")
    out.append("")
    out.append("const Sprite kPet[6] = {")
    for s in STATES:
        out.append(f"    {{kPix_{s}, 32, 32, 0, 0}},  // {s}")
    out.append("};")
    refs = ["nullptr" if s not in EXTRA_FOR else f"&kExtra_{EXTRA_FOR[s]}" for s in STATES]
    out.append("const Sprite* const kPetExtra[6] = {" + ", ".join(refs) + "};  // " + ", ".join(STATES))
    for name, g in (("Umbrella", art.umbrella), ("Scarf", art.scarf)):
        out.append(f"const Sprite kOverlay{name} = {{kPix_{name.lower()}, {g.w}, {g.h}, {g.dx}, {g.dy}}};")
    pal = ", ".join("0x0000" if c is None else f"0x{rgb565(c):04X}" for c in art.colors)
    out.append(f"const uint16_t kDefaultPalette565[kPaletteSize] = {{{pal}}};")
    out += ["", "}  // namespace pet", ""]
    return "\n".join(out)


def _png(path, pixels, w, h):
    """An RGBA PNG; pixels is h rows of w (r, g, b, a) tuples."""
    raw = b"".join(b"\x00" + b"".join(struct.pack("BBBB", *px) for px in row) for row in pixels)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def write_previews(art, png_dir, scale=8):
    """Each state plain, with the umbrella, and with the scarf, at `scale`, on black like the handheld."""
    os.makedirs(png_dir, exist_ok=True)
    layers_all = [(art.umbrella.rows, art.umbrella.dx, art.umbrella.dy), (art.scarf.rows, art.scarf.dx, art.scarf.dy)]
    x0 = min(0, *(g.dx for g in art.extras.values()), *(dx for _, dx, _ in layers_all))
    y0 = min(0, *(g.dy for g in art.extras.values()), *(dy for _, _, dy in layers_all))
    x1 = max(32, *(g.dx + g.w for g in art.extras.values()))
    y1 = max(32, *(dy + len(rows) for rows, _, dy in layers_all), *(g.dy + g.h for g in art.extras.values()))
    w, h = x1 - x0, y1 - y0
    written = []
    for s in STATES:
        extra = art.extras.get(EXTRA_FOR.get(s, ""), None)
        for suffix, overlay in (("", None), ("_umbrella", art.umbrella), ("_scarf", art.scarf)):
            canvas = [[0] * w for _ in range(h)]
            layers = [(art.states[s], 0, 0)]
            if extra:
                layers.append((extra.rows, extra.dx, extra.dy))
            if overlay:
                layers.append((overlay.rows, overlay.dx, overlay.dy))
            for rows, dx, dy in layers:
                for j, row in enumerate(rows):
                    for i, v in enumerate(row):
                        if v:
                            canvas[dy + j - y0][dx + i - x0] = v
            pixels = []
            for row in canvas:
                line = []
                for v in row:
                    c = art.colors[v] if v else 0x000000
                    line += [((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255)] * scale
                pixels += [line] * scale
            path = os.path.join(png_dir, f"{s}{suffix}.png")
            _png(path, pixels, w * scale, h * scale)
            written.append(path)
    return written


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--art", default=os.path.join(ROOT, "art"))
    ap.add_argument("--cpp", default=os.path.join(ROOT, "firmware", "lib", "pet", "src", "pet_sprites.cpp"))
    ap.add_argument("--png-dir", default=os.path.join(ROOT, "art", "build"))
    ap.add_argument("--no-palette-size-check", dest="cpp_palette_check", action="store_false",
                    help="skip matching kPaletteSize (for test art)")
    args = ap.parse_args(argv)
    try:
        art = build(args.art)
        if args.cpp_palette_check and len(art.colors) != PALETTE_SIZE:
            raise ArtError(f"palette.txt has {len(art.colors)} slots, but pet_sprites.h's kPaletteSize is {PALETTE_SIZE}")
    except (ArtError, OSError) as e:
        print(f"sprite_build: {e}", file=sys.stderr)
        return 1
    with open(args.cpp, "w") as f:
        f.write(cpp_source(art))
    written = write_previews(art, args.png_dir)
    print(f"wrote {args.cpp} and {len(written)} previews in {args.png_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
