#pragma once
// A small 2D canvas over an RGB565 buffer: just the drawing the handheld's and the dock's HTML mockups use (pixel
// plates, Nunito text, 5×7 pixel digits, pixel art, anti-aliased circles, strokes, and arcs, and the dock map's
// block lines and dots). Plain C++ so the screens can be rendered on a laptop for previews (each device's
// tools/preview.cpp) as well as on the device. Shared by the handheld and the dock (firmware/lib/gfx).
//
// A canvas covers either a whole row-major screen or one band of screen rows stored the way the panel wants it
// (rotated a quarter turn right and byte-swapped), so the device can draw a frame band by band in internal RAM
// and send each band as it is. Drawing outside the canvas's band is skipped cheaply.
#include <math.h>
#include <stddef.h>
#include <stdint.h>

namespace ui {

// lroundf(v) as an int, without the library call when |v| < 2^23 (every coordinate, alpha and pen position the screens
// draw with): the same result, halfway cases away from zero. v minus its truncation is exact there, so comparing it
// with one half decides the rounding exactly as lroundf does. Anything larger, infinite or NaN goes to lroundf itself.
inline int roundi(float v) {
    if (!(fabsf(v) < 8388608.f)) return static_cast<int>(lroundf(v));
    const int i = static_cast<int>(v);   // toward zero
    const float f = v - static_cast<float>(i);
    return f >= 0.5f ? i + 1 : f <= -0.5f ? i - 1 : i;
}

// 0xRRGGBB colors.
namespace col {
constexpr uint32_t bg = 0x000000, navy = 0x0b1a36, raised = 0x17294f, mist = 0x9db0d8, headlight = 0xffd95a,
                   cream = 0xf4ecc8, signal = 0x34c759, amber = 0xffb000, white = 0xffffff, dimInk = 0x5c6d93;
}

enum class Align : uint8_t { Left, Center, Right };
enum class Base : uint8_t { Top, Middle, Alphabetic };

struct Font;
class Recording;

struct PlateStyle {
    uint32_t fill = col::raised, fill2 = col::raised;
    uint32_t rim = col::white;
    float rimAlpha = 0.22f;
    int rimW = 2, edge = 5, block = 4;
    uint8_t steps[3] = {3, 1, 1};
    uint8_t nSteps = 3;
    bool highlight = true;
};
PlateStyle smallPlate();   // the mockup's SMALL: 3 px blocks, steps 2, 1

class Canvas {
public:
    // A whole w × h screen, row-major, native byte order.
    Canvas(uint16_t* px, int w, int h);
    // Screen rows [y0, y0 + rows) of a screenW-wide screen, laid out for the panel: rotated a quarter turn right
    // (buffer row r is screen column r, and within it screen row y is at y0 + rows - 1 - y) and byte-swapped.
    // That is exactly the panel window for columns [screenW - y0 - rows, screenW - y0), all rows.
    static Canvas panelBand(uint16_t* px, int screenW, int y0, int rows);
    // Only screen columns [x0, x1) of such a band: px holds (x1 - x0) * rows pixels, buffer row r being screen column
    // x0 + r. That is the panel window for columns [screenW - y0 - rows, screenW - y0), rows [x0, x1).
    static Canvas panelWindow(uint16_t* px, int screenW, int x0, int x1, int y0, int rows);
    // Any layout: screen pixel (x, y), for x in [x0, x1) and y in [y0, y1), is px[org + x * sx + y * sy], native byte
    // order (the dock draws bands of its portrait screen straight in its landscape panel's order). px holds exactly
    // (x1 - x0) * (y1 - y0) pixels, which clear() fills.
    static Canvas strided(uint16_t* px, int org, int sx, int sy, int x0, int y0, int x1, int y1);

