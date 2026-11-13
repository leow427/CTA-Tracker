// The pixel map of Chicago, shared by the dock and handheld mockups (design: docs/superpowers/specs/2026-11-12-pixel-map-
// design.md). The data is CHI_BASEMAP from 2026-11-12-chicago-basemap.js (tools/basemap_build.py), loaded first.
//
// raster() turns the map into a grid of cells, one class number per cell, for a frame and a pan; it does no drawing,
// so it ports to C++ as is. paint() colors a grid in one of the STYLES with fillRect runs, the one gfx2d call it needs.
// Classes: 0 land, 1 green, 2 sand, 3 water, 4 side street, 5 main street, 6 expressway (higher wins a cell).
// Map data (c) OpenStreetMap contributors, ODbL 1.0.
'use strict';
const PixelMap = (() => {
  const LAND = 0, GREEN = 1, SAND = 2, WATER = 3, SIDE = 4, MAIN = 5, EXPRESS = 6;
  const AREA = 1;
  // Real widths in meters (a street is drawn this wide when that's more than a cell, else one cell).
  const WIDTH_M = { [WATER]: 0, [SIDE]: 10, [MAIN]: 20, [EXPRESS]: 40 };

  // ---------- decoding (once) ----------
  let FEATS = null;
  function decode() {
    if (FEATS) return FEATS;
    const bin = atob(CHI_BASEMAP.data.replace(/\s+/g, '')), n = bin.length, b = new Uint8Array(n);
    for (let i = 0; i < n; i++) b[i] = bin.charCodeAt(i);
    let p = 4;
    if (String.fromCharCode(b[0], b[1], b[2], b[3]) !== 'CBM2') throw new Error('pixel map: bad data');
    const vint = () => { let v = 0, s = 1, c; do { c = b[p++]; v += (c & 0x7f) * s; s *= 128; } while (c & 0x80); return v % 2 ? -(v + 1) / 2 : v / 2; };
    // blocks of one class and kind; every point a delta from the one before it (tools/basemap_build.py, pack)
    const feats = [];
    let la = 0, lo = 0;
    for (let nb = vint(); nb > 0; nb--) {
      const cls = b[p++], kind = b[p++];
      for (let nf = vint(); nf > 0; nf--) {
        const nr = kind === AREA ? vint() : 1, rings = [];
        let la0 = Infinity, la1 = -Infinity, lo0 = Infinity, lo1 = -Infinity;
        for (let r = 0; r < nr; r++) {
          const np = vint(), ring = new Int32Array(np * 2);
          for (let k = 0; k < np; k++) {
            la += vint(); lo += vint(); ring[2 * k] = la; ring[2 * k + 1] = lo;
            if (la < la0) la0 = la; if (la > la1) la1 = la; if (lo < lo0) lo0 = lo; if (lo > lo1) lo1 = lo;
          }
          rings.push(ring);
        }
        feats.push({ cls, area: kind === AREA, rings, la0, la1, lo0, lo1 });
      }
    }
    return (FEATS = feats);
  }

  // ---------- rasterizing ----------
  // frame: { lat0, lon0, kmX, kmY, pxPerKm, cx, cy } (map_frame.h's MapFrame), pan: { x, y } in screen px.
  // area: { x, y, w, h } the screen rectangle to cover; cell: screen px per cell. opts.lod: 'all' | 'quiet'.
  // Cells are anchored to the map: cell (i, j) covers screen x from ox + i * cell (ox moves with the pan), and gi, gj
  // (i + gi0, j + gj0) number it the same way for as long as the frame doesn't change, so patterns don't crawl.
  function raster(frame, pan, area, cell, opts = {}) {
    const feats = decode(), [oLa, oLo] = CHI_BASEMAP.origin;
    const mod = (a, m) => ((a % m) + m) % m;
    const ox = area.x + mod(pan.x, cell) - cell, oy = area.y + mod(pan.y, cell) - cell;
    const cols = Math.ceil(area.w / cell) + 2, rows = Math.ceil(area.h / cell) + 2, grid = new Uint8Array(cols * rows);
    // a point in 1e-5 degrees from the origin -> cell coordinates (float)
    const sx = frame.kmX * frame.pxPerKm * 1e-5 / cell, sy = -frame.kmY * frame.pxPerKm * 1e-5 / cell;
    const bx = (frame.cx + (oLo - frame.lon0) * frame.kmX * frame.pxPerKm + pan.x - ox) / cell;
    const by = (frame.cy - (oLa - frame.lat0) * frame.kmY * frame.pxPerKm + pan.y - oy) / cell;
    // the box on screen, back in data units, to skip what's off it
    const lo0 = (0 - bx) / sx, lo1 = (cols - bx) / sx, la1 = (0 - by) / sy, la0 = (rows - by) / sy;
    const cellM = cell * 1000 / frame.pxPerKm;
    const showSide = opts.lod !== 'quiet' && cellM <= 33, showMain = cellM <= 250;
    const put = (i, j, c) => { if (i >= 0 && j >= 0 && i < cols && j < rows) { const k = j * cols + i; if (grid[k] < c) grid[k] = c; } };

    // lines as wide as their road reach this many cells past their centerline, so they count as on screen that much
    // earlier (no pop-in at the edges while panning)
    const reach = c => Math.max(1, Math.round((WIDTH_M[c] || 0) / cellM)) / 2 + 1;
    const ux = Math.abs(1 / sx), uy = Math.abs(1 / sy);
    for (const f of feats) {
      const m = f.area ? 0 : reach(f.cls);
      if (f.lo1 < lo0 - m * ux || f.lo0 > lo1 + m * ux || f.la1 < la0 - m * uy || f.la0 > la1 + m * uy) continue;
      if (f.cls === SIDE && !showSide) continue;
      if (f.cls === MAIN && !showMain) continue;
      if (f.area) {
        // an area whose box holds no cell center can't fill a cell (exact, and most small parks when zoomed out)
        const xa = bx + f.lo0 * sx, xb = bx + f.lo1 * sx, ya = by + f.la1 * sy, yb = by + f.la0 * sy;
        if (Math.ceil(xa - 0.5) > Math.ceil(xb - 0.5) - 1 || Math.ceil(ya - 0.5) > Math.ceil(yb - 0.5) - 1) continue;
        fillArea(f, f.cls);
      } else strokeLine(f, f.cls);
    }

    // Even-odd scanline fill at cell centers: a cell is in when its center is.
    function fillArea(f, c) {
      const edges = [];
      let jmin = Infinity, jmax = -Infinity;
      for (const r of f.rings) {
        const n = r.length / 2;
        for (let k = 0; k < n - 1; k++) {
          const x0 = bx + r[2 * k + 1] * sx, y0 = by + r[2 * k] * sy, x1 = bx + r[2 * k + 3] * sx, y1 = by + r[2 * k + 2] * sy;
          if (y0 === y1) continue;
          const ya = Math.min(y0, y1), yb = Math.max(y0, y1);
          if (yb < 0.5 || ya > rows) continue;
          edges.push(y0 < y1 ? [y0, y1, x0, (x1 - x0) / (y1 - y0)] : [y1, y0, x1, (x0 - x1) / (y0 - y1)]);
          jmin = Math.min(jmin, ya); jmax = Math.max(jmax, yb);
        }
      }
      if (!edges.length) return;
      edges.sort((a, b) => a[0] - b[0]);
      const j0 = Math.max(0, Math.ceil(jmin - 0.5)), j1 = Math.min(rows - 1, Math.floor(jmax - 0.5));
      let next = 0, active = [];
      const xs = [];
      for (let j = j0; j <= j1; j++) {
        const y = j + 0.5;
        while (next < edges.length && edges[next][0] <= y) active.push(edges[next++]);
        active = active.filter(e => e[1] > y);
        xs.length = 0;
        for (const e of active) if (e[0] <= y) xs.push(e[2] + (y - e[0]) * e[3]);
        xs.sort((a, b) => a - b);
        for (let k = 0; k + 1 < xs.length; k += 2) {
          const i0 = Math.max(0, Math.ceil(xs[k] - 0.5)), i1 = Math.min(cols - 1, Math.ceil(xs[k + 1] - 0.5) - 1);
          for (let i = i0; i <= i1; i++) put(i, j, c);
        }
      }
    }

    // A line of square brushes (Bresenham over cells), as wide as the road really is, at least one cell.
    function strokeLine(f, c) {
      const w = Math.max(1, Math.round((WIDTH_M[c] || 0) / cellM)), h = (w - 1) >> 1;
      for (const r of f.rings) {
        const n = r.length / 2;
        for (let k = 0; k < n - 1; k++) {
          let x0 = Math.floor(bx + r[2 * k + 1] * sx), y0 = Math.floor(by + r[2 * k] * sy);
          const x1 = Math.floor(bx + r[2 * k + 3] * sx), y1 = Math.floor(by + r[2 * k + 2] * sy);
          if (Math.max(x0, x1) < -w || Math.min(x0, x1) > cols + w || Math.max(y0, y1) < -w || Math.min(y0, y1) > rows + w) continue;
          const dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0), stx = x0 < x1 ? 1 : -1, sty = y0 < y1 ? 1 : -1;
          let e = dx + dy;
          for (;;) {
            if (w === 1) put(x0, y0, c); else for (let a = 0; a < w; a++) for (let d = 0; d < w; d++) put(x0 - h + a, y0 - h + d, c);
            if (x0 === x1 && y0 === y1) break;
            const e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += stx; } if (e2 <= dx) { e += dx; y0 += sty; }
          }
        }
      }
    }

    const gi0 = -Math.floor(pan.x / cell), gj0 = -Math.floor(pan.y / cell);
    return { grid, cols, rows, ox, oy, cell, gi0, gj0, cellM };
  }

  // ---------- styles ----------
  // pal: a color per class. shore: water cells beside land. dither: classes drawn as a checkerboard with a second color.
  // outline: classes drawn only along their edge, their inside in `fill`. Every color stays darker than the lines.
  const STYLES = {
    night: { name: 'Night', note: 'Black land, deep blue water, dim gray-blue streets.',
      pal: [0x000000, 0x0c2a1c, 0x2b2414, 0x0c1f45, 0x161d2c, 0x26324c, 0x3a4a72] },
    navy: { name: 'Navy', note: 'The app\'s navy Chicago, in pixels: navy land, darker water.',
      pal: [0x0b1a36, 0x10313a, 0x2c2c24, 0x050c1c, 0x1b2b4e, 0x263c69, 0x35538e] },
    bit8: { name: '8-bit', note: 'A game overworld: checkered water and parks, a bright shoreline.',
      pal: [0x000000, 0x123d1d, 0x4a3c1c, 0x0e2a66, 0x1b2233, 0x2c3a58, 0x4a5f94],
      dither: { [GREEN]: 0x0b2814, [WATER]: 0x0a1f4e }, shore: 0x3566c4 },
    blueprint: { name: 'Blueprint', note: 'Outlines only: the shore and park edges, thin streets.',
      pal: [0x000000, 0x1e5233, 0x5a4a22, 0x2a4f8f, 0x141b29, 0x222d45, 0x34436a],
      outline: { [GREEN]: 0x000000, [SAND]: 0x000000, [WATER]: 0x060f24 } },
    quiet: { name: 'Quiet', note: 'Water, parks and the big streets only; no side streets.', lod: 'quiet',
      pal: [0x000000, 0x0a2016, 0x221d12, 0x0a1838, 0x111725, 0x1a2333, 0x2a3654] },
  };

  // Calls fill(x, y, w, h, rgb) for runs of same-colored cells in each row, clipped to clip {x, y, w, h}.
  function paint(ras, style, fill, clip) {
    const { grid, cols, rows, ox, oy, cell, gi0, gj0 } = ras, s = STYLES[style] || STYLES.night;
    const at = (i, j) => (i < 0 || j < 0 || i >= cols || j >= rows ? -1 : grid[j * cols + i]);
    const colorAt = (i, j) => {
      const c = grid[j * cols + i];
      if (s.outline && s.outline[c] !== undefined) {
        const edge = [at(i - 1, j), at(i + 1, j), at(i, j - 1), at(i, j + 1)].some(n => n !== -1 && n !== c && n < SIDE);
        if (!edge) return s.outline[c];
      }
      if (s.shore && c === WATER && [at(i - 1, j), at(i + 1, j), at(i, j - 1), at(i, j + 1)].some(n => n !== -1 && n < WATER)) return s.shore;
      if (s.dither && s.dither[c] !== undefined && ((i + gi0 + j + gj0) & 1)) return s.dither[c];
      return s.pal[c];
    };
    const x0 = clip.x, y0 = clip.y, x1 = clip.x + clip.w, y1 = clip.y + clip.h;
    for (let j = 0; j < rows; j++) {
      const ty = oy + j * cell, by = Math.max(y0, ty), ey = Math.min(y1, ty + cell);
      if (ey <= by) continue;
      let i = 0;
      while (i < cols) {
        const rgb = colorAt(i, j);
        let k = i + 1;
        while (k < cols && colorAt(k, j) === rgb) k++;
        const bx = Math.max(x0, ox + i * cell), ex = Math.min(x1, ox + k * cell);
        if (ex > bx) fill(bx, by, ex - bx, ey - by, rgb);
        i = k;
      }
    }
  }

  // A small cache: the last grid, kept until the frame, pan, area, cell or level of detail changes.
  let last = null;
  function cached(frame, pan, area, cell, opts = {}) {
    const key = JSON.stringify([frame, pan, area, cell, opts.lod || 'all']);
    if (!last || last.key !== key) last = { key, ras: raster(frame, pan, area, cell, opts) };
    return last.ras;
  }

  return { raster, cached, paint, STYLES, LAND, GREEN, SAND, WATER, SIDE, MAIN, EXPRESS, credit: '© OpenStreetMap contributors' };
})();
