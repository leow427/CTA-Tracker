#include "gfx2d.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#include "fonts.h"

namespace ui {

namespace {

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
inline int maxi(int a, int b) { return a > b ? a : b; }
inline int mini(int a, int b) { return a < b ? a : b; }
inline uint16_t to565(uint32_t rgb) {
    return static_cast<uint16_t>((((rgb >> 16) & 0xF8) << 8) | (((rgb >> 8) & 0xFC) << 3) | ((rgb & 0xFF) >> 3));
}
inline uint32_t lerpRgbI(uint32_t a, uint32_t b, int t256) {   // t256 in 0..256
    auto ch = [&](int s) {
        const int va = (a >> s) & 255, vb = (b >> s) & 255;
        return static_cast<uint32_t>(va + (((vb - va) * t256 + 128) >> 8)) << s;
    };
    return ch(16) | ch(8) | ch(0);
}
inline uint32_t lerpRgb(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int s) {
        const float va = (a >> s) & 255, vb = (b >> s) & 255;
        return static_cast<uint32_t>(roundi(va + (vb - va) * t)) << s;
    };
    return ch(16) | ch(8) | ch(0);
}
// RGB565 blending with one multiply: a color spread out as 00000ggg ggg00000 rrrrr000 000bbbbb leaves room
// above each channel for a 5-bit factor, so y + (x - y) * a / 32 works on all three at once (exact per channel,
// wraparound included).
constexpr uint32_t kSpread = 0x07E0F81F;
inline uint32_t spread(uint32_t c) { return (c | c << 16) & kSpread; }
inline uint16_t unspread(uint32_t x) { return static_cast<uint16_t>(x | x >> 16); }
inline uint32_t lerpS(uint32_t y, uint32_t x, uint32_t a32) { return ((((x - y) * a32) >> 5) + y) & kSpread; }
inline uint16_t swap16(uint32_t c) { return static_cast<uint16_t>((c << 8) | ((c >> 8) & 0xFF)); }
// The widest block a faded seg counts its overlaps for (Canvas::fadedSeg); wider ones are blended block by block.
constexpr int kFadeBlockMax = 16;
// A seg whose doubled major extent is at most this works out its steps in 32 bits (Canvas::seg).
constexpr int64_t kSeg32Max = 500000;
// Native RGB565 d blended toward rgb by a/255.
inline uint16_t mix565(uint16_t d, uint32_t rgb, int a) {
    if (a >= 255) return to565(rgb);
    return unspread(lerpS(spread(d), spread(to565(rgb)), static_cast<uint32_t>(a * 32 + 128) >> 8));
}

// The blits' inner loops, one per byte order and opacity so the per-pixel work has no branches but the key.
template <bool kSwap, bool kOpaque, bool kOverBlack>
void nearestRun(uint16_t* p, int step, const uint16_t* srow, int u, int inv, int n, uint16_t key, uint32_t a32) {
    for (; n > 0; --n, u += inv, p += step) {
        const uint32_t c = srow[u >> 16];
        if (c == key) continue;
        if (kOpaque) { *p = kSwap ? swap16(c) : static_cast<uint16_t>(c); continue; }
        const uint32_t d = kOverBlack ? 0 : kSwap ? swap16(*p) : *p;   // lerpS from 0 is just a scale
        const uint16_t o = unspread(lerpS(spread(d), spread(c), a32));
        *p = kSwap ? swap16(o) : o;
    }
}

// The 5×7 pixel font (CTAKit PixelFont).
struct PG { char ch; const char* rows[7]; };
const PG kPF[] = {
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."}},
    {':', {".", ".", "#", ".", "#", ".", "."}},
    {'-', {"...", "...", "...", "###", "...", "...", "..."}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'u', {".....", ".....", "#...#", "#...#", "#...#", "#..##", ".##.#"}},
    {'e', {".....", ".....", ".###.", "#...#", "#####", "#....", ".###."}},
    {' ', {"...", "...", "...", "...", "...", "...", "..."}},
};
const PG& pf(char ch) {
    for (const PG& g : kPF)
        if (g.ch == ch) return g;
    return kPF[sizeof(kPF) / sizeof(kPF[0]) - 1];
}

uint32_t nextCp(const char*& s) {
    const uint8_t c = static_cast<uint8_t>(*s);
    if (c < 0x80) { ++s; return c; }
    uint32_t cp; int n;
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 2; }
    else { cp = c & 0x07; n = 3; }
    ++s;
    for (int i = 0; i < n && (*s & 0xC0) == 0x80; ++i) cp = (cp << 6) | (*s++ & 0x3F);
    return cp;
}

const Glyph* glyphFor(const Font* f, uint32_t cp) {
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (f->glyphs[mid].cp == cp) return &f->glyphs[mid];
        if (f->glyphs[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return nullptr;
}

int rowInset(int j, int h, int block, const uint8_t* steps, int nSteps, bool bottom) {
    int in = 0;
    for (int n = 0; n < nSteps; ++n)
        if (j < (n + 1) * block) { in = steps[n] * block; break; }
    if (bottom) {
        const int jj = h - 1 - j;
        for (int n = 0; n < nSteps; ++n)
            if (jj < (n + 1) * block) { in = in > steps[n] * block ? in : steps[n] * block; break; }
    }
    return in;
}

// A text's baseline as a device row, as text() places it.
int baseRow(const Font* f, float y, Base base, int oy) {
    const float by = base == Base::Top ? y + f->top : base == Base::Middle ? y + f->middle : y;
    return roundi(by) + oy;
}

// A line of text's width (measure()'s sum) and the rows [lo, hi) its glyphs cover with the baseline on row byi
// (lo >= hi: none).
float textRows(const Font* f, const char* t, int byi, int& lo, int& hi) {
    float w = 0;
    lo = INT_MAX;
    hi = INT_MIN;
    while (*t) {
        const Glyph* g = glyphFor(f, nextCp(t));
        if (!g) continue;
        w += g->adv16 / 16.f;
        if (g->h) {
            lo = mini(lo, byi + g->y);
            hi = maxi(hi, byi + g->y + g->h);
        }
    }
    return w;
}

// The recorded calls (Recording::Op::kind).
enum : uint8_t { kClear = 1, kRect, kBlend, kShape, kPlate, kChip, kText, kOutlined, kDigits, kInk, kSprite, kCircle,
                 kLine, kArc, kArrow, kSeg, kDot, kCells };

// FNV-1a over 32-bit words, 64 bits wide: a change to any one word always changes the hash (xor and an odd multiply
// are both one-to-one), and a recording's bands only need equal or not.
constexpr uint64_t kFnvBasis = 0xcbf29ce484222325ull, kFnvPrime = 0x100000001b3ull;
inline uint64_t fnvWords(uint64_t h, const uint8_t* p, size_t words) {
    for (size_t k = 0; k < words; ++k) {
        uint32_t w;
        memcpy(&w, p + 4 * k, 4);
        h = (h ^ w) * kFnvPrime;
    }
    return h;
}

int gTimingDepth = 0;
// Adds a top-level primitive call's cycles to its slot (nested calls are part of their caller).
struct Timed {
    Prim p;
    uint32_t t0 = 0;
    explicit Timed(Prim prim) : p(prim) { if (gfxClock && gTimingDepth++ == 0) t0 = gfxClock(); }
    ~Timed() { if (gfxClock && --gTimingDepth == 0) gfxCycles[static_cast<int>(p)] += gfxClock() - t0; }
};

}  // namespace

uint32_t (*gfxClock)() = nullptr;
uint32_t gfxCycles[static_cast<int>(Prim::Count)] = {};
const char* const kPrimNames[static_cast<int>(Prim::Count)] = {"clear", "rect", "shape", "plate", "text", "digits", "ink",
                                                               "sprite", "circle", "line", "arc", "smooth", "nearest", "seg"};

PlateStyle smallPlate() {
    PlateStyle s;
    s.block = 3; s.steps[0] = 2; s.steps[1] = 1; s.nSteps = 2;
    return s;
}

uint32_t mixToWhite(uint32_t rgb, float k) { return lerpRgb(rgb, 0xFFFFFF, k); }
uint32_t scaleRgb(uint32_t rgb, float k) { return lerpRgb(0, rgb, k); }

// ---------- addressing ----------

Canvas::Canvas(uint16_t* px, int w, int h) : px_(px), org_(0), sx_(1), sy_(w), swap_(false), bx1_(w), by1_(h) {
    resetClip();
}

Canvas Canvas::panelBand(uint16_t* px, int screenW, int y0, int rows) {
    Canvas c;
    c.px_ = px;
    c.org_ = y0 + rows - 1;
    c.sx_ = rows;
    c.sy_ = -1;
    c.swap_ = true;
    c.bx0_ = 0; c.by0_ = y0; c.bx1_ = screenW; c.by1_ = y0 + rows;
    c.resetClip();
    return c;
}

Canvas Canvas::panelWindow(uint16_t* px, int screenW, int x0, int x1, int y0, int rows) {
    Canvas c = panelBand(px, screenW, y0, rows);
    c.org_ -= x0 * rows;
    c.bx0_ = x0;
    c.bx1_ = x1;
    c.resetClip();
    return c;
}

Canvas Canvas::strided(uint16_t* px, int org, int sx, int sy, int x0, int y0, int x1, int y1) {
    Canvas c;
    c.px_ = px;
    c.org_ = org;
    c.sx_ = sx;
    c.sy_ = sy;
    c.swap_ = false;
    c.bx0_ = x0; c.by0_ = y0; c.bx1_ = x1; c.by1_ = y1;
    c.resetClip();
    return c;
}

void Canvas::resetClip() { cx0_ = bx0_; cy0_ = by0_; cx1_ = bx1_; cy1_ = by1_; }

void Canvas::clip(int x, int y, int w, int h) {
    cx0_ = clampi(x + ox, bx0_, bx1_); cy0_ = clampi(y + oy, by0_, by1_);
    cx1_ = clampi(x + ox + w, bx0_, bx1_); cy1_ = clampi(y + oy + h, by0_, by1_);
}

void Canvas::clear(uint32_t rgb) {
    if (rec_) {
        if (Recording::Op* op = rec_->add(*this, kClear)) {
            op->a[0].u = rgb;
            rec_->keep(op, by0_, by1_, *this, false);   // the whole canvas, clip or not
        }
        return;
    }
    const Timed timed(Prim::Clear);
    const uint16_t c = enc(to565(rgb));
    const int n = (bx1_ - bx0_) * (by1_ - by0_);
    if (c == 0) { memset(px_, 0, static_cast<size_t>(n) * 2); return; }
    for (int i = 0; i < n; ++i) px_[i] = c;
}

void Canvas::fill565(int x0, int y0, int x1, int y1, uint16_t c) {
    x0 = maxi(x0, cx0_); y0 = maxi(y0, cy0_); x1 = mini(x1, cx1_); y1 = mini(y1, cy1_);
    if (x0 >= x1 || y0 >= y1) return;
    const uint16_t e = enc(c);
    if (y1 - y0 >= x1 - x0) {   // the longer side in the inner loop
        for (int i = x0; i < x1; ++i) {
            uint16_t* p = &at(i, y0);
            for (int j = y0; j < y1; ++j, p += sy_) *p = e;
        }
    } else {
        for (int j = y0; j < y1; ++j) {
            uint16_t* p = &at(x0, j);
            for (int i = x0; i < x1; ++i, p += sx_) *p = e;
        }
    }
}

// Two neighboring pixels written as one word (little-endian: the lower address in the low half), into a buffer of
// 16-bit pixels.
typedef uint32_t __attribute__((__may_alias__)) PixelPair;
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "PixelPair puts the first pixel in the low half");

