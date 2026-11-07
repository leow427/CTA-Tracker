"""Tests for sprite_build.py (plan 1, task 11): Trackie's art grids to C++ arrays and PNG previews."""
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import sprite_build  # noqa: E402

PALETTE = """\
# char slot color role
. 0 - transparent
K 1 #0B0D14 outline
B 2 #2D6BE6 body
W 3 #FFFFFF face
"""


def grid(w, h, fill="."):
    return "\n".join(fill * w for _ in range(h)) + "\n"


class Art:
    """A minimal valid art directory in a temp folder, removed after the test."""

    def __init__(self, test):
        self.dir = tempfile.mkdtemp()
        test.addCleanup(shutil.rmtree, self.dir)
        self.write("palette.txt", PALETTE)
        body = ["." * 32 for _ in range(32)]
        body[12] = "." * 11 + "B" * 10 + "." * 11  # door row the face lands on
        self.write("body.txt", "\n".join(body) + "\n")
        for state in sprite_build.STATES:
            face = ["." * 10 for _ in range(8)]
            face[0] = "W" + "." * 9
            self.write(f"face_{state}.txt", "\n".join(face) + "\n")
        for name in sprite_build.EXTRA_FILES:
            self.write(f"extra_{name}.txt", "@ 28 -4\n" + grid(7, 7, "W"))
        self.write("overlay_umbrella.txt", "@ 0 -6\n" + grid(32, 7, "K"))
        self.write("overlay_scarf.txt", "@ 0 22\n" + grid(32, 5, "B"))

    def write(self, name, text):
        with open(os.path.join(self.dir, name), "w") as f:
            f.write(text)

    def path(self, name):
        return os.path.join(self.dir, name)