    // State, like ctx.save()/restore() in the mockup: a translation, a clip rectangle, and a global alpha.
    int ox = 0, oy = 0;
    float alpha = 1.f;
    void clip(int x, int y, int w, int h);   // in current (translated) coordinates, intersected with the canvas
    void resetClip();
    // True when nothing of this rectangle (current coordinates) lands inside the clip, so drawing it can be skipped.
    bool hidden(float x, float y, float w, float h) const {
        return outside(static_cast<int>(x) + ox - 1, static_cast<int>(y) + oy - 1, static_cast<int>(x + w) + ox + 2,
                       static_cast<int>(y + h) + oy + 2);
    }

    void clear(uint32_t rgb = 0);   // the whole canvas
    void fillRect(float x, float y, float w, float h, uint32_t rgb, float a = 1.f);
    void blend(int x, int y, uint32_t rgb, int a255);   // device coordinates, clipped

    // A stepped-corner pixel shape (the app's PixelShape) and a raised plate built from it.
    void fillPixelShape(int x, int y, int w, int h, int block, const uint8_t* steps, int nSteps, uint32_t rgb,
                        float a = 1.f, bool bottom = true);
    void plate(int x, int y, int w, int h, const PlateStyle& s);
    int chip(const char* text, int x, int y, uint32_t bg, uint32_t fg);   // returns its width

    // Nunito text. weight 800 or 900; sizes are the ones firmware/lib/gfx/tools/font_build.html renders.
    static const Font* font(int weight, int size);
    static float measure(const Font* f, const char* text);
    // Whether the font has a glyph for every character (the large title sizes carry letters and digits only).
    static bool covers(const Font* f, const char* text);
    // Shortens text with "…" to fit maxW, into out (cap bytes).
    static const char* fit(const Font* f, const char* text, float maxW, char* out, size_t cap);
    float text(const Font* f, const char* text, float x, float y, uint32_t rgb, Align al = Align::Left,
               Base base = Base::Top, float a = 1.f);
    // Text over a solid r px outline in `outline` (the app's OutlinedText, on bus cards): the glyphs' coverage spread
    // r px every way, drawn once under the text.
    float textOutlined(const Font* f, const char* text, float x, float y, uint32_t rgb, uint32_t outline, int r,
                       Align al = Align::Left, Base base = Base::Top);

    // The app's 5×7 pixel digits ("0"–"9", ":", "D", "u", "e", " "). y is the top. Returns the width.
    static int pixelWidth(const char* text, int s);
    // grow: each pixel drawn that much bigger every way (the digits' outline, drawn under them).
    int pixelText(const char* text, float x, int y, int s, uint32_t rgb, Align al = Align::Left, int grow = 0);

    // Pixel art from rows of characters: any char other than '.' is drawn in rgb.
    void ink(const char* const* rows, int nRows, int x, int y, int s, uint32_t rgb, float a = 1.f);
    // An indexed sprite (pet::Sprite's layout): w × h palette indexes, 0 transparent, placed dx, dy art pixels
    // from (x, y), each art pixel an s × s block of pal565[index] (native RGB565). Same rule as pet::renderSprite.
    void sprite(const uint8_t* idx, int w, int h, int dx, int dy, int x, int y, const uint16_t* pal565, int s);

    // Anti-aliased shapes.
    void fillCircle(float cx, float cy, float r, uint32_t rgb, float a = 1.f);
    void line(float x0, float y0, float x1, float y1, float width, uint32_t rgb, float a = 1.f);   // round caps
    // Stroke an arc of radius r from angle a0 to a1 (radians, clockwise from +x, like canvas), width lw.
    void arc(float cx, float cy, float r, float a0, float a1, float lw, uint32_t rgb, float a = 1.f,
             bool roundCaps = false);
    void arrowCircle(float cx, float cy, float r, uint32_t rgb, uint32_t inkRgb);   // arrow.right.circle.fill