void Canvas::fillPattern(int x0, int y0, int x1, int y1, const uint16_t* col) {
    const int xa = maxi(x0, cx0_), xb = mini(x1, cx1_), ya = maxi(y0, cy0_), yb = mini(y1, cy1_);
    if (xa >= xb || ya >= yb) return;
    const int n = yb - ya;
    if ((sy_ != 1 && sy_ != -1) || n > kRun) {   // a column isn't one run of memory: row by row
        for (int j = ya; j < yb; ++j) fill565(xa, j, xb, j + 1, col[j - y0]);
        return;
    }
    // The column as it lies in memory (lowest address first), and as words for a run starting on a word boundary
    // (pairs[0]) or half a word in (pairs[1], after one pixel on its own).
    uint16_t run[kRun];
    for (int k = 0; k < n; ++k) run[k] = enc(col[(sy_ > 0 ? ya + k : yb - 1 - k) - y0]);
    uint32_t pairs[2][kRun / 2];
    for (int s = 0; s < 2; ++s)
        for (int k = s; k + 1 < n; k += 2) pairs[s][(k - s) / 2] = run[k] | static_cast<uint32_t>(run[k + 1]) << 16;
    for (int i = xa; i < xb; ++i) {
        uint16_t* p = &at(i, sy_ > 0 ? ya : yb - 1);
        int k = 0;
        if (reinterpret_cast<uintptr_t>(p) & 2) *p++ = run[k++];
        PixelPair* q = reinterpret_cast<PixelPair*>(p);
        const uint32_t* w = pairs[k];
        for (; k + 1 < n; k += 2) *q++ = *w++;
        if (k < n) *reinterpret_cast<uint16_t*>(q) = run[k];
    }
}

void Canvas::blendRect(int x0, int y0, int x1, int y1, uint16_t c, int a255) {
    if (x0 >= x1 || y0 >= y1) return;
    const uint32_t cs = spread(c), a32 = static_cast<uint32_t>(a255 * 32 + 128) >> 8;   // as mix565 has them
    // Along a column first when its rows run along memory (a band), else along a row.
    const bool down = sy_ == 1 || sy_ == -1;
    const int nOuter = down ? x1 - x0 : y1 - y0, nInner = down ? y1 - y0 : x1 - x0;
    const int stepOuter = down ? sx_ : sy_, stepInner = down ? sy_ : sx_;
    uint16_t* line = &at(x0, y0);
    for (int o = 0; o < nOuter; ++o, line += stepOuter) {
        uint16_t* p = line;
        if (swap_)
            for (int k = 0; k < nInner; ++k, p += stepInner) *p = swap16(unspread(lerpS(spread(swap16(*p)), cs, a32)));
        else
            for (int k = 0; k < nInner; ++k, p += stepInner) *p = unspread(lerpS(spread(*p), cs, a32));
    }
}

void Canvas::blend(int x, int y, uint32_t rgb, int a) {
    if (rec_) {
        if (Recording::Op* op = rec_->add(*this, kBlend)) {
            op->a[0].i = x; op->a[1].i = y; op->a[2].u = rgb; op->a[3].i = a;
            rec_->keep(op, y, y + 1, *this);
        }
        return;
    }
    if (x < cx0_ || y < cy0_ || x >= cx1_ || y >= cy1_ || a <= 0) return;
    uint16_t& d = at(x, y);
    d = enc(mix565(dec(d), rgb, a));
}

// ---------- fills and plates ----------

void Canvas::fillRect(float x, float y, float w, float h, uint32_t rgb, float a) {
    if (rec_) {
        if (Recording::Op* op = rec_->add(*this, kRect)) {
            op->a[0].f = x; op->a[1].f = y; op->a[2].f = w; op->a[3].f = h; op->a[4].u = rgb; op->a[5].f = a;
            rec_->keep(op, roundi(y) + oy, roundi(y + h) + oy, *this);
        }
        return;
    }
    const Timed timed(Prim::Rect);
    const int x0 = roundi(x) + ox, x1 = roundi(x + w) + ox;
    const int y0 = roundi(y) + oy, y1 = roundi(y + h) + oy;
    if (outside(x0, y0, x1, y1)) return;
    const int a255 = roundi(255 * a * alpha);
    if (a255 <= 0) return;
    if (a255 >= 255) { fill565(x0, y0, x1, y1, to565(rgb)); return; }
    blendRect(maxi(x0, cx0_), maxi(y0, cy0_), mini(x1, cx1_), mini(y1, cy1_), to565(rgb), a255);
}

void Canvas::fillPixelShape(int x, int y, int w, int h, int block, const uint8_t* steps, int nSteps, uint32_t rgb,
                            float a, bool bottom) {
    if (rec_) {
        if (Recording::Op* op = rec_->add(*this, kShape)) {
            op->a[0].i = x; op->a[1].i = y; op->a[2].i = w; op->a[3].i = h; op->a[4].i = block; op->a[5].i = nSteps;
            op->a[6].u = rgb; op->a[7].f = a;
            op->flag = bottom;
            const size_t n = nSteps > 0 ? static_cast<size_t>(nSteps) : 0;
            uint8_t* d = rec_->reserve(op, n);
            if (!d) return;
            if (n) memcpy(d, steps, n);
            rec_->keep(op, y + oy, y + oy + h, *this);
        }
        return;
    }
    const Timed timed(Prim::Shape);
    const int X = x + ox, Y = y + oy;
    if (outside(X, Y, X + w, Y + h)) return;
    const int a255 = roundi(255 * a * alpha);
    if (a255 <= 0) return;
    const uint16_t c = to565(rgb);
    const int j0 = maxi(0, cy0_ - Y), j1 = mini(h, cy1_ - Y);
    for (int j = j0; j < j1; ++j) {
        const int in = rowInset(j, h, block, steps, nSteps, bottom);
        if (a255 >= 255) fill565(X + in, Y + j, X + w - in, Y + j + 1, c);
        else blendRect(maxi(X + in, cx0_), Y + j, mini(X + w - in, cx1_), Y + j + 1, c, a255);
    }
}

