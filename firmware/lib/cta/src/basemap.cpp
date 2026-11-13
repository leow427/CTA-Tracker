#include "basemap.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace cta {

namespace {

// Big tables live in PSRAM on the dock (internal RAM is for the bands and the stacks).
void* bigAlloc(size_t n) {
#if defined(ESP_PLATFORM)
    if (void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return p;
#endif
    return malloc(n);
}

constexpr uint8_t kArea = 1;
// Real widths in meters: a street is drawn this wide when that's more than a cell, else one cell wide.
constexpr float kWidthM[kClassCount] = {0, 0, 0, 0, 10, 20, 40};

struct Feature {
    uint8_t cls, area;
    uint16_t nRings;
    uint32_t ring0;               // its first ring in gRings
    int32_t la0, la1, lo0, lo1;   // its box, in 1e-5 degrees from the origin
};
struct Ring { uint32_t pt0, n; };   // its points: gPts[2 * pt0 ...], lat then lon

Feature* gFeats = nullptr;
Ring* gRings = nullptr;
int32_t* gPts = nullptr;
uint32_t gNFeats = 0, gMaxAreaPts = 0;
bool gTried = false, gOk = false;

struct Edge { float y0, y1, x, dxdy; };
Edge* gEdges = nullptr;
float* gXs = nullptr;
uint32_t gVersion = 0;

// The packing (tools/basemap_build.py, pack): zigzag varints; blocks of one class and kind; every point a delta from
// the one before it. Counting runs first so each table is allocated once.
struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;
    int32_t vint() {
        uint32_t v = 0;
        int s = 0;
        while (true) {
            if (p >= end || s > 28) { ok = false; return 0; }
            const uint8_t c = *p++;
            v |= static_cast<uint32_t>(c & 0x7f) << s;
            s += 7;
            if (!(c & 0x80)) break;
        }
        return (v & 1) ? -static_cast<int32_t>((v + 1) >> 1) : static_cast<int32_t>(v >> 1);
    }
};

bool walk(bool fill, uint32_t& nf, uint32_t& nr, uint32_t& np) {
    Reader r{kBasemap + 4, kBasemap + kBasemapSize};
    nf = nr = np = 0;
    int32_t la = 0, lo = 0;
    for (int32_t nb = r.vint(); nb > 0 && r.ok; --nb) {
        if (r.p + 2 > r.end) return false;
        const uint8_t cls = r.p[0], kind = r.p[1];
        r.p += 2;
        if (cls >= kClassCount) return false;
        for (int32_t nfb = r.vint(); nfb > 0 && r.ok; --nfb) {
            const int32_t rings = kind == kArea ? r.vint() : 1;
            Feature* f = fill ? &gFeats[nf] : nullptr;
            if (f) *f = Feature{cls, kind, static_cast<uint16_t>(rings), nr, INT32_MAX, INT32_MIN, INT32_MAX, INT32_MIN};
            uint32_t pts = 0;
            for (int32_t k = 0; k < rings && r.ok; ++k) {
                const int32_t n = r.vint();
                if (fill) gRings[nr] = Ring{np, static_cast<uint32_t>(n)};
                for (int32_t j = 0; j < n && r.ok; ++j) {
                    la += r.vint();
                    lo += r.vint();
                    if (fill) {
                        gPts[2 * np] = la;
                        gPts[2 * np + 1] = lo;
                        f->la0 = std::min(f->la0, la); f->la1 = std::max(f->la1, la);
                        f->lo0 = std::min(f->lo0, lo); f->lo1 = std::max(f->lo1, lo);
                    }
                    ++np;
                }
                pts += static_cast<uint32_t>(n);
                ++nr;
            }
            if (kind == kArea) gMaxAreaPts = std::max(gMaxAreaPts, pts);
            ++nf;
        }
    }
    return r.ok;
}

}  // namespace