    // Crisp, not anti-aliased (the dock map's track, to match the pixel art). seg: a Bresenham line from the rounded
    // ends, a b × b block (top left b/2 up and left of each point) at every step. dot: a size × size square centered
    // on the rounded (x, y) (top left size/2 up and left) with its four corner pixels knocked off.
    void seg(float x0, float y0, float x1, float y1, int b, uint32_t rgb, float a = 1.f);
    void dot(float x, float y, int size, uint32_t rgb, float a = 1.f);
    // A grid of cells (the dock map's pixel Chicago, cta::basemapRaster): cell (i, j), grid[j * cols + i], is the
    // cell × cell square at (x + i * cell, y + j * cell), in pal[grid value] (values past nPal, and skip, are left
    // alone: the dock's land is the black the screen is already cleared to). Opaque; alpha doesn't apply. Recorded as
    // one call: the grid isn't copied, so it must stay as it is until the frame is played, and version must change
    // whenever its cells do (it stands for them in the bands' hashes).
    void cells(const uint8_t* grid, int cols, int rows, int x, int y, int cell, const uint32_t* pal, int nPal,
               uint32_t version, int skip = -1);

    // Scaled blits of a native row-major RGB565 image centered on (cx, cy), with `key` pixels transparent and
    // global alpha a: bilinear, and nearest (rougher, much cheaper, for frames in motion). overBlack promises the
    // image lands on black, so fading it needn't read what's under it (same result, faster).
    void blitScaled(const uint16_t* src, int sw, int sh, uint16_t key, float cx, float cy, float scale, float a,
                    bool overBlack = false);
    void blitNearest(const uint16_t* src, int sw, int sh, uint16_t key, float cx, float cy, float scale, float a,
                     bool overBlack = false);

private:
    friend class Recording;
    Canvas() = default;
    uint16_t& at(int x, int y) { return px_[org_ + x * sx_ + y * sy_]; }
    uint16_t enc(uint16_t c) const { return swap_ ? static_cast<uint16_t>(c << 8 | c >> 8) : c; }
    uint16_t dec(uint16_t c) const { return enc(c); }
    void fill565(int x0, int y0, int x1, int y1, uint16_t c);   // device coordinates, clipped, opaque
    // Columns [x0, x1) of rows [y0, y1), row y in col[y - y0] (native RGB565): device coordinates, clipped, opaque.
    // On a band, where a column's rows run along memory, each column is written a word (two pixels) at a time.
    static constexpr int kRun = 48;   // the most rows written that way (a band: the dock's 32, the handheld's 48)
    void fillPattern(int x0, int y0, int x1, int y1, const uint16_t* col);
    // Device rectangle [x0, x1) × [y0, y1), already clipped, blended toward c (native RGB565) by a255 / 255 (0 < a255
    // < 255): every pixel exactly as mix565 blends it, the color and factor worked out once, along memory first.
    void blendRect(int x0, int y0, int x1, int y1, uint16_t c, int a255);
    // seg's steps lo..hi from its first step's minor coordinate (quotient q, remainder rem), in I-bit arithmetic: 32
    // bits for any line that fits a screen, 64 for the rest (gfx2d.cpp).
    template <typename I>
    void segSteps(int lo, int hi, int b, bool steep, int sx, int sy, int x0, int y0, I q, I rem, I major2, I minor2,
                  uint16_t c, int a255);
    // A faded seg's blocks, each pixel visited once and blended as many times as blocks cover it (gfx2d.cpp).
    template <typename I>
    void fadedSeg(int lo, int hi, int b, bool steep, int sm, int sn, int m0, int n0, I q, I rem, I major2, I minor2,
                  uint16_t c, int a255);
    bool outside(int x0, int y0, int x1, int y1) const { return x1 <= cx0_ || x0 >= cx1_ || y1 <= cy0_ || y0 >= cy1_; }