void Canvas::plate(int x, int y, int w, int h, const PlateStyle& s) {
    if (rec_) {
        if (Recording::Op* op = rec_->add(*this, kPlate)) {
            op->a[0].i = x; op->a[1].i = y; op->a[2].i = w; op->a[3].i = h;
            op->a[4].u = s.fill; op->a[5].u = s.fill2; op->a[6].u = s.rim; op->a[7].f = s.rimAlpha;
            op->a[8].i = s.rimW; op->a[9].i = s.edge; op->a[10].i = s.block;
            op->a[11].u = s.steps[0] | s.steps[1] << 8 | s.steps[2] << 16 | static_cast<uint32_t>(s.nSteps) << 24;
            op->flag = s.highlight;
            int lo = y + oy, hi = y + oy + h;
            if (s.edge) {   // the shadow under it
                lo = mini(lo, y + s.edge + oy);
                hi = maxi(hi, y + s.edge + h + oy);
            }
            rec_->keep(op, lo, hi, *this);
        }
        return;
    }
    const Timed timed(Prim::Plate);
    const int X = x + ox, Y = y + oy;
    const int a255 = roundi(255 * alpha);
    if (s.edge > 0 && a255 >= 255) {
        // The shadow, edge rows down: the plate goes over it opaque, so only what shows is drawn (its bottom rows, and
        // beside the plate's stepped bottom corners). The same pixels as the whole shape drawn first.
        const uint16_t c = to565(scaleRgb(s.fill2, 0.55f));
        const int Ys = Y + s.edge;
        for (int k = maxi(0, cy0_ - Ys), k1 = mini(h, cy1_ - Ys); k < k1; ++k) {
            const int sin = rowInset(k, h, s.block, s.steps, s.nSteps, true), j = k + s.edge;
            if (j >= h) {
                fill565(X + sin, Ys + k, X + w - sin, Ys + k + 1, c);
                continue;
            }
            const int in = rowInset(j, h, s.block, s.steps, s.nSteps, true);   // the plate's row over it
            if (in <= sin) continue;
            fill565(X + sin, Ys + k, X + mini(in, w - sin), Ys + k + 1, c);
            fill565(X + maxi(w - in, sin), Ys + k, X + w - sin, Ys + k + 1, c);
        }
    } else if (s.edge) {
        fillPixelShape(x, y + s.edge, w, h, s.block, s.steps, s.nSteps, scaleRgb(s.fill2, 0.55f));
    }
    if (outside(X, Y, X + w, Y + h)) return;
    if (a255 <= 0) return;
    const int r = s.rimW;
    const int j0 = maxi(0, cy0_ - Y), j1 = mini(h, cy1_ - Y);
    const int rimT = static_cast<int>(s.rimAlpha * 256 + 0.5f);
    // Opaque on a band (rows run along memory, the device's case): each row's inset, span and colors first, then the
    // columns every row fills (its fill, or its rim on the rim-only rows at the top and bottom) a column at a time,
    // then each row's two ends. The same pixels as row by row.
    const bool columns = a255 >= 255 && (sy_ == 1 || sy_ == -1) && j1 > j0 && j1 - j0 <= kRun;
    int inA[kRun], leA[kRun], reA[kRun];
    uint16_t cbA[kRun], crA[kRun], mid[kRun];
    int L = 0, R = w;   // the columns [L, R) every row fills in one color
    for (int j = j0; j < j1; ++j) {
        const int in = rowInset(j, h, s.block, s.steps, s.nSteps, true);
        uint32_t base = s.fill == s.fill2 ? s.fill : lerpRgbI(s.fill, s.fill2, ((2 * j + 1) * 256 + h) / (2 * h));
        if (s.highlight && j < h / 2)
            base = lerpRgbI(base, 0xFFFFFF, static_cast<int>(0.12f * 256 * (1.f - (j + 0.5f) / (h / 2.f)) + 0.5f));
        const uint32_t rimmed = lerpRgbI(base, s.rim, rimT);
        int le = w, re = 0;   // the eroded span [le, re); empty near the top and bottom
        if (r == 0) { le = in; re = w - in; }
        else if (j >= r && j < h - r) {
            int m = 0;
            for (int d = -(r - 1); d <= r - 1; ++d) {
                const int k = rowInset(clampi(j + d, 0, h - 1), h, s.block, s.steps, s.nSteps, true);
                m = k > m ? k : m;
            }
            le = m + r; re = w - m - r;
        }
        const int YY = Y + j;
        if (columns) {
            const int k = j - j0;
            const bool rimOnly = le >= re;
            inA[k] = in; leA[k] = le; reA[k] = re;
            cbA[k] = to565(base); crA[k] = to565(rimmed);
            mid[k] = rimOnly ? crA[k] : cbA[k];
            L = maxi(L, rimOnly ? in : le);
            R = mini(R, rimOnly ? w - in : re);
        } else if (a255 >= 255) {
            const uint16_t cb = to565(base), cr = to565(rimmed);
            if (le >= re) fill565(X + in, YY, X + w - in, YY + 1, cr);
            else {
                fill565(X + in, YY, X + le, YY + 1, cr);
                fill565(X + le, YY, X + re, YY + 1, cb);
                fill565(X + re, YY, X + w - in, YY + 1, cr);
            }
        } else
            for (int i = maxi(in, cx0_ - X); i < mini(w - in, cx1_ - X); ++i) {
                uint16_t& d = at(X + i, YY);
                d = enc(mix565(dec(d), (i >= le && i < re) ? base : rimmed, a255));
            }
    }
    if (!columns) return;
    if (L < R) fillPattern(X + L, Y + j0, X + R, Y + j1, mid);
    for (int j = j0; j < j1; ++j) {
        const int k = j - j0, in = inA[k], le = leA[k], re = reA[k], YY = Y + j;
        const uint16_t cb = cbA[k], cr = crA[k];
        if (L >= R) {   // no column every row fills: the row whole, as above
            if (le >= re) fill565(X + in, YY, X + w - in, YY + 1, cr);
            else {
                fill565(X + in, YY, X + le, YY + 1, cr);
                fill565(X + le, YY, X + re, YY + 1, cb);
                fill565(X + re, YY, X + w - in, YY + 1, cr);
            }
        } else if (le >= re) {   // rim only: its ends beside the middle
            fill565(X + in, YY, X + L, YY + 1, cr);
            fill565(X + R, YY, X + w - in, YY + 1, cr);
        } else {                 // rim, fill up to the middle; fill, rim after it
            fill565(X + in, YY, X + le, YY + 1, cr);
            fill565(X + le, YY, X + L, YY + 1, cb);
            fill565(X + R, YY, X + re, YY + 1, cb);
            fill565(X + re, YY, X + w - in, YY + 1, cr);
        }
    }
}

int Canvas::chip(const char* t, int x, int y, uint32_t bg, uint32_t fg) {
    const Font* f = font(900, 11);
    const int w = static_cast<int>(ceilf(measure(f, t))) + 12;
    if (rec_) {
        if (Recording::Op* op = rec_->add(*this, kChip)) {
            op->a[0].i = x; op->a[1].i = y; op->a[2].u = bg; op->a[3].u = fg;
            const size_t n = strlen(t) + 1;
            uint8_t* d = rec_->reserve(op, n);
            if (!d) return w;
            memcpy(d, t, n);
            int lo, hi;   // the label, as below
            textRows(f, t, baseRow(f, y + 9.5f, Base::Middle, oy), lo, hi);
            rec_->keep(op, mini(lo, y + oy), maxi(hi, y + oy + 18), *this);
        }
        return w;
    }
    if (outside(x + ox, y + oy, x + ox + w, y + oy + 18)) return w;
    const uint8_t steps[1] = {1};
    fillPixelShape(x, y, w, 18, 2, steps, 1, bg);
    text(f, t, x + 6, y + 9.5f, fg, Align::Left, Base::Middle);
    return w;
}

// ---------- text ----------

const Font* Canvas::font(int weight, int size) {
    const Font* best = nullptr;
    int bestD = 1 << 30;
    for (int i = 0; i < kFontCount; ++i) {
        const Font& f = kFonts[i];
        const int d = (f.weight == weight ? 0 : 1000) + (f.size > size ? f.size - size : size - f.size);
        if (d < bestD) { bestD = d; best = &f; }
    }
    return best;
}

bool Canvas::covers(const Font* f, const char* t) {
    while (*t)
        if (!glyphFor(f, nextCp(t))) return false;
    return true;
}

float Canvas::measure(const Font* f, const char* t) {
    float w = 0;
    while (*t) {
        const Glyph* g = glyphFor(f, nextCp(t));
        if (g) w += g->adv16 / 16.f;
    }
    return w;
}

const char* Canvas::fit(const Font* f, const char* t, float maxW, char* out, size_t cap) {
    size_t n = strlen(t);
    if (n >= cap) n = cap - 1;
    memcpy(out, t, n); out[n] = 0;
    if (measure(f, out) <= maxW) return out;
    const float ell = measure(f, "\xE2\x80\xA6");
    while (n > 1) {
        do { --n; } while (n > 0 && (static_cast<uint8_t>(out[n]) & 0xC0) == 0x80);
        out[n] = 0;
        if (measure(f, out) + ell <= maxW) break;
    }
    if (n + 4 <= cap) memcpy(out + n, "\xE2\x80\xA6", 4);
    return out;
}

float Canvas::text(const Font* f, const char* t, float x, float y, uint32_t rgb, Align al, Base base, float a) {
    if (rec_) {
        int lo, hi;
        const float w = textRows(f, t, baseRow(f, y, base, oy), lo, hi);
        if (Recording::Op* op = rec_->add(*this, kText)) {
            op->font = f;
            op->a[0].f = x; op->a[1].f = y; op->a[2].u = rgb; op->a[3].f = a;
            op->small[0] = static_cast<uint8_t>(al); op->small[1] = static_cast<uint8_t>(base);
            const size_t n = strlen(t) + 1;
            uint8_t* d = rec_->reserve(op, n);
            if (!d) return w;
            memcpy(d, t, n);
            rec_->keep(op, lo, hi, *this);
        }
        return w;
    }
    const Timed timed(Prim::Text);
    const float w = measure(f, t);
    float pen = al == Align::Left ? x : al == Align::Center ? x - w / 2 : x - w;
    const int byi = baseRow(f, y, base, oy);
    // The line's rows: glyphs reach at most a size above and below the baseline.
    if (byi + f->size < cy0_ || byi - 2 * f->size > cy1_) return w;
    const float k = a * alpha;
    if (k <= 0) return w;
    const uint32_t k256 = k >= 1 ? 256 : static_cast<uint32_t>(k * 256 + 0.5f);
    const uint16_t c565 = to565(rgb), ce = enc(c565);
    const uint32_t cs = spread(c565);
    while (*t) {
        const Glyph* g = glyphFor(f, nextCp(t));
        if (!g) continue;
        const int gx = roundi(pen) + g->x + ox, gy = byi + g->y;
        pen += g->adv16 / 16.f;
        if (outside(gx, gy, gx + g->w, gy + g->h)) continue;
        const uint8_t* bits = f->bits + g->off;
        const int i0 = maxi(0, cx0_ - gx), i1 = mini(g->w, cx1_ - gx);
        const int j0 = maxi(0, cy0_ - gy), j1 = mini(g->h, cy1_ - gy);
        for (int j = j0; j < j1; ++j) {
            const uint8_t* row = bits + j * g->w;
            uint16_t* p = &at(gx + i0, gy + j);
            for (int i = i0; i < i1; ++i, p += sx_) {
                const uint32_t c = row[i];
                if (!c) continue;
                const uint32_t a32 = (c * k256 * 32 + 32768) >> 16;
                if (a32 >= 32) *p = ce;
                else if (a32) *p = enc(unspread(lerpS(spread(dec(*p)), cs, a32)));
            }
        }
    }
    return w;
}