class SpriteBuildTests(unittest.TestCase):
    def test_rejects_short_grid(self):
        art = Art(self)
        art.write("body.txt", grid(32, 31))
        with self.assertRaises(sprite_build.ArtError) as e:
            sprite_build.build(art.dir)
        self.assertIn("body.txt", str(e.exception))
        self.assertIn("32 rows", str(e.exception))

    def test_rejects_unknown_char(self):
        art = Art(self)
        rows = grid(32, 32).splitlines()
        rows[5] = "." * 31 + "Q"
        art.write("body.txt", "\n".join(rows) + "\n")
        with self.assertRaises(sprite_build.ArtError) as e:
            sprite_build.build(art.dir)
        self.assertIn("body.txt:6", str(e.exception))  # the file and line
        self.assertIn("'Q'", str(e.exception))

    def test_rejects_wrong_face_size(self):
        art = Art(self)
        art.write("face_happy.txt", grid(9, 8))
        with self.assertRaises(sprite_build.ArtError) as e:
            sprite_build.build(art.dir)
        self.assertIn("face_happy.txt", str(e.exception))

    def test_rejects_bad_palette(self):
        art = Art(self)
        art.write("palette.txt", PALETTE + "Z 5 #FFFFFF gap\n")  # slots must run 0..n-1
        with self.assertRaises(sprite_build.ArtError):
            sprite_build.build(art.dir)

    def expect_error(self, art, *needles):
        with self.assertRaises(sprite_build.ArtError) as e:
            sprite_build.build(art.dir)
        for needle in needles:
            self.assertIn(needle, str(e.exception))

    def test_line_numbers_count_comments(self):
        art = Art(self)
        rows = grid(32, 32).splitlines()
        rows[19] = "." * 31  # one short row
        art.write("body.txt", "# a comment\n# another\n" + "\n".join(rows) + "\n")
        self.expect_error(art, "body.txt:22:", "31 columns")  # 2 comment lines + row 20

    def test_blank_lines(self):
        art = Art(self)
        art.write("body.txt", "\n" + grid(32, 32) + "\n\n")  # around the grid: fine
        sprite_build.build(art.dir)
        rows = grid(32, 32).splitlines()
        art.write("body.txt", "\n".join(rows[:10]) + "\n\n" + "\n".join(rows[10:]) + "\n")
        self.expect_error(art, "body.txt:11:", "blank line inside the grid")

    def test_placement_lines(self):
        art = Art(self)
        art.write("extra_happy.txt", grid(7, 7, "W"))
        self.expect_error(art, "extra_happy.txt", "missing its '@ dx dy' line")
        art = Art(self)
        art.write("overlay_scarf.txt", "@ 0 22\n@ 0 23\n" + grid(32, 5, "B"))
        self.expect_error(art, "overlay_scarf.txt:2:", "second '@'")
        art = Art(self)
        art.write("body.txt", "@ 1 1\n" + grid(32, 32))
        self.expect_error(art, "body.txt:1:", "only places extras and overlays")
        art = Art(self)
        art.write("extra_hyped.txt", "@ 200 0\n" + grid(7, 7, "W"))
        self.expect_error(art, "extra_hyped.txt:1:", "-128..127")

    def test_overlay_shape(self):
        art = Art(self)
        art.write("overlay_umbrella.txt", "@ 0 -6\n" + grid(31, 7, "K"))
        self.expect_error(art, "overlay_umbrella.txt:2:", "expected 32")
        art = Art(self)
        art.write("overlay_umbrella.txt", "@ 0 -6\n")
        self.expect_error(art, "overlay_umbrella.txt", "expected 1-255")

    def test_palette_rules(self):
        for bad, needle in [(PALETTE + "K 4 #000000 dup\n", "listed twice"),
                            (PALETTE.replace(". 0 -", "X 0 -"), "slot 0"),
                            (PALETTE + "Z 4 #-7FD3F sign\n", "#RRGGBB"),
                            (PALETTE + "Z 4 #0x12ab hex\n", "#RRGGBB"),
                            (PALETTE + "Z 4 12AB34 nohash\n", "#RRGGBB")]:
            art = Art(self)
            art.write("palette.txt", bad)
            self.expect_error(art, "palette.txt", needle)

    def test_previews_show_overlays(self):
        art = Art(self)
        out = sprite_build.build(art.dir)
        png_dir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, png_dir)
        written = sprite_build.write_previews(out, png_dir, scale=1)
        plain = open(os.path.join(png_dir, "happy.png"), "rb").read()
        umbrella = open(os.path.join(png_dir, "happy_umbrella.png"), "rb").read()
        self.assertNotEqual(plain, umbrella)
        self.assertEqual(len(written), 18)

    def test_face_stamp(self):
        art = Art(self)
        out = sprite_build.build(art.dir)
        pal = {c: i for i, c in enumerate(out.chars)}
        happy = out.states["happy"]
        self.assertEqual(happy[12][11], pal["W"])  # the face's W replaces the door
        self.assertEqual(happy[12][12], pal["B"])  # the face's "." keeps the body
        self.assertEqual(happy[11][11], 0)  # nothing outside columns 11-20, rows 12-19 changes

    def test_cpp_has_all_states(self):
        art = Art(self)
        out = sprite_build.build(art.dir)
        cpp = sprite_build.cpp_source(out)
        self.assertIn("const Sprite kPet[6]", cpp)
        for state in sprite_build.STATES:
            self.assertIn(f"kPix_{state}", cpp)
        self.assertIn("const Sprite* const kPetExtra[6]", cpp)
        extras = cpp[cpp.index("kPetExtra[6]"):].split(";")[0]
        self.assertEqual(extras.count("nullptr"), 1)  # Sad has no extra
        self.assertIn("kDefaultPalette565[kPaletteSize]", cpp)
        self.assertIn("static_assert", cpp)

    def test_png_signature(self):
        art = Art(self)
        out = sprite_build.build(art.dir)
        png_dir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, png_dir)
        written = sprite_build.write_previews(out, png_dir, scale=2)
        self.assertEqual(len(written), 18)  # 6 states, each plain, with umbrella, with scarf
        with open(written[0], "rb") as f:
            self.assertEqual(f.read(8), b"\x89PNG\r\n\x1a\n")

    def test_main_exit_codes(self):
        art = Art(self)
        out_dir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, out_dir)
        cpp = os.path.join(out_dir, "pet_sprites.cpp")
        self.assertEqual(sprite_build.main(["--art", art.dir, "--cpp", cpp, "--png-dir", out_dir, "--no-palette-size-check"]), 0)
        self.assertTrue(os.path.exists(cpp))
        art.write("body.txt", grid(32, 31))
        self.assertEqual(sprite_build.main(["--art", art.dir, "--cpp", cpp, "--png-dir", out_dir, "--no-palette-size-check"]), 1)

    def test_main_checks_palette_size(self):
        art = Art(self)  # 4 slots, not kPaletteSize
        out_dir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, out_dir)
        cpp = os.path.join(out_dir, "pet_sprites.cpp")
        self.assertEqual(sprite_build.main(["--art", art.dir, "--cpp", cpp, "--png-dir", out_dir]), 1)
        self.assertFalse(os.path.exists(cpp))


if __name__ == "__main__":
    unittest.main()
