import os
import re
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sprite_build  # noqa: E402

ROOT = sprite_build.ROOT
PIXEL_SWIFT = os.path.join(ROOT, 'ios', 'CTATracker', 'Design', 'Pixel.swift')
KIT_SWIFT = os.path.join(ROOT, 'ios', 'CTAKit', 'Sources', 'CTAKit', 'Pixel.swift')


def strings(block):
    return re.findall(r'"([^"]*)"', block)


class SpriteBuildTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.art = os.path.join(self.tmp, 'art')
        shutil.copytree(sprite_build.ART, self.art, ignore=shutil.ignore_patterns('build'))

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def edit(self, name, old, new):
        path = os.path.join(self.art, name)
        text = open(path).read()
        self.assertIn(old, text)
        open(path, 'w').write(text.replace(old, new, 1))

    def test_loads_real_art(self):
        art = sprite_build.load(self.art)
        self.assertEqual(len(art['body']), 32)
        self.assertEqual(set(art['faces']), set(sprite_build.STATES))
        self.assertNotIn('sad', art['extras'])
        self.assertEqual(art['overlays']['umbrella'][0], -6)
        self.assertIn('9', art['font'])

    def test_rejects_short_grid(self):
        self.edit('trackie.txt', '................................\n\n== face happy', '\n== face happy')
        with self.assertRaisesRegex(sprite_build.ArtError, r'trackie\.txt.*31 rows'):
            sprite_build.load(self.art)

    def test_rejects_unknown_char(self):
        self.edit('trackie.txt', '..KKKKKKKKKKKK..', '..KKKKKKQKKKKK..')
        with self.assertRaisesRegex(sprite_build.ArtError, r'trackie\.txt:\d+: body has unknown characters Q'):
            sprite_build.load(self.art)

    def test_rejects_ragged_glyph(self):
        self.edit('glyphs.txt', '== sun\n..K..', '== sun\n..K...')
        with self.assertRaisesRegex(sprite_build.ArtError, r'glyphs\.txt:\d+: sun row'):
            sprite_build.load(self.art)

    def test_cpp_has_all_states(self):
        out = sprite_build.cpp(sprite_build.load(self.art))
        self.assertRegex(out, r'const Sprite kPets\[6\] = \{(\{32, 32, pet_\w+_px\}, ){5}\{32, 32, pet_asleep_px\}\}')
        self.assertIn('{0, 0, nullptr}', out)   # sad has no extra
        self.assertIn('kOverlayUmbrella', out)
        self.assertIn("{':', ", out)

    def test_png_signature(self):
        paths = sprite_build.previews(sprite_build.load(self.art), os.path.join(self.tmp, 'build'))
        self.assertEqual(len(paths), 6 + len(sprite_build.MONO))
        with open(paths[0], 'rb') as f:
            self.assertEqual(f.read(8), b'\x89PNG\r\n\x1a\n')

    def test_generated_cpp_is_current(self):
        with open(sprite_build.CPP) as f:
            self.assertEqual(f.read(), sprite_build.cpp(sprite_build.load()), 'rerun tools/sprite_build.py')

    def test_matches_the_iphone_art(self):
        """The devices draw the app's Trackie, glyphs and digits pixel for pixel."""
        art = sprite_build.load()
        sw = open(PIXEL_SWIFT).read()
        self.assertEqual(art['body'], strings(re.search(r'petBody = \[(.*?)\]', sw, re.S).group(1)))
        chars = re.search(r'petChars = "([^"]+)"', sw).group(1)
        pal = [v.strip() for v in re.search(r'petPalette: \[UInt32\] = \[(.*?)\]', sw, re.S).group(1).split(',')]
        ours = art['palette'][:len(chars)]
        self.assertEqual(''.join(c for c, _ in ours), chars)
        self.assertEqual([None if v == '0' else int(v, 16) for v in pal], [rgb for _, rgb in ours])
        for kind, table in (('face', 'petFaces'), ('extra', 'petExtras')):
            block = re.search(table + r': .*?= \[(.*?)\n    \]', sw, re.S).group(1)
            theirs = {m: strings(b) for m, b in re.findall(r'\.(\w+): \[((?:\s*"[^"]*",?)+)\s*\]', block)}
            mine = art['faces'] if kind == 'face' else art['extras']
            for mood, rows in theirs.items():
                self.assertEqual(mine[mood], rows, f'{kind} {mood}')
            self.assertEqual(set(theirs), set(mine) - {'asleep'})
        live = re.search(r'static let live = \[glyph\(\[(.*?)\]\),\s*glyph\(\[(.*?)\]\)\]', sw, re.S)
        self.assertEqual(art['mono']['live0'], strings(live.group(1)))
        self.assertEqual(art['mono']['live1'], strings(live.group(2)))
        self.assertEqual(art['mono']['alert'], strings(re.search(r'static let alert = glyph\(\[(.*?)\]\)', sw, re.S).group(1)))
        kit = open(KIT_SWIFT).read()
        block = re.search(r'glyphs: \[Character: \[String\]\] = \[(.*?)\n    \]', kit, re.S).group(1)
        theirs = {ch: strings(rows) for ch, rows in re.findall(r'"(.)": \[((?:\s*"[^"]*",?)+)\s*\]', block)}
        self.assertTrue(theirs)
        for ch, rows in theirs.items():
            self.assertEqual(art['font'][ch], rows, f'glyph {ch!r}')


if __name__ == '__main__':
    unittest.main()