// textOutlined's row ring: up to 5 rows (r <= 2) of a glyph's horizontal spread.
static constexpr int kOutlineW = 256;
static uint8_t gOutlineRows[5][kOutlineW];   // drawing runs on one task

float Canvas::textOutlined(const Font* f, const char* t, float x, float y, uint32_t rgb, uint32_t outline, int r,
                           Align al, Base base) {
    if (rec_) {
        int lo, hi;
        const float w = textRows(f, t, baseRow(f, y, base, oy), lo, hi);
        if (Recording::Op* op = rec_->add(*this, kOutlined)) {
            op->font = f;
            op->a[0].f = x; op->a[1].f = y; op->a[2].u = rgb; op->a[3].u = outline; op->a[4].i = r;
            op->small[0] = static_cast<uint8_t>(al); op->small[1] = static_cast<uint8_t>(base);
            const size_t n = strlen(t) + 1;
            uint8_t* d = rec_->reserve(op, n);
            if (!d) return w;
            memcpy(d, t, n);
            const int rr = maxi(0, mini(r, 2));   // the outline's spread, as below
            rec_->keep(op, lo - rr, hi + rr, *this);
        }
        return w;
    }
    const Timed timed(Prim::Text);
    const float w = measure(f, t);
    const float k = alpha;
    r = mini(r, 2);
    if (r > 0 && k > 0) {
        float pen = al == Align::Left ? x : al == Align::Center ? x - w / 2 : x - w;
        const int byi = baseRow(f, y, base, oy);
        const uint32_t k256 = k >= 1 ? 256 : static_cast<uint32_t>(k * 256 + 0.5f);
        const uint16_t c565 = to565(outline), ce = enc(c565);
        const uint32_t cs = spread(c565);
        // Faded, the outline leaves out the text's own pixels, so the text over it shows the card, not the outline,
        // as the app fades the finished card. Opaque, the text covers them anyway, so they're skipped.
        const bool faded = k < 1;
        for (const char* s = t; *s;) {
            const Glyph* g = glyphFor(f, nextCp(s));
            if (!g) continue;
            const int gx = roundi(pen) + g->x + ox, gy = byi + g->y;
            pen += g->adv16 / 16.f;
            if (outside(gx - r, gy - r, gx + g->w + r, gy + g->h + r)) continue;
            const uint8_t* bits = f->bits + g->off;
            const int gw = g->w, gh = g->h, n = gw + 2 * r;
            const int i0 = maxi(-r, cx0_ - gx), i1 = mini(gw + r, cx1_ - gx);
            const int j0 = maxi(-r, cy0_ - gy), j1 = mini(gh + r, cy1_ - gy);
            if (n > kOutlineW || i0 >= i1 || j0 >= j1) continue;
            // The spread is separable: each glyph row's max over [i - r, i, i + r] (kept for rows j - r ... j + r in a
            // ring), then the max of three of those rows.
            const int R = 2 * r + 1;
            auto hrow = [&](int sj) {
                uint8_t* out = gOutlineRows[(sj % R + R) % R];
                if (sj < 0 || sj >= gh) { memset(out, 0, n); return; }
                const uint8_t* row = bits + sj * gw;
                for (int q = 0; q < n; ++q) {
                    const int i = q - r;
                    uint8_t m = i >= 0 && i < gw ? row[i] : 0;
                    if (i - r >= 0 && i - r < gw && row[i - r] > m) m = row[i - r];
                    if (i + r >= 0 && i + r < gw && row[i + r] > m) m = row[i + r];
                    out[q] = m;
                }
            };
            for (int sj = j0 - r; sj < j0 + r; ++sj) hrow(sj);
            for (int j = j0; j < j1; ++j) {
                hrow(j + r);
                const uint8_t* up = gOutlineRows[((j - r) % R + R) % R];
                const uint8_t* mid = gOutlineRows[(j % R + R) % R];
                const uint8_t* dn = gOutlineRows[((j + r) % R + R) % R];
                const uint8_t* own = j >= 0 && j < gh ? bits + j * gw : nullptr;
                uint16_t* p = &at(gx + i0, gy + j);
                for (int i = i0; i < i1; ++i, p += sx_) {
                    const uint32_t o = own && i >= 0 && i < gw ? own[i] : 0;
                    if (o == 255 && !faded) continue;
                    uint32_t c = mid[i + r];
                    if (up[i + r] > c) c = up[i + r];
                    if (dn[i + r] > c) c = dn[i + r];
                    if (faded) c = c > o ? c - o : 0;
                    if (!c) continue;
                    const uint32_t a32 = (c * k256 * 32 + 32768) >> 16;
                    if (a32 >= 32) *p = ce;
                    else if (a32) *p = enc(unspread(lerpS(spread(dec(*p)), cs, a32)));
                }
            }
        }
    }
    return text(f, t, x, y, rgb, al, base);
}

int Canvas::pixelWidth(const char* t, int s) {
    int w = 0;
    for (int i = 0; t[i]; ++i) w += static_cast<int>(strlen(pf(t[i]).rows[0])) + (i ? 1 : 0);
    return w * s;
}

int Canvas::pixelText(const char* t, float x, int y, int s, uint32_t rgb, Align al, int grow) {
    if (rec_) {
        const int w = pixelWidth(t, s);
        if (Recording::Op* op = rec_->add(*this, kDigits)) {
            op->a[0].f = x; op->a[1].i = y; op->a[2].i = s; op->a[3].u = rgb; op->a[4].i = grow;
            op->small[0] = static_cast<uint8_t>(al);
            const size_t n = strlen(t) + 1;
            uint8_t* d = rec_->reserve(op, n);
            if (!d) return w;
            memcpy(d, t, n);
            rec_->keep(op, y + oy - grow, y + oy + 7 * s + grow, *this);
        }
        return w;
    }
    const Timed timed(Prim::Digits);
    const int w = pixelWidth(t, s);
    int cx = roundi(al == Align::Center ? x - w / 2.f : al == Align::Right ? x - w : x);
    if (outside(cx + ox - grow, y + oy - grow, cx + ox + w + grow, y + oy + 7 * s + grow)) return w;
    for (int k = 0; t[k]; ++k) {
        const PG& g = pf(t[k]);
        const int gw = static_cast<int>(strlen(g.rows[0]));
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < gw; ++c)
                if (g.rows[r][c] == '#') fillRect(cx + c * s - grow, y + r * s - grow, s + 2 * grow, s + 2 * grow, rgb);
        cx += (gw + 1) * s;
    }
    return w;
}

// ---------- pixel art ----------

void Canvas::ink(const char* const* rows, int nRows, int x, int y, int s, uint32_t rgb, float a) {
    if (rec_) {
        if (nRows <= 0) return;   // nothing to draw
        if (nRows > Recording::kInkRows) { rec_->fail(); return; }
        if (Recording::Op* op = rec_->add(*this, kInk)) {
            op->a[0].i = x; op->a[1].i = y; op->a[2].i = s; op->a[3].u = rgb; op->a[4].f = a; op->a[5].i = nRows;
            size_t n = 0;
            for (int j = 0; j < nRows; ++j) n += strlen(rows[j]) + 1;
            uint8_t* d = rec_->reserve(op, n);
            if (!d) return;
            for (int j = 0; j < nRows; ++j) {   // the rows, one after another
                const size_t k = strlen(rows[j]) + 1;
                memcpy(d, rows[j], k);
                d += k;
            }
            rec_->keep(op, y + oy, y + oy + nRows * s, *this);
        }
        return;
    }
    const Timed timed(Prim::Ink);
    if (y + oy + nRows * s <= cy0_ || y + oy >= cy1_) return;
    for (int j = 0; j < nRows; ++j)
        for (int i = 0; rows[j][i]; ++i)
            if (rows[j][i] != '.') fillRect(x + i * s, y + j * s, s, s, rgb, a);
}

void Canvas::sprite(const uint8_t* idx, int w, int h, int dx, int dy, int x, int y, const uint16_t* pal565, int s) {
    if (rec_) {
        if (idx == nullptr || pal565 == nullptr || s <= 0 || w <= 0 || h <= 0) return;   // draws nothing
        if (Recording::Op* op = rec_->add(*this, kSprite)) {
            const size_t n = static_cast<size_t>(w) * h, palAt = (n + 1) & ~static_cast<size_t>(1);
            int top = 0;   // the highest palette index it uses
            for (size_t k = 0; k < n; ++k) top = maxi(top, idx[k]);
            uint8_t* d = rec_->reserve(op, palAt + (top + 1) * sizeof(uint16_t));
            if (!d) return;
            memcpy(d, idx, n);
            memcpy(d + palAt, pal565, (top + 1) * sizeof(uint16_t));
            op->a[0].i = w; op->a[1].i = h; op->a[2].i = dx; op->a[3].i = dy; op->a[4].i = x; op->a[5].i = y;
            op->a[6].i = s; op->a[7].i = top;
            const int Y = y + oy + dy * s;
            rec_->keep(op, Y, Y + h * s, *this);
        }
        return;
    }
    const Timed timed(Prim::Sprite);
    if (idx == nullptr || pal565 == nullptr || s <= 0) return;
    const int X = x + ox + dx * s, Y = y + oy + dy * s;
    if (outside(X, Y, X + w * s, Y + h * s)) return;
    for (int j = 0; j < h; ++j) {
        const int py = Y + j * s;
        if (py + s <= cy0_ || py >= cy1_) continue;
        for (int i = 0; i < w; ++i) {
            const uint8_t index = idx[j * w + i];
            if (index) fill565(X + i * s, py, X + i * s + s, py + s, pal565[index]);
        }
    }
}

// ---------- anti-aliased shapes ----------

