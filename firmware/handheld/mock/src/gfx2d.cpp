#include "gfx2d.h"

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
        return static_cast<uint32_t>(lroundf(va + (vb - va) * t)) << s;
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
                                                               "sprite", "circle", "line", "arc", "smooth", "nearest"};

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

void Canvas::resetClip() { cx0_ = bx0_; cy0_ = by0_; cx1_ = bx1_; cy1_ = by1_; }

void Canvas::clip(int x, int y, int w, int h) {
    cx0_ = clampi(x + ox, bx0_, bx1_); cy0_ = clampi(y + oy, by0_, by1_);
    cx1_ = clampi(x + ox + w, bx0_, bx1_); cy1_ = clampi(y + oy + h, by0_, by1_);
}

void Canvas::clear(uint32_t rgb) {
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

void Canvas::blend(int x, int y, uint32_t rgb, int a) {
    if (x < cx0_ || y < cy0_ || x >= cx1_ || y >= cy1_ || a <= 0) return;
    uint16_t& d = at(x, y);
    d = enc(mix565(dec(d), rgb, a));
}

// ---------- fills and plates ----------

void Canvas::fillRect(float x, float y, float w, float h, uint32_t rgb, float a) {
    const Timed timed(Prim::Rect);
    const int x0 = static_cast<int>(lroundf(x)) + ox, x1 = static_cast<int>(lroundf(x + w)) + ox;
    const int y0 = static_cast<int>(lroundf(y)) + oy, y1 = static_cast<int>(lroundf(y + h)) + oy;
    if (outside(x0, y0, x1, y1)) return;
    const int a255 = static_cast<int>(lroundf(255 * a * alpha));
    if (a255 <= 0) return;
    if (a255 >= 255) { fill565(x0, y0, x1, y1, to565(rgb)); return; }
    for (int j = maxi(y0, cy0_); j < mini(y1, cy1_); ++j)
        for (int i = maxi(x0, cx0_); i < mini(x1, cx1_); ++i) {
            uint16_t& d = at(i, j);
            d = enc(mix565(dec(d), rgb, a255));
        }
}

void Canvas::fillPixelShape(int x, int y, int w, int h, int block, const uint8_t* steps, int nSteps, uint32_t rgb,
                            float a, bool bottom) {
    const Timed timed(Prim::Shape);
    const int X = x + ox, Y = y + oy;
    if (outside(X, Y, X + w, Y + h)) return;
    const int a255 = static_cast<int>(lroundf(255 * a * alpha));
    if (a255 <= 0) return;
    const uint16_t c = to565(rgb);
    const int j0 = maxi(0, cy0_ - Y), j1 = mini(h, cy1_ - Y);
    for (int j = j0; j < j1; ++j) {
        const int in = rowInset(j, h, block, steps, nSteps, bottom);
        if (a255 >= 255) { fill565(X + in, Y + j, X + w - in, Y + j + 1, c); continue; }
        for (int i = maxi(in, cx0_ - X); i < mini(w - in, cx1_ - X); ++i) {
            uint16_t& d = at(X + i, Y + j);
            d = enc(mix565(dec(d), rgb, a255));
        }
    }
}

void Canvas::plate(int x, int y, int w, int h, const PlateStyle& s) {
    const Timed timed(Prim::Plate);
    if (s.edge) fillPixelShape(x, y + s.edge, w, h, s.block, s.steps, s.nSteps, scaleRgb(s.fill2, 0.55f));
    const int X = x + ox, Y = y + oy;
    if (outside(X, Y, X + w, Y + h)) return;
    const int a255 = static_cast<int>(lroundf(255 * alpha));
    if (a255 <= 0) return;
    const int r = s.rimW;
    const int j0 = maxi(0, cy0_ - Y), j1 = mini(h, cy1_ - Y);
    const int rimT = static_cast<int>(s.rimAlpha * 256 + 0.5f);
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
        if (a255 >= 255) {
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
}

int Canvas::chip(const char* t, int x, int y, uint32_t bg, uint32_t fg) {
    const Font* f = font(900, 11);
    const int w = static_cast<int>(ceilf(measure(f, t))) + 12;
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
    const Timed timed(Prim::Text);
    const float w = measure(f, t);
    float pen = al == Align::Left ? x : al == Align::Center ? x - w / 2 : x - w;
    const float by = base == Base::Top ? y + f->top : base == Base::Middle ? y + f->middle : y;
    const int byi = static_cast<int>(lroundf(by)) + oy;
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
        const int gx = static_cast<int>(lroundf(pen)) + g->x + ox, gy = byi + g->y;
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

int Canvas::pixelWidth(const char* t, int s) {
    int w = 0;
    for (int i = 0; t[i]; ++i) w += static_cast<int>(strlen(pf(t[i]).rows[0])) + (i ? 1 : 0);
    return w * s;
}

int Canvas::pixelText(const char* t, float x, int y, int s, uint32_t rgb, Align al) {
    const Timed timed(Prim::Digits);
    const int w = pixelWidth(t, s);
    int cx = static_cast<int>(lroundf(al == Align::Center ? x - w / 2.f : al == Align::Right ? x - w : x));
    if (outside(cx + ox, y + oy, cx + ox + w, y + oy + 7 * s)) return w;
    for (int k = 0; t[k]; ++k) {
        const PG& g = pf(t[k]);
        const int gw = static_cast<int>(strlen(g.rows[0]));
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < gw; ++c)
                if (g.rows[r][c] == '#') fillRect(cx + c * s, y + r * s, s, s, rgb);
        cx += (gw + 1) * s;
    }
    return w;
}

// ---------- pixel art ----------

void Canvas::ink(const char* const* rows, int nRows, int x, int y, int s, uint32_t rgb, float a) {
    const Timed timed(Prim::Ink);
    if (y + oy + nRows * s <= cy0_ || y + oy >= cy1_) return;
    for (int j = 0; j < nRows; ++j)
        for (int i = 0; rows[j][i]; ++i)
            if (rows[j][i] != '.') fillRect(x + i * s, y + j * s, s, s, rgb, a);
}

void Canvas::sprite(const uint8_t* idx, int w, int h, int dx, int dy, int x, int y, const uint16_t* pal565, int s) {
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
    if (outside(static_cast<int>(cx - r - 2) + ox, static_cast<int>(cy - r - 2) + oy,
                static_cast<int>(cx + r + 3) + ox, static_cast<int>(cy + r + 3) + oy)) return;
    fillCircle(cx, cy, r, rgb);
    const float lw = r * 0.26f;
    line(cx - r * 0.45f, cy, cx + r * 0.42f, cy, lw, inkRgb);
    line(cx + r * 0.05f, cy - r * 0.4f, cx + r * 0.45f, cy, lw, inkRgb);
    line(cx + r * 0.45f, cy, cx + r * 0.05f, cy + r * 0.4f, lw, inkRgb);
}

// ---------- scaled blits ----------

// Bilinear, separably: each destination row first mixes its two source rows into a row buffer (spread colors),
// then each pixel mixes two neighbors of that, with 5-bit weights. Taps touching the key (a card's corners and
// edges) are flagged (bit 31, never set in a spread color) and weighted by their opaque taps only, so edges stay
// clean.
void Canvas::blitScaled(const uint16_t* src, int sw, int sh, uint16_t key, float cx, float cy, float scale, float a,
                        bool overBlack) {
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

}  // namespace ui
