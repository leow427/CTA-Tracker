// Landmark checks for the pixel map: loads the committed docs/ideas/2026-11-12-chicago-basemap.js and
// 2026-11-12-pixel-map.js as the mockups do, rasterizes 10 m cells around known places, and checks what's there.
// The expressway medians are checked against CTA's station positions (data/stops.json), a separate source.
// Run from the repo root: node tools/test_basemap_landmarks.mjs
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const ctx = vm.createContext({ atob, console });
for (const f of ['2026-11-12-chicago-basemap.js', '2026-11-12-pixel-map.js'])
  vm.runInContext(fs.readFileSync(path.join(ROOT, 'docs/ideas', f), 'utf8'), ctx, { filename: f });
const PixelMap = vm.runInContext('PixelMap', ctx);
const NAMES = ['land', 'green', 'sand', 'water', 'side street', 'main street', 'expressway'];

// The classes within r cells (10 m each) of a point.
function around(lat, lon, r) {
  const cell = 3, half = 45, frame = { lat0: lat, lon0: lon, kmX: 111.32 * Math.cos(lat * Math.PI / 180), kmY: 111.32, pxPerKm: 300, cx: half, cy: half };
  const ras = PixelMap.raster(frame, { x: 0, y: 0 }, { x: 0, y: 0, w: 2 * half, h: 2 * half }, cell);
  const i0 = Math.floor((half - ras.ox) / cell), j0 = Math.floor((half - ras.oy) / cell), seen = new Set();
  for (let j = j0 - r; j <= j0 + r; j++) for (let i = i0 - r; i <= i0 + r; i++) seen.add(ras.grid[j * ras.cols + i]);
  return seen;
}

const stops = JSON.parse(fs.readFileSync(path.join(ROOT, 'data/stops.json'), 'utf8'));
const station = name => { const s = stops.find(s => s.kind === 'station' && s.name === name); if (!s) throw new Error(`no station ${name}`); return [s.lat, s.lon]; };

const W = PixelMap.WATER, G = PixelMap.GREEN, SA = PixelMap.SAND, EX = PixelMap.EXPRESS, STREETS = [PixelMap.SIDE, PixelMap.MAIN, EX];
const checks = [
  // [what, lat, lon, radius in cells, classes, one of which must be there]
  ['Lake Michigan off the Loop', 41.890, -87.580, 0, [W]],
  ['Lake Michigan off Rogers Park', 42.010, -87.640, 0, [W]],
  ['Chicago River beside the State St bridge (20 m wide)', 41.8873, -87.6280, 2, [W]],
  ['Chicago River at Michigan Ave', 41.8885, -87.6225, 1, [W]],
  ['Grant Park, Butler Field', 41.8800, -87.6190, 0, [G]],
  ['Grant Park, Hutchinson Field', 41.8705, -87.6190, 0, [G]],
  ['Lincoln Park, by the zoo', 41.9205, -87.6335, 0, [G]],
  ['Humboldt Park', 41.9015, -87.7045, 0, [G]],
  ['Graceland Cemetery', 41.9565, -87.6595, 0, [G]],
  ['Montrose Beach', 41.9655, -87.6370, 3, [SA]],
  ['Clark & Lake', 41.88573, -87.63093, 1, STREETS],
  ['Belmont & Sheffield (the bus stop pole, at the curb)', 41.93975, -87.65445, 2, STREETS],
  ['the Loop is land, not water', 41.8800, -87.6300, 0, [0, ...STREETS]],
  // The Blue Line in the Kennedy and the Eisenhower, the Red Line in the Dan Ryan: CTA's stations sit on the expressway.
  ...['Addison', 'Irving Park', 'Montrose', 'Jefferson Park Transit Center'].map(n => [`${n} (Blue) in the Kennedy`, n]),
  ...['Kedzie-Homan', 'Pulaski', 'Cicero', 'Austin'].map(n => [`${n} (Blue) in the Eisenhower`, n]),
  ...['Sox-35th', '47th', 'Garfield', '63rd', '69th', '79th', '87th'].map(n => [`${n} (Red) in the Dan Ryan`, n]),
];

let failed = 0;
for (const c of checks) {
  let [what, lat, lon, r, want] = c;
  if (typeof lat === 'string') {
    // a station by name; for names shared by several lines, the one nearest the expressway counts
    const cands = stops.filter(s => s.kind === 'station' && s.name.startsWith(lat));
    const hit = cands.find(s => around(s.lat, s.lon, 3).has(EX));
    [lat, lon] = hit ? [hit.lat, hit.lon] : station(lat); r = 3; want = [EX];
  }
  const got = around(lat, lon, r), ok = want.some(k => got.has(k));
  if (!ok) failed++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}: ${[...got].map(k => NAMES[k]).join(', ')}`);
}
console.log(failed ? `${failed} of ${checks.length} failed` : `all ${checks.length} passed`);
process.exit(failed ? 1 : 0);