void Canvas::fillCircle(float cx, float cy, float r, uint32_t rgb, float a) {
    if (rec_) {
        if (Recording::Op* op = rec_->add(*this, kCircle)) {
            op->a[0].f = cx; op->a[1].f = cy; op->a[2].f = r; op->a[3].u = rgb; op->a[4].f = a;
            const float y = cy + oy;
            rec_->keep(op, static_cast<int>(floorf(y - r - 1)), static_cast<int>(ceilf(y + r + 1)) + 1, *this);
        }
        return;
    }
    const Timed timed(Prim::Circle);
    cx += ox; cy += oy;
    const int j0 = maxi(static_cast<int>(floorf(cy - r - 1)), cy0_), j1 = mini(static_cast<int>(ceilf(cy + r + 1)) + 1, cy1_);
    if (j0 >= j1 || cx + r + 1 < cx0_ || cx - r - 1 > cx1_) return;
    const float k = 255 * a * alpha, ro = r + 0.5f;
    for (int j = j0; j < j1; ++j) {
        const float dy = j + 0.5f - cy;
        if (fabsf(dy) >= ro) continue;
        const float xo = sqrtf(ro * ro - dy * dy);   // the row's span; outside it coverage is zero
        const int i0 = maxi(static_cast<int>(floorf(cx - xo)), cx0_), i1 = mini(static_cast<int>(ceilf(cx + xo)) + 1, cx1_);
        for (int i = i0; i < i1; ++i) {
            const float dx = i + 0.5f - cx;
            const float cov = clampf(r - sqrtf(dx * dx + dy * dy) + 0.5f, 0, 1);
            if (cov <= 0) continue;
            uint16_t& p = at(i, j);
            p = enc(mix565(dec(p), rgb, static_cast<int>(cov * k + 0.5f)));
        }
    }
}

void Canvas::line(float x0, float y0, float x1, float y1, float width, uint32_t rgb, float a) {
    if (rec_) {
        if (Recording::Op* op = rec_->add(*this, kLine)) {
            op->a[0].f = x0; op->a[1].f = y0; op->a[2].f = x1; op->a[3].f = y1; op->a[4].f = width; op->a[5].u = rgb;
            op->a[6].f = a;
            const float ya = y0 + oy, yb = y1 + oy, hw = width / 2;
            rec_->keep(op, static_cast<int>(floorf(fminf(ya, yb) - hw - 1)),
                       static_cast<int>(ceilf(fmaxf(ya, yb) + hw + 1)) + 1, *this);
        }
        return;
    }
    const Timed timed(Prim::Line);
    x0 += ox; x1 += ox; y0 += oy; y1 += oy;
    const float hw = width / 2, k = 255 * a * alpha;
    const float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy, inv = len2 > 0 ? 1.f / len2 : 0;
    const int i0 = maxi(static_cast<int>(floorf(fminf(x0, x1) - hw - 1)), cx0_);
    const int i1 = mini(static_cast<int>(ceilf(fmaxf(x0, x1) + hw + 1)) + 1, cx1_);
    const int j0 = maxi(static_cast<int>(floorf(fminf(y0, y1) - hw - 1)), cy0_);
    const int j1 = mini(static_cast<int>(ceilf(fmaxf(y0, y1) + hw + 1)) + 1, cy1_);
    for (int j = j0; j < j1; ++j)
        for (int i = i0; i < i1; ++i) {
            const float px = i + 0.5f, py = j + 0.5f;
            const float t = clampf(((px - x0) * dx + (py - y0) * dy) * inv, 0, 1);
            const float ex = px - (x0 + t * dx), ey = py - (y0 + t * dy), d = sqrtf(ex * ex + ey * ey);
            const float cov = clampf(hw - d + 0.5f, 0, 1);
            if (cov <= 0) continue;
            uint16_t& p = at(i, j);
            p = enc(mix565(dec(p), rgb, static_cast<int>(cov * k + 0.5f)));
        }
}

void Canvas::arc(float cx, float cy, float r, float a0, float a1, float lw, uint32_t rgb, float a, bool roundCaps) {
    if (rec_) {
        if (Recording::Op* op = rec_->add(*this, kArc)) {
            op->a[0].f = cx; op->a[1].f = cy; op->a[2].f = r; op->a[3].f = a0; op->a[4].f = a1; op->a[5].f = lw;
            op->a[6].u = rgb; op->a[7].f = a;
            op->flag = roundCaps;
            // Its rows are within its bounding box (every point of it within r of the center) and the pad.
            const float y = cy + oy, ar = fabsf(r), pad = lw / 2 + 2;
            rec_->keep(op, static_cast<int>(floorf(y - ar - pad)) - 1, static_cast<int>(ceilf(y + ar + pad)) + 1, *this);
        }
        return;
    }
    const Timed timed(Prim::Arc);
    cx += ox; cy += oy;
    const float hw = lw / 2, k = 255 * a * alpha, span = a1 - a0, tau = 6.2831853f, pad = hw + 2;
    if (cy + r + pad < cy0_ || cy - r - pad > cy1_ || cx + r + pad < cx0_ || cx - r - pad > cx1_) return;
    const bool full = span >= tau - 1e-4f, wide = span > tau / 2;
    const float ux0 = cosf(a0), uy0 = sinf(a0), ux1 = cosf(a1), uy1 = sinf(a1);
    const float ex0 = cx + r * ux0, ey0 = cy + r * uy0, ex1 = cx + r * ux1, ey1 = cy + r * uy1;
    // The bounding box: the two ends, and each compass point the sweep passes.
    float bx0 = fminf(ex0, ex1), bx1 = fmaxf(ex0, ex1), by0 = fminf(ey0, ey1), by1 = fmaxf(ey0, ey1);
    for (int q = 0; q < 4; ++q) {
        float d = fmodf(q * (tau / 4) - a0, tau);
        if (d < 0) d += tau;
        if (!full && d > span) continue;
        const float x = cx + (q == 0 ? r : q == 2 ? -r : 0), y = cy + (q == 1 ? r : q == 3 ? -r : 0);
        bx0 = fminf(bx0, x); bx1 = fmaxf(bx1, x); by0 = fminf(by0, y); by1 = fmaxf(by1, y);
    }
    const int j0 = clampi(static_cast<int>(floorf(by0 - pad)), cy0_, cy1_), j1 = clampi(static_cast<int>(ceilf(by1 + pad)), cy0_, cy1_);
    const float ro = r + hw + 1, ri = fmaxf(0, r - hw - 1);
    // Only the ring's span on each row. A point is in the sweep when it's past the start and before the end
    // (cross products with the end directions), or either one for a sweep over half a turn.
    for (int j = j0; j < j1; ++j) {
        const float py = j + 0.5f - cy;
        if (fabsf(py) > ro) continue;
        const float xo = sqrtf(ro * ro - py * py), xi = fabsf(py) < ri ? sqrtf(ri * ri - py * py) : 0;
        for (int side = 0; side < 2; ++side) {
            float s0 = side ? cx + xi : cx - xo, s1 = side ? cx + xo : cx - xi;
            s0 = fmaxf(s0, bx0 - pad); s1 = fminf(s1, bx1 + pad);
            const int i0 = maxi(static_cast<int>(floorf(s0)), cx0_), i1 = mini(static_cast<int>(ceilf(s1)), cx1_ - 1);
            for (int i = i0; i <= i1; ++i) {
                const float px = i + 0.5f - cx, dist = sqrtf(px * px + py * py);
                if (fabsf(dist - r) > hw + 1) continue;
                const float c0 = ux0 * py - uy0 * px, c1 = px * uy1 - py * ux1;
                const bool in = full || (wide ? (c0 >= 0 || c1 >= 0) : (c0 >= 0 && c1 >= 0));
                float cov = 0;
                if (in) cov = clampf(hw - fabsf(dist - r) + 0.5f, 0, 1);
                else if (roundCaps) {
                    const float dx0 = i + 0.5f - ex0, dy0 = j + 0.5f - ey0, dx1 = i + 0.5f - ex1, dy1 = j + 0.5f - ey1;
                    cov = clampf(hw - sqrtf(fminf(dx0 * dx0 + dy0 * dy0, dx1 * dx1 + dy1 * dy1)) + 0.5f, 0, 1);
                }
                if (cov <= 0) continue;
                uint16_t& p = at(i, j);
                p = enc(mix565(dec(p), rgb, static_cast<int>(cov * k + 0.5f)));
            }
            if (xi == 0) break;   // the row crosses the ring once
        }
    }
}

void Canvas::arrowCircle(float cx, float cy, float r, uint32_t rgb, uint32_t inkRgb) {
    if (rec_) {
        if (Recording::Op* op = rec_->add(*this, kArrow)) {
            op->a[0].f = cx; op->a[1].f = cy; op->a[2].f = r; op->a[3].u = rgb; op->a[4].u = inkRgb;
            // The circle (r + 1 either way) and the arrow inside it.
            const float y = cy + oy, ar = fabsf(r);
            rec_->keep(op, static_cast<int>(floorf(y - ar - 2)) - 1, static_cast<int>(ceilf(y + ar + 2)) + 1, *this);
        }
        return;
    }
    if (outside(static_cast<int>(cx - r - 2) + ox, static_cast<int>(cy - r - 2) + oy,
                static_cast<int>(cx + r + 3) + ox, static_cast<int>(cy + r + 3) + oy)) return;
    fillCircle(cx, cy, r, rgb);
    const float lw = r * 0.26f;
    line(cx - r * 0.45f, cy, cx + r * 0.42f, cy, lw, inkRgb);
    line(cx + r * 0.05f, cy - r * 0.4f, cx + r * 0.45f, cy, lw, inkRgb);
    line(cx + r * 0.45f, cy, cx + r * 0.05f, cy + r * 0.4f, lw, inkRgb);
}

// ---------- crisp block lines and dots (the dock's map) ----------