    uint16_t* px_ = nullptr;
    int org_ = 0, sx_ = 1, sy_ = 0;   // pixel (x, y) is px_[org_ + x * sx_ + y * sy_]
    bool swap_ = false;
    int bx0_ = 0, by0_ = 0, bx1_ = 0, by1_ = 0;   // what the buffer covers, device coordinates, [x0, x1)
    int cx0_ = 0, cy0_ = 0, cx1_ = 0, cy1_ = 0;   // clip, within that
    Recording* rec_ = nullptr;   // set: every drawing call is recorded there, not drawn (Recording::begin)
};

// A frame's drawing, recorded once and played back band by band. The dock draws its screen in bands of rows; recorded,
// the screens' code runs once a frame instead of once a band, and each band plays only the calls that reach its rows.
// Each band also gets a hash of those calls, in order, with everything that decides their pixels (arguments, text,
// sprite and pixel-art data, and the canvas's translation, clip and alpha): a band whose hash is the one a frame
// buffer already shows needn't be drawn or copied again. A call's rows are worked out from the same arithmetic the
// call draws with, so a band plays every call that can touch it.
//
// Every call but the blits can be recorded (a blit's source may change before playback). A frame that blits, starts
// with anything but clear() (the band would show what the last band drawn left), or doesn't fit, ends false: draw it
// band by band directly instead. Big (kOps calls): allocate one and keep it (the dock's lives in PSRAM).
class Recording {
public:
    static constexpr int kOps = 4096, kArena = 32 * 1024, kRefs = 16384, kBands = 32;
    // Starts recording a frame of a w × h screen (whole-screen coordinates), cut into bands of bandRows rows: draw the
    // frame into the canvas this returns, then call end().
    Canvas begin(int w, int h, int bandRows);
    // Finishes the frame; false when it can't be played back (see above).
    bool end();
    int bands() const { return nBands_; }
    // The hash of the calls that reach band b: equal hashes, equal pixels.
    uint64_t hash(int b) const { return bandHash_[b]; }
    // Plays band b's calls into cv, a canvas over that band's rows (Canvas::strided, as the frame is drawn).
    void play(Canvas& cv, int b) const;

private:
    friend class Canvas;
    union Arg { float f; int32_t i; uint32_t u; };
    struct Op {
        uint8_t kind, b0, b1, flag;           // the call; the first and last band it reaches; a bool argument
        uint8_t small[4];                     // small arguments (alignment, baseline)
        int32_t ox, oy, cx0, cy0, cx1, cy1;   // the canvas's translation and clip at the call
        float alpha;                          // and its alpha
        const Font* font;
        uint32_t data, len;                   // the call's bytes in the arena: text, steps, pixel art, a sprite
        Arg a[12];
    };
    static constexpr int kInkRows = 64;
    // A call's slot, zeroed, with the canvas's state (null when the recording is full or has failed).
    Op* add(const Canvas& cv, uint8_t kind);
    // n bytes of the arena for op's data, zeroed (null when it's full).
    uint8_t* reserve(Op* op, size_t n);
    // Keeps op when it can touch any of rows [y0, y1) (within the clip unless clipped is false): hashes it into each
    // band it reaches. A call that touches no row changes no pixel and is dropped.
    void keep(Op* op, int y0, int y1, const Canvas& cv, bool clipped = true);
    void fail() { ok_ = false; }

    Op ops_[kOps];
    alignas(4) uint8_t arena_[kArena];
    uint16_t refs_[kRefs];                 // band b plays ops_[refs_[k]] for k in [first_[b], first_[b + 1])
    uint16_t first_[kBands + 1];
    uint16_t count_[kBands];
    uint64_t bandHash_[kBands];
    int nOps_ = 0, arenaUsed_ = 0, refsUsed_ = 0, nBands_ = 0, rows_ = 1;
    bool ok_ = false;
};

// Optional timing of the drawing primitives (the device's "p" command): point clock at a cycle counter and each
// top-level call adds its cycles to its primitive's slot. Null (the default) costs a branch per call.
enum class Prim : uint8_t { Clear, Rect, Shape, Plate, Text, Digits, Ink, Sprite, Circle, Line, Arc, Smooth, Nearest, Seg, Count };
extern uint32_t (*gfxClock)();
extern uint32_t gfxCycles[static_cast<int>(Prim::Count)];
extern const char* const kPrimNames[static_cast<int>(Prim::Count)];

uint32_t mixToWhite(uint32_t rgb, float k);
uint32_t scaleRgb(uint32_t rgb, float k);

}  // namespace ui