bool basemapLoad() {
    if (gTried) return gOk;
    gTried = true;
    if (kBasemapSize < 4 || memcmp(kBasemap, "CBM2", 4) != 0) return false;
    uint32_t nf, nr, np;
    if (!walk(false, nf, nr, np)) return false;
    gFeats = static_cast<Feature*>(bigAlloc(sizeof(Feature) * nf));
    gRings = static_cast<Ring*>(bigAlloc(sizeof(Ring) * nr));
    gPts = static_cast<int32_t*>(bigAlloc(sizeof(int32_t) * 2 * np));
    gEdges = static_cast<Edge*>(bigAlloc(sizeof(Edge) * (gMaxAreaPts + 1)));
    gXs = static_cast<float*>(bigAlloc(sizeof(float) * (gMaxAreaPts + 1)));
    if (!gFeats || !gRings || !gPts || !gEdges || !gXs) return false;
    gOk = walk(true, nf, nr, np);
    gNFeats = nf;
    return gOk;
}

bool basemapRaster(const MapFrame& f, int panX, int panY, int x, int y, int w, int h, int cell, bool sideStreets,
                   BasemapGrid& g) {
    if (!basemapLoad() || cell < 1) return false;
    auto mod = [](int a, int m) { return ((a % m) + m) % m; };
    const int ox = x + mod(panX, cell) - cell, oy = y + mod(panY, cell) - cell;
    const int cols = (w + cell - 1) / cell + 2, rows = (h + cell - 1) / cell + 2;
    if (!g.cells || g.cols * g.rows < cols * rows) {
        free(g.cells);
        g.cells = static_cast<uint8_t*>(bigAlloc(static_cast<size_t>(cols) * rows));
        if (!g.cells) return false;
    }
    g.cols = cols; g.rows = rows; g.ox = ox; g.oy = oy; g.cell = cell; g.version = ++gVersion;
    uint8_t* grid = g.cells;
    memset(grid, kLand, static_cast<size_t>(cols) * rows);

    // A point in 1e-5 degrees from the origin -> cell coordinates. mapProject rounds the point and then adds the pan.
    const float px = f.kmX * f.pxPerKm, py = f.kmY * f.pxPerKm;
    const float sx = px * 1e-5f / cell, sy = -py * 1e-5f / cell;
    const float bx = (f.cx + (kBasemapOrigin[1] - f.lon0) * px + panX - ox) / cell;
    const float by = (f.cy - (kBasemapOrigin[0] - f.lat0) * py + panY - oy) / cell;
    const float cellM = cell * 1000.f / f.pxPerKm;
    const bool showSide = sideStreets && cellM <= 33, showMain = cellM <= 250;
    // the grid's box in data units, to skip what's off it
    const float lo0 = -bx / sx, lo1 = (cols - bx) / sx, la1 = -by / sy, la0 = (rows - by) / sy;
    const float ux = fabsf(1 / sx), uy = fabsf(1 / sy);
    auto put = [&](int i, int j, uint8_t c) {
        if (i < 0 || j < 0 || i >= cols || j >= rows) return;
        uint8_t& d = grid[j * cols + i];
        if (d < c) d = c;
    };

    for (uint32_t n = 0; n < gNFeats; ++n) {
        const Feature& ft = gFeats[n];
        if (ft.cls == kSideStreet && !showSide) continue;
        if (ft.cls == kMainStreet && !showMain) continue;
        const int width = std::max(1, static_cast<int>(lroundf(kWidthM[ft.cls] / cellM)));
        const float m = ft.area ? 0 : width / 2.f + 1;   // a wide road counts as on screen that much earlier
        if (ft.lo1 < lo0 - m * ux || ft.lo0 > lo1 + m * ux || ft.la1 < la0 - m * uy || ft.la0 > la1 + m * uy) continue;
        if (ft.area) {
            // An area whose box holds no cell center can't fill a cell; else even-odd scanlines at cell centers.
            const float xa = bx + ft.lo0 * sx, xb = bx + ft.lo1 * sx, ya = by + ft.la1 * sy, yb = by + ft.la0 * sy;
            if (ceilf(xa - 0.5f) > ceilf(xb - 0.5f) - 1 || ceilf(ya - 0.5f) > ceilf(yb - 0.5f) - 1) continue;
            uint32_t ne = 0;
            float jmin = 1e30f, jmax = -1e30f;
            for (uint32_t r = 0; r < ft.nRings; ++r) {
                const Ring& rg = gRings[ft.ring0 + r];
                const int32_t* p = gPts + 2 * rg.pt0;
                for (uint32_t k = 0; k + 1 < rg.n; ++k) {
                    const float x0 = bx + p[2 * k + 1] * sx, y0 = by + p[2 * k] * sy;
                    const float x1 = bx + p[2 * k + 3] * sx, y1 = by + p[2 * k + 2] * sy;
                    if (y0 == y1) continue;
                    const float ylo = std::min(y0, y1), yhi = std::max(y0, y1);
                    if (yhi < 0.5f || ylo > rows) continue;
                    gEdges[ne++] = y0 < y1 ? Edge{y0, y1, x0, (x1 - x0) / (y1 - y0)} : Edge{y1, y0, x1, (x0 - x1) / (y0 - y1)};
                    jmin = std::min(jmin, ylo);
                    jmax = std::max(jmax, yhi);
                }
            }
            if (!ne) continue;
            std::sort(gEdges, gEdges + ne, [](const Edge& a, const Edge& b) { return a.y0 < b.y0; });
            const int j0 = std::max(0, static_cast<int>(ceilf(jmin - 0.5f)));
            const int j1 = std::min(rows - 1, static_cast<int>(floorf(jmax - 0.5f)));
            uint32_t next = 0, nActive = 0;   // gEdges[0, nActive) is the active list, compacted as rows go by
            for (int j = j0; j <= j1; ++j) {
                const float yc = j + 0.5f;
                while (next < ne && gEdges[next].y0 <= yc) gEdges[nActive++] = gEdges[next++];
                uint32_t keep = 0, nx = 0;
                for (uint32_t k = 0; k < nActive; ++k) {
                    const Edge e = gEdges[k];
                    if (e.y1 <= yc) continue;
                    gEdges[keep++] = e;
                    gXs[nx++] = e.x + (yc - e.y0) * e.dxdy;
                }
                nActive = keep;
                std::sort(gXs, gXs + nx);
                for (uint32_t k = 0; k + 1 < nx; k += 2) {
                    const int i0 = std::max(0, static_cast<int>(ceilf(gXs[k] - 0.5f)));
                    const int i1 = std::min(cols - 1, static_cast<int>(ceilf(gXs[k + 1] - 0.5f)) - 1);
                    for (int i = i0; i <= i1; ++i) put(i, j, ft.cls);
                }
            }
        } else {
            // A line of square brushes (Bresenham over cells), as wide as the road really is.
            const int hw = (width - 1) >> 1;
            for (uint32_t r = 0; r < ft.nRings; ++r) {
                const Ring& rg = gRings[ft.ring0 + r];
                const int32_t* p = gPts + 2 * rg.pt0;
                for (uint32_t k = 0; k + 1 < rg.n; ++k) {
                    int x0 = static_cast<int>(floorf(bx + p[2 * k + 1] * sx)), y0 = static_cast<int>(floorf(by + p[2 * k] * sy));
                    const int x1 = static_cast<int>(floorf(bx + p[2 * k + 3] * sx)), y1 = static_cast<int>(floorf(by + p[2 * k + 2] * sy));
                    if (std::max(x0, x1) < -width || std::min(x0, x1) > cols + width || std::max(y0, y1) < -width ||
                        std::min(y0, y1) > rows + width)
                        continue;
                    const int dx = abs(x1 - x0), dy = -abs(y1 - y0), stx = x0 < x1 ? 1 : -1, sty = y0 < y1 ? 1 : -1;
                    int e = dx + dy;
                    while (true) {
                        if (width == 1) put(x0, y0, ft.cls);
                        else
                            for (int a = 0; a < width; ++a)
                                for (int b = 0; b < width; ++b) put(x0 - hw + a, y0 - hw + b, ft.cls);
                        if (x0 == x1 && y0 == y1) break;
                        const int e2 = 2 * e;
                        if (e2 >= dy) { e += dy; x0 += stx; }
                        if (e2 <= dx) { e += dx; y0 += sty; }
                    }
                }
            }
        }
    }
    return true;
}

}  // namespace cta