void Canvas::seg(float x0f, float y0f, float x1f, float y1f, int b, uint32_t rgb, float a) {
    if (rec_) {
        if (Recording::Op* op = rec_->add(*this, kSeg)) {
            op->a[0].f = x0f; op->a[1].f = y0f; op->a[2].f = x1f; op->a[3].f = y1f; op->a[4].i = b; op->a[5].u = rgb;
            op->a[6].f = a;
            const int ya = roundi(y0f), yb = roundi(y1f), h = b >> 1;
            rec_->keep(op, mini(ya, yb) - h + oy, maxi(ya, yb) - h + b + oy, *this);   // every block, as below
        }
        return;
    }
    const Timed timed(Prim::Seg);
    int x0 = roundi(x0f), y0 = roundi(y0f);
    const int x1 = roundi(x1f), y1 = roundi(y1f), h = b >> 1;
    if (outside(mini(x0, x1) - h + ox, mini(y0, y1) - h + oy, maxi(x0, x1) - h + b + ox, maxi(y0, y1) - h + b + oy)) return;
    const int a255 = roundi(255 * a * alpha);
    if (a255 <= 0 || b <= 0) return;   // nothing to draw: faded to nothing, or blocks with no pixels
    // The mockup's Bresenham (Zingl's form: e = dx - |dy|, x steps when 2e >= -|dy|, y when 2e <= dx) visits, at
    // step n of a line whose major axis is y (|dy| >= dx), y0 + sy * n and x0 + sx * floor((2n dx + |dy|) / 2|dy|);
    // along x, x0 + sx * n and y0 + sy * floor((2n |dy| + dx) / 2dx). Working the steps out directly lets a long
    // line visit only the steps whose block can touch the clip (one band of the screen, on the dock).
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, ady = y1 > y0 ? y1 - y0 : y0 - y1;
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    const bool steep = ady >= dx;
    const int steps = mini(steep ? ady : dx, 3999);   // the mockup's cap: at most 4000 blocks
    // The steps whose major coordinate puts the block within the clip, plus one either side.
    int lo = 0, hi = steps;
    {
        const int m0 = steep ? y0 : x0, sm = steep ? sy : sx;
        const int c0 = (steep ? cy0_ - oy : cx0_ - ox) - b + h, c1 = (steep ? cy1_ - oy : cx1_ - ox) + h;   // block meets the clip
        int a = (c0 - m0) * sm, z = (c1 - m0) * sm;
        if (a > z) { const int t = a; a = z; z = t; }
        lo = maxi(lo, a - 1);
        hi = mini(hi, z + 1);
    }
    if (lo > hi) return;   // no block reaches the clip
    // The minor coordinate's floor((2n minor + major) / 2 major), as a quotient q and remainder from step lo on (each
    // step adds 2 minor to the numerator, at most one carry since minor <= major: no division a step). In 32 bits for
    // any line that fits a screen, without the 64-bit library divisions at every band a line reaches: there lo <= 3999
    // and minor2 <= major2 <= kSeg32Max, so the first numerator is under 4000 * kSeg32Max < 2^31.
    const uint16_t c = to565(rgb);
    const int64_t major2 = 2LL * (steep ? ady : dx), minor2 = 2LL * (steep ? dx : ady);
    if (major2 <= kSeg32Max) {
        const int32_t m = static_cast<int32_t>(major2), mi = static_cast<int32_t>(minor2);
        int32_t q = 0, rem = 0;
        if (m) {
            const int32_t num = mi * lo + m / 2;
            q = num / m;
            rem = num % m;
        }
        segSteps<int32_t>(lo, hi, b, steep, sx, sy, x0, y0, q, rem, m, mi, c, a255);
    } else {
        const int64_t num = minor2 * lo + major2 / 2;
        segSteps<int64_t>(lo, hi, b, steep, sx, sy, x0, y0, num / major2, num % major2, major2, minor2, c, a255);
    }
}

template <typename I>
void Canvas::segSteps(int lo, int hi, int b, bool steep, int sx, int sy, int x0, int y0, I q, I rem, I major2, I minor2,
                      uint16_t c, int a255) {
    const int h = b >> 1;
    if (a255 < 255 && b <= kFadeBlockMax) {
        fadedSeg<I>(lo, hi, b, steep, steep ? sy : sx, steep ? sx : sy, (steep ? y0 + oy : x0 + ox) - h,
                    (steep ? x0 + ox : y0 + oy) - h, q, rem, major2, minor2, c, a255);
        return;
    }
    // The canvas in locals (a store below would otherwise make the compiler reload it every pixel): pixel (x, y) is
    // px[org + x * stepX + y * stepY], as at() has it.
    uint16_t* const px = px_;
    const int org = org_, stepX = sx_, stepY = sy_, clx0 = cx0_, cly0 = cy0_, clx1 = cx1_, cly1 = cy1_;
    const uint16_t e = enc(c);
    int lastN = -2, lastX = 0, lastY = 0;   // the last opaque block drawn
    for (int n = lo; n <= hi; ++n) {
        int x, y;
        if (steep) {
            y = y0 + sy * n;
            x = x0 + sx * static_cast<int>(q);   // 0 for a single point (ady == 0)
        } else {
            x = x0 + sx * n;
            y = y0 + sy * static_cast<int>(q);
        }
        if (major2 && (rem += minor2) >= major2) {
            rem -= major2;
            ++q;
        }
        // Each block as fillRect(x - h, y - h, b, b, rgb, a) would draw it.
        const int X = x - h + ox, Y = y - h + oy;
        if (X >= clx1 || X + b <= clx0 || Y >= cly1 || Y + b <= cly0) continue;
        if (a255 < 255) {   // too wide to count: blended block by block
            blendRect(maxi(X, clx0), maxi(Y, cly0), mini(X + b, clx1), mini(Y + b, cly1), c, a255);
            continue;
        }
        // Opaque, a block one step from the last only adds its leading column and row (the rest is the same color
        // already), so a thick line costs about 2b pixels a step instead of b * b; each written straight, clipped as
        // fill565 would clip it.
        if (n > lo && n - 1 == lastN) {
            if (X != lastX) {
                const int cx = X > lastX ? X + b - 1 : X;
                if (cx >= clx0 && cx < clx1) {
                    const int ja = maxi(Y, cly0), jb = mini(Y + b, cly1);
                    uint16_t* p = px + org + cx * stepX + ja * stepY;
                    for (int j = ja; j < jb; ++j, p += stepY) *p = e;
                }
            }
            if (Y != lastY) {
                const int ry = Y > lastY ? Y + b - 1 : Y;
                if (ry >= cly0 && ry < cly1) {
                    const int ia = maxi(X, clx0), ib = mini(X + b, clx1);
                    uint16_t* p = px + org + ia * stepX + ry * stepY;
                    for (int i = ia; i < ib; ++i, p += stepX) *p = e;
                }
            }
        } else {
            fill565(X, Y, X + b, Y + b, c);
        }
        lastN = n; lastX = X; lastY = Y;
    }
}

// A faded seg (see seg): each block is blended over what's there, as fillRect(x - h, y - h, b, b, rgb, a) would blend
// it, so a pixel under k of the blocks is blended k times (the line builds up where they overlap). Here the blocks
// over each pixel are counted first and the pixel blended k times in a row: the same pixels, visiting each once
// instead of once per block (b * b loads, blends and stores a step). Steps lo..hi; step n's block covers major lines
// [m0 + sm * n, + b) and minor lines [n0 + sn * minor(n), + b), minor(n) being q at step lo and stepping as in seg. A
// line of the major axis has seen every block over it once the steps reach the block that starts it (stepping
// backwards, the one that ends it): blocks start a line apart.
template <typename I>
void Canvas::fadedSeg(int lo, int hi, int b, bool steep, int sm, int sn, int m0, int n0, I q, I rem, I major2, I minor2,
                      uint16_t c, int a255) {
    const uint32_t cs = spread(c), a32 = static_cast<uint32_t>(a255 * 32 + 128) >> 8;   // as mix565 has them
    const int majLo = steep ? cy0_ : cx0_, majHi = steep ? cy1_ : cx1_, minLo = steep ? cx0_ : cy0_,
              minHi = steep ? cx1_ : cy1_;
    // The canvas's addressing in locals: pixel (major line L, minor line p) is px[org + L * stepMaj + p * stepMin], as
    // at() has it.
    uint16_t* const px = px_;
    const int org = org_, stepMaj = steep ? sy_ : sx_, stepMin = steep ? sx_ : sy_;
    const bool swap = swap_;
    int first[kFadeBlockMax];   // the first minor line of each of the last b blocks, step n's at n % b
    int slot = lo % b;          // n % b
    for (int n = lo; n <= hi + b - 1; ++n, slot = slot + 1 == b ? 0 : slot + 1) {
        if (n <= hi) {
            first[slot] = n0 + sn * static_cast<int>(q);
            if (major2 && (rem += minor2) >= major2) {
                rem -= major2;
                ++q;
            }
        }
        // The line every block over it has now been seen for, and the steps whose blocks cover it.
        const int line = sm > 0 ? m0 + n : m0 - n + b - 1;
        const int nA = maxi(lo, n - b + 1), nB = mini(hi, n);
        if (line < majLo || line >= majHi || nA > nB) continue;
        // Along the line, the blocks' spans [first, first + b) as a count: +1 where one starts, -1 past its end. The
        // starts move at most one line a step, one way, so the at most b of them lie within b - 1 lines of each other:
        // every index below is under 2b - 1.
        const int iA = (slot - (n - nA) + b) % b, iB = (slot - (n - nB) + b) % b;   // nA % b, nB % b
        const int fa = first[iA], fb = first[iB], base = mini(fa, fb), span = maxi(fa, fb) - base + b;
        int cnt[2 * kFadeBlockMax];
        for (int t = 0; t < span; ++t) cnt[t] = 0;
        for (int k = nA, i = iA; k <= nB; ++k, i = i + 1 == b ? 0 : i + 1) {
            const int s = first[i] - base;
            ++cnt[s];
            --cnt[s + b];
        }
        // The count up to the clip, then each pixel inside it blended as many times as blocks cover it.
        const int t0 = maxi(0, minLo - base), t1 = mini(span, minHi - base);
        if (t0 >= t1) continue;
        int k = 0;
        for (int t = 0; t < t0; ++t) k += cnt[t];
        uint16_t* d = px + org + line * stepMaj + (base + t0) * stepMin;
        for (int t = t0; t < t1; ++t, d += stepMin) {
            k += cnt[t];
            if (k <= 0) continue;
            uint32_t v = spread(swap ? swap16(*d) : *d);
            for (int r = 0; r < k; ++r) v = lerpS(v, cs, a32);
            const uint16_t o = unspread(v);
            *d = swap ? swap16(o) : o;
        }
    }
}

void Canvas::dot(float xf, float yf, int size, uint32_t rgb, float a) {
    const int h = size >> 1, x = roundi(xf), y = roundi(yf);
    if (rec_) {
        if (Recording::Op* op = rec_->add(*this, kDot)) {
            op->a[0].f = xf; op->a[1].f = yf; op->a[2].i = size; op->a[3].u = rgb; op->a[4].f = a;
            rec_->keep(op, y - h + oy, y - h + size + oy, *this);
        }
        return;
    }
    fillRect(x - h + 1, y - h, size - 2, size, rgb, a);
    fillRect(x - h, y - h + 1, 1, size - 2, rgb, a);          // the two side columns, so a faded dot fades evenly
    fillRect(x - h + size - 1, y - h + 1, 1, size - 2, rgb, a);
}

// ---------- scaled blits ----------

// Bilinear, separably: each destination row first mixes its two source rows into a row buffer (spread colors),
// then each pixel mixes two neighbors of that, with 5-bit weights. Taps touching the key (a card's corners and
// edges) are flagged (bit 31, never set in a spread color) and weighted by their opaque taps only, so edges stay
// clean.
void Canvas::blitScaled(const uint16_t* src, int sw, int sh, uint16_t key, float cx, float cy, float scale, float a,
                        bool overBlack) {
    if (rec_) { rec_->fail(); return; }   // its source may change before playback
    const Timed timed(Prim::Smooth);
    const float dw = sw * scale, dh = sh * scale, x0 = cx - dw / 2 + ox, y0 = cy - dh / 2 + oy, inv1 = 1.f / scale;
    if (fabsf(scale - 1) < 1e-4f && x0 == floorf(x0) && y0 == floorf(y0)) {   // unscaled on whole pixels: a copy
        blitNearest(src, sw, sh, key, cx, cy, 1.f, a, overBlack);
        return;
    }
    const int ka = static_cast<int>(256 * a * alpha);
    const int inv = static_cast<int>(65536 * inv1);   // source step per destination pixel, 16.16
    const int i0 = clampi(static_cast<int>(floorf(x0)), cx0_, cx1_), i1 = clampi(static_cast<int>(ceilf(x0 + dw)), cx0_, cx1_);
    const int j0 = clampi(static_cast<int>(floorf(y0)), cy0_, cy1_), j1 = clampi(static_cast<int>(ceilf(y0 + dh)), cy0_, cy1_);
    if (i0 >= i1 || j0 >= j1 || ka <= 0) return;
    const uint32_t a32 = static_cast<uint32_t>(ka + 4) >> 3;
    const int u0 = static_cast<int>(((i0 + 0.5f - x0) * inv1 - 0.5f) * 65536);
    constexpr int kMaxW = 512;
    constexpr uint32_t kFlag = 0x80000000u;
    static uint32_t mixed[kMaxW];
    const bool buffered = sw <= kMaxW;
    const int sLo = maxi(0, u0 >> 16), sHi = mini(sw - 1, ((u0 + (i1 - i0 - 1) * inv) >> 16) + 1);
    for (int j = j0; j < j1; ++j) {
        const int v = static_cast<int>(((j + 0.5f - y0) * inv1 - 0.5f) * 256);   // 24.8
        const int iv = v >> 8, fv = v & 255;
        const uint16_t* r0 = iv >= 0 && iv < sh ? src + iv * sw : nullptr;
        const uint16_t* r1 = iv + 1 >= 0 && iv + 1 < sh ? src + (iv + 1) * sw : nullptr;
        if (buffered) {
            const uint32_t fv32 = static_cast<uint32_t>(fv) >> 3;
            for (int s = sLo; s <= sHi; ++s) {
                const uint32_t c0 = r0 ? r0[s] : key, c1 = r1 ? r1[s] : key;
                mixed[s] = c0 == key || c1 == key ? kFlag : lerpS(spread(c0), spread(c1), fv32);
            }
        }
        uint16_t* p = &at(i0, j);
        int u = u0;
        for (int i = i0; i < i1; ++i, u += inv, p += sx_) {
            const int iu = u >> 16, fu = (u >> 8) & 255;
            uint32_t col;   // spread
            int al = ka;
            uint32_t m0 = kFlag, m1 = kFlag;
            if (buffered && iu >= 0 && iu + 1 < sw) { m0 = mixed[iu]; m1 = mixed[iu + 1]; }
            if (!((m0 | m1) & kFlag)) {
                col = lerpS(m0, m1, static_cast<uint32_t>(fu) >> 3);
            } else {
                int wsum = 0, r = 0, g = 0, b = 0;
                for (int q = 0; q < 4; ++q) {
                    const int su = iu + (q & 1);
                    const uint16_t* row = q >> 1 ? r1 : r0;
                    if (!row || su < 0 || su >= sw) continue;
                    const uint16_t c = row[su];
                    if (c == key) continue;
                    const int wq = ((q & 1) ? fu : 256 - fu) * ((q >> 1) ? fv : 256 - fv) >> 8;   // 0..256
                    r += wq * (c >> 11); g += wq * ((c >> 5) & 63); b += wq * (c & 31);
                    wsum += wq;
                }
                if (wsum < 3) continue;
                col = spread(static_cast<uint32_t>((r / wsum) << 11 | (g / wsum) << 5 | (b / wsum)));
                al = (ka * wsum) >> 8;
            }
            if (al >= 255) { *p = enc(unspread(col)); continue; }
            const uint32_t aa = al == ka ? a32 : static_cast<uint32_t>(al + 4) >> 3;
            *p = enc(unspread(lerpS(overBlack ? 0 : spread(dec(*p)), col, aa)));
        }
    }
}

void Canvas::blitNearest(const uint16_t* src, int sw, int sh, uint16_t key, float cx, float cy, float scale, float a,
                         bool overBlack) {
    if (rec_) { rec_->fail(); return; }   // its source may change before playback
    const Timed timed(Prim::Nearest);
    const float dw = sw * scale, dh = sh * scale, x0 = cx - dw / 2 + ox, y0 = cy - dh / 2 + oy, inv1 = 1.f / scale;
    const int al = static_cast<int>(256 * a * alpha);
    const int inv = static_cast<int>(65536 * inv1);
    int i0 = clampi(static_cast<int>(floorf(x0)), cx0_, cx1_);
    const int i1 = clampi(static_cast<int>(ceilf(x0 + dw)), cx0_, cx1_);
    const int j0 = clampi(static_cast<int>(floorf(y0)), cy0_, cy1_), j1 = clampi(static_cast<int>(ceilf(y0 + dh)), cy0_, cy1_);
    if (i0 >= i1 || j0 >= j1 || al <= 0 || inv <= 0) return;
    // The columns whose source pixel is inside the image, worked out once for every row.
    int u = static_cast<int>((i0 + 0.5f - x0) * inv1 * 65536);
    if (u < 0) { const int k = (-u + inv - 1) / inv; i0 += k; u += k * inv; }
    const int limit = sw << 16;
    if (u >= limit || i0 >= i1) return;
    const int n = mini(i1 - i0, (limit - u + inv - 1) / inv);
    const uint32_t a32 = static_cast<uint32_t>(al + 4) >> 3;
    for (int j = j0; j < j1; ++j) {
        const int sv = static_cast<int>((j + 0.5f - y0) * inv1);
        if (sv < 0 || sv >= sh) continue;
        uint16_t* p = &at(i0, j);
        const uint16_t* srow = src + sv * sw;
        if (al >= 255) {
            if (swap_) nearestRun<true, true, false>(p, sx_, srow, u, inv, n, key, a32);
            else nearestRun<false, true, false>(p, sx_, srow, u, inv, n, key, a32);
        } else if (overBlack) {
            if (swap_) nearestRun<true, false, true>(p, sx_, srow, u, inv, n, key, a32);
            else nearestRun<false, false, true>(p, sx_, srow, u, inv, n, key, a32);
        } else {
            if (swap_) nearestRun<true, false, false>(p, sx_, srow, u, inv, n, key, a32);
            else nearestRun<false, false, false>(p, sx_, srow, u, inv, n, key, a32);
        }
    }
}

// ---------- cell grids (the dock map's pixel Chicago) ----------

void Canvas::cells(const uint8_t* grid, int cols, int rows, int x, int y, int cell, const uint32_t* pal, int nPal,
                   uint32_t version) {
    nPal = mini(maxi(nPal, 0), 16);
    if (cell < 1 || cols < 1 || rows < 1) return;
    if (rec_) {
        if (Recording::Op* op = rec_->add(*this, kCells)) {
            op->a[0].i = cols; op->a[1].i = rows; op->a[2].i = x; op->a[3].i = y; op->a[4].i = cell;
            op->a[5].u = version; op->a[6].i = nPal;
            if (uint8_t* d = rec_->reserve(op, sizeof(grid) + nPal * sizeof(uint32_t))) {
                memcpy(d, &grid, sizeof(grid));
                memcpy(d + sizeof(grid), pal, nPal * sizeof(uint32_t));
                rec_->keep(op, y + oy, y + oy + rows * cell, *this);
            }
        }
        return;
    }
    const Timed timed(Prim::Rect);
    uint16_t c565[16];
    for (int k = 0; k < nPal; ++k) c565[k] = to565(pal[k]);
    const int X0 = x + ox, Y0 = y + oy;
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    const int j0 = maxi(0, floorDiv(cy0_ - Y0, cell)), j1 = mini(rows - 1, floorDiv(cy1_ - 1 - Y0, cell));
    const int i0 = maxi(0, floorDiv(cx0_ - X0, cell)), i1 = mini(cols - 1, floorDiv(cx1_ - 1 - X0, cell));
    for (int j = j0; j <= j1; ++j) {
        const uint8_t* row = grid + static_cast<size_t>(j) * cols;
        const int ya = Y0 + j * cell;
        for (int i = i0; i <= i1;) {
            const uint8_t v = row[i];
            int k = i + 1;
            while (k <= i1 && row[k] == v) ++k;
            if (v < nPal) fill565(X0 + i * cell, ya, X0 + k * cell, ya + cell, c565[v]);
            i = k;
        }
    }
}

// ---------- recording ----------

Canvas Recording::begin(int w, int h, int bandRows) {
    static_assert(sizeof(Op) % 4 == 0, "ops are hashed as 32-bit words");
    Canvas c;   // whole-screen bounds, no pixels: every call records
    c.sy_ = w;
    c.bx1_ = w;
    c.by1_ = h;
    c.resetClip();
    c.rec_ = this;
    nOps_ = arenaUsed_ = refsUsed_ = 0;
    rows_ = bandRows > 0 ? bandRows : 1;
    nBands_ = h > 0 ? (h + rows_ - 1) / rows_ : 0;
    ok_ = bandRows > 0 && w > 0 && h > 0 && nBands_ <= kBands;
    for (int b = 0; b < kBands; ++b) {
        bandHash_[b] = kFnvBasis;
        count_[b] = 0;
    }
    return c;
}

Recording::Op* Recording::add(const Canvas& cv, uint8_t kind) {
    if (!ok_) return nullptr;
    if (nOps_ >= kOps) { ok_ = false; return nullptr; }
    Op* op = &ops_[nOps_];
    memset(op, 0, sizeof(Op));   // padding included: ops are hashed whole
    op->kind = kind;
    op->ox = cv.ox; op->oy = cv.oy;
    op->cx0 = cv.cx0_; op->cy0 = cv.cy0_; op->cx1 = cv.cx1_; op->cy1 = cv.cy1_;
    op->alpha = cv.alpha;
    return op;
}

uint8_t* Recording::reserve(Op* op, size_t n) {
    const size_t padded = (n + 3) & ~static_cast<size_t>(3);
    if (padded > static_cast<size_t>(kArena - arenaUsed_)) { ok_ = false; return nullptr; }
    uint8_t* d = arena_ + arenaUsed_;
    memset(d, 0, padded);   // hashed in whole words
    op->data = static_cast<uint32_t>(arenaUsed_);
    op->len = static_cast<uint32_t>(n);
    arenaUsed_ += static_cast<int>(padded);
    return d;
}

void Recording::keep(Op* op, int y0, int y1, const Canvas& cv, bool clipped) {
    if (!ok_) return;
    int lo = maxi(y0, cv.by0_), hi = mini(y1, cv.by1_);
    if (clipped) { lo = maxi(lo, cv.cy0_); hi = mini(hi, cv.cy1_); }
    if (lo >= hi) return;   // it touches no row: no pixel changes
    // The call's hash: the op (its data's place in the arena aside, which shifts with what came before) and its data.
    const uint32_t at = op->data;
    op->data = 0;
    uint64_t h = fnvWords(kFnvBasis, reinterpret_cast<const uint8_t*>(op), sizeof(Op) / 4);
    op->data = at;
    h = fnvWords(h, arena_ + at, (op->len + 3) / 4);
    const int b0 = (lo - cv.by0_) / rows_, b1 = (hi - 1 - cv.by0_) / rows_;
    if (refsUsed_ + (b1 - b0 + 1) > kRefs) { ok_ = false; return; }
    refsUsed_ += b1 - b0 + 1;
    op->b0 = static_cast<uint8_t>(b0);
    op->b1 = static_cast<uint8_t>(b1);
    for (int b = b0; b <= b1; ++b) {
        bandHash_[b] = (bandHash_[b] ^ h) * kFnvPrime;
        ++count_[b];
    }
    ++nOps_;
}

bool Recording::end() {
    // A band is drawn over what the last band drawn left in the band buffer: it must start with a clear.
    if (!ok_ || nOps_ == 0 || ops_[0].kind != kClear) return ok_ = false;
    uint16_t at[kBands];
    int n = 0;
    for (int b = 0; b < nBands_; ++b) {
        first_[b] = at[b] = static_cast<uint16_t>(n);
        n += count_[b];
    }
    first_[nBands_] = static_cast<uint16_t>(n);
    for (int i = 0; i < nOps_; ++i)
        for (int b = ops_[i].b0; b <= ops_[i].b1; ++b) refs_[at[b]++] = static_cast<uint16_t>(i);
    return true;
}

void Recording::play(Canvas& cv, int b) const {
    const int ox = cv.ox, oy = cv.oy, cx0 = cv.cx0_, cy0 = cv.cy0_, cx1 = cv.cx1_, cy1 = cv.cy1_;
    const float alpha = cv.alpha;
    for (int k = first_[b]; k < first_[b + 1]; ++k) {
        const Op& op = ops_[refs_[k]];
        // The canvas as it was at the call; its clip within this band, as clip() would have made it.
        cv.ox = op.ox;
        cv.oy = op.oy;
        cv.alpha = op.alpha;
        cv.cx0_ = clampi(op.cx0, cv.bx0_, cv.bx1_);
        cv.cy0_ = clampi(op.cy0, cv.by0_, cv.by1_);
        cv.cx1_ = clampi(op.cx1, cv.bx0_, cv.bx1_);
        cv.cy1_ = clampi(op.cy1, cv.by0_, cv.by1_);
        const Arg* a = op.a;
        const uint8_t* d = arena_ + op.data;
        const char* s = reinterpret_cast<const char*>(d);
        const Align al = static_cast<Align>(op.small[0]);
        const Base base = static_cast<Base>(op.small[1]);
        switch (op.kind) {
            case kClear: cv.clear(a[0].u); break;
            case kRect: cv.fillRect(a[0].f, a[1].f, a[2].f, a[3].f, a[4].u, a[5].f); break;
            case kBlend: cv.blend(a[0].i, a[1].i, a[2].u, a[3].i); break;
            case kShape: cv.fillPixelShape(a[0].i, a[1].i, a[2].i, a[3].i, a[4].i, d, a[5].i, a[6].u, a[7].f, op.flag); break;
            case kPlate: {
                PlateStyle ps;
                ps.fill = a[4].u; ps.fill2 = a[5].u; ps.rim = a[6].u; ps.rimAlpha = a[7].f;
                ps.rimW = a[8].i; ps.edge = a[9].i; ps.block = a[10].i;
                ps.steps[0] = a[11].u & 255; ps.steps[1] = a[11].u >> 8 & 255; ps.steps[2] = a[11].u >> 16 & 255;
                ps.nSteps = static_cast<uint8_t>(a[11].u >> 24);
                ps.highlight = op.flag;
                cv.plate(a[0].i, a[1].i, a[2].i, a[3].i, ps);
                break;
            }
            case kChip: cv.chip(s, a[0].i, a[1].i, a[2].u, a[3].u); break;
            case kText: cv.text(op.font, s, a[0].f, a[1].f, a[2].u, al, base, a[3].f); break;
            case kOutlined: cv.textOutlined(op.font, s, a[0].f, a[1].f, a[2].u, a[3].u, a[4].i, al, base); break;
            case kDigits: cv.pixelText(s, a[0].f, a[1].i, a[2].i, a[3].u, al, a[4].i); break;
            case kInk: {
                const char* rows[kInkRows];
                const int n = a[5].i;
                for (int j = 0; j < n; ++j) {
                    rows[j] = s;
                    s += strlen(s) + 1;
                }
                cv.ink(rows, n, a[0].i, a[1].i, a[2].i, a[3].u, a[4].f);
                break;
            }
            case kSprite: {
                const size_t n = static_cast<size_t>(a[0].i) * a[1].i, palAt = (n + 1) & ~static_cast<size_t>(1);
                uint16_t pal[256];
                memcpy(pal, d + palAt, (a[7].i + 1) * sizeof(uint16_t));
                cv.sprite(d, a[0].i, a[1].i, a[2].i, a[3].i, a[4].i, a[5].i, pal, a[6].i);
                break;
            }
            case kCircle: cv.fillCircle(a[0].f, a[1].f, a[2].f, a[3].u, a[4].f); break;
            case kLine: cv.line(a[0].f, a[1].f, a[2].f, a[3].f, a[4].f, a[5].u, a[6].f); break;
            case kArc: cv.arc(a[0].f, a[1].f, a[2].f, a[3].f, a[4].f, a[5].f, a[6].u, a[7].f, op.flag); break;
            case kArrow: cv.arrowCircle(a[0].f, a[1].f, a[2].f, a[3].u, a[4].u); break;
            case kSeg: cv.seg(a[0].f, a[1].f, a[2].f, a[3].f, a[4].i, a[5].u, a[6].f); break;
            case kDot: cv.dot(a[0].f, a[1].f, a[2].i, a[3].u, a[4].f); break;
            case kCells: {
                const uint8_t* grid;
                memcpy(&grid, d, sizeof(grid));
                uint32_t pal[16];
                memcpy(pal, d + sizeof(grid), a[6].i * sizeof(uint32_t));
                cv.cells(grid, a[0].i, a[1].i, a[2].i, a[3].i, a[4].i, pal, a[6].i, a[5].u);
                break;
            }
            default: break;
        }
    }
    cv.ox = ox; cv.oy = oy; cv.alpha = alpha;
    cv.cx0_ = cx0; cv.cy0_ = cy0; cv.cx1_ = cx1; cv.cy1_ = cy1;
}

}  // namespace ui
