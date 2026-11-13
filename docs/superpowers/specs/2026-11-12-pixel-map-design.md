# Pixel map of Chicago for the dock and handheld mockups

- **Date:** 2026-11-12
- **Owner:** Leo
- **Status:** Built in the mockups (2026-11-12). Leo picked Quiet, now on the dock (2026-11-13)
- **Relates to:** the dock mockup `docs/ideas/2026-11-11-dock-ui-mockup.html` (its map page; picture
  `docs/ideas/2026-11-11-dock-ui-map.png`), the handheld mockup `docs/ideas/2026-11-04-handheld-ui-mockup.html` (its
  stretch-goal map page), and the main spec `2026-11-03-cta-tracker-design.md` (sections 7 and 8)

## 1. Why

Both mockups have a map page you reach by swiping left. Each shows the L lines, stops and pixel trains, but on plain
black. With nothing around them, the lines float: you can't tell where the lake is, where the river runs, or which
street a stop is on. It looks unfinished.

The fix is a real map of Chicago behind the lines, drawn in the mockups' chunky pixel look (the same look as the map's
trains, pins and block-drawn tracks), and accurate: everything sits where it really is.

**Success:** at a glance each map page reads as Chicago (the lake, the river, the parks, the street grid). Every L line
runs along its real street, structure or expressway median, and Leo approves the look from exported pictures.

## 2. Decisions so far (Leo, 2026-11-12)

- **Look:** the mockups' own chunky pixel style, not the iPhone app's smooth navy map.
- **Approach:** store the map as lines and shapes, and draw them as big pixels (section 5).
- **Lines that share track are not drawn side by side.** Every line is drawn on its real track. Where lines share a
  track they overlap, and one of them shows on top. Today's sideways nudges for Brown and Purple (`OFFSET` in both
  mockups) go away.
- **The map stays its own page,** reached by swiping left as it is today. It is not a background behind the stop cards.
- **Mockups only.** The work is in the HTML mockups and a laptop tool. No firmware changes and nothing is flashed.
  Porting it to the dock and handheld firmware comes later, as its own piece of work.
- **Several styles to choose from.** Instead of one palette approved from pictures, build 3–5 stylings around the
  look the mockups already have, so Leo can pick one (section 8).

## 3. What the user sees

Each map page keeps everything it has today, in the same places. Under it, Chicago is drawn in big square pixels:

- **Water:** Lake Michigan, the river and its branches, the canals, the lagoons and the harbors.
- **Green space:** parks, cemeteries, golf courses and forest preserves.
- **Beaches,** in a dim sand tone.
- **Streets in three classes:** expressways (the brightest), main streets, and side streets (the faintest).
- **Land** stays black, as today.

Pixel size follows each mockup's existing art:

- **Dock:** 3 × 3 screen pixels. Its map trains are drawn at 3× and its pins use 3 px blocks.
- **Handheld:** 2 × 2. Its map is already drawn at half resolution and doubled.

As the dock map zooms out, side streets drop away first, then main streets, so it never turns into a solid block.
Expressways, water and the bigger parks always show.

```
 ┌────────────────────────────┐
 │ Trackie   [map]  7:42 41°  │  dock status strip (unchanged)
 ├────────────────────────────┤
 │▒▒ ░ ░ ░ ║ ░ ░ ░ ░ ▓▓▓▓▓▓▓▓▓│  ▓ lake   ▒ park   ░ side streets
 │▒▒─░─░─░─║─░─░─░─░─▓▓▓▓▓▓▓▓▓│  ─ main street   ║ L line, on its real track
 │ ░ ░ ░ ░ ║ ░ ░ ①Belmont 3m ▓│  pins and labels (unchanged)
 │ ░ ░ ░ ░ ║ ░ ░ ░ ░ ░▒▒▒▒▓▓▓▓│
 └────────────────────────────┘
```

A small, dim "© OpenStreetMap contributors" sits on each map page (the data's license requires it).

## 4. Goals, non-goals, anti-goals

**Goals**
- A pixel map of the whole CTA service area behind both mockups' map pages, from real geographic data.
- Accurate to within one map pixel at every zoom (section 6.5).
- Panning stays as smooth as it is today in the browser.
- Ports to the firmware later without a rethink. The dock mockup is drawn only with `gfx2d`-style calls (its README),
  and the map keeps that rule. The data is encoded the way the device would carry it.

**Non-goals (for now)**
- Any firmware change or flashing. `firmware/dock/src/dock_ui.cpp` keeps its black map and its Brown and Purple
  offsets until the port.
- Street names or any other labels.
- Buildings, Metra and freight railroads, airport runways, alleys, footpaths.
- Changing `data/train_shapes.json`, which the iPhone app and the firmware also use.

**Anti-goals: these make it a failure even if it works**
- The map competes with the lines, the trains or the pins. The picked lines, trains and pins must stay the
  brightest things on screen.
- Anything drawn in the wrong place, or a smooth, blurry or anti-aliased look.
- The map page gets slower to open or to pan.

## 5. Approaches

1. **Store the map as lines and shapes, then draw them as big pixels (picked).** A laptop tool turns OpenStreetMap
   data into compact geometry. The map page draws it onto a coarse grid of cells, one color per cell. It works at
   any zoom, stays accurate at every zoom, and stays small.
2. **Pre-drawn pixel pictures (tiles).** The simplest to draw, but at the zoom where side streets show the CTA area
   runs to several MB. Making it fit means a few fixed zooms and no close-up streets. Rejected.
3. **Map tiles from an online service.** Needs a service and a key, fails offline, and isn't pixelated. Rejected.

## 6. Design

### 6.1 The data (laptop tool)

- **Source:** OpenStreetMap, downloaded once by bounding box from the public Overpass API, split into a few
  requests and cached in a git-ignored folder. Only the processed output is committed. The snapshot date is recorded
  in the output.
- **Coverage:** the box around every CTA stop in `data/stops.json` (41.644–42.073 N, 87.904–87.526 W, about
  48 × 31 km) plus a 2 km margin on each side. Past the edge the map is plain black.
- **What's kept, by class:**

| Class | OpenStreetMap features |
|---|---|
| Water | Lake Michigan (built from `natural=coastline`, closed against the coverage box), `natural=water` areas, `waterway=river` / `canal` lines |
| Green | `leisure=park`, `leisure=golf_course`, `leisure=nature_reserve`, `landuse=cemetery`, `landuse=forest`, `natural=wood` |
| Sand | `natural=beach` |
| Expressway | `highway=motorway`, `trunk` (Lake Shore Drive among them) |
| Main street | `highway=primary`, `secondary`, `tertiary` |
| Side street | `highway=residential`, `unclassified`, `living_street` |

- **Left out:** roads in tunnels or below ground level (Lower Wacker, for example), ramps (`*_link`), alleys and
  other `service` roads, footpaths, cycleways, and anything private.
- **Simplified** to within 2 m of the real geometry, well under the smallest cell (about 4 m on the dock).
- **The L and the 22 at the same precision:** the mockups' L shapes come from `data/train_shapes.json`, which is
  simplified to 5 m. That can put a line 1–2 cells off the map at the dock's closest zoom. The dock mockup also holds
  only three clipped lines, and a hand-made line for the 22. So the tool also writes, from the same CTA GTFS feed
  (`data/gtfs/google_transit.zip`, read the way `tools/gtfs_build.py` reads it):
  - every L line's shape at 2 m
  - the 22's southbound shape

  Both mockups draw those instead. `data/train_shapes.json` stays as it is.
- **Output:** one committed script beside the mockups, `docs/ideas/2026-11-12-chicago-basemap.js`. Both mockups load
  it with a `<script src>` tag, which works when the HTML is opened straight from disk. It holds the data, its
  credit and license note, and the rasterizer (section 6.2). The data is quantized and delta-coded in a layout the
  firmware could carry as is, with a spatial index so a frame skips what's off screen. The same input must always
  produce byte-identical output.
- **Size:** the encoded data must be at most 700 KB, which is what the dock could spare later.
  - The tool builds and measures it first.
  - If it's over, side streets are kept only within 1 km of a CTA stop. That still covers all of Chicago and drops
    the unserved suburbs and the corner of Indiana.
  - Geometry is never coarsened to save space.
  - If it's still over, stop and tell Leo.
- **Tool:** `tools/basemap_build.py` (download, build and write), standard library only, with unit tests in
  `tools/test_basemap_build.py` on a small fixture, as `tools/test_map_build.py` does.

### 6.2 The rasterizer (shared by both mockups)

- **The function:** a pure function in the shared script. It takes a map frame (center, km per degree, pixels per km,
  pan), a cell size in screen pixels, and a grid to fill. It writes one class number per cell. It does no drawing
  of its own, so it ports to C++ as is.
- **Anchored to the map:** cells move with the pan, so panning slides the pixels with the city instead of making
  them crawl.
- **Raster rule:** a feature fills the cells it covers, and a cell's color is the topmost class in it. Order, bottom
  to top: land, green, sand, water, side street, main street, expressway.
- **Street width:** streets are drawn at their real width when that is more than one cell (side street about 10 m,
  main street about 20 m, expressway about 40 m), and at least one cell wide otherwise.
- **Level of detail:** a street class only draws when its typical spacing is at least about 3 cells.
  - Chicago's side streets are about 100 m apart one way and 200 m the other, so they show when a cell is at most
    about 33 m.
  - Main streets show when a cell is at most about 250 m, and expressways always show.
  - Water and green areas always draw, so an area under one cell simply disappears.

### 6.3 The dock mockup's map page

- **Grid:** the map area (screen rows 80–799, 480 × 720 px) is 160 × 240 cells of 3 × 3 px. They're painted with
  `fillRect` runs of the same color, a `gfx2d`-style call, under the track casings. Nothing else on the page moves.
- **Lines:** all eight L lines come from the shared script, not just today's three. Lines nobody picked are dimmed as
  today. With no side-by-side, the order is: lines nobody picked, then picked lines, with the line of the first
  picked section last, so it's on top. The zone stations, trains and pins use their real positions, with no offset.
- **Side streets at the default picks:** the default picks (Red at Belmont, Brown at Diversey, the 22 at Clark &
  Belmont) frame to about 28 m cells. That must show the side-street grid, which is most of what makes the map read
  as a city.
- **Caching:** the grid is redrawn only when the frame or the pan changes, so dragging stays smooth.

### 6.4 The handheld mockup's map page

- **Grid:** the map is already drawn into a 233 × 233 canvas and doubled, at a fixed 120 px/km around the phone's
  fix. That makes a cell about 17 m, so the side streets show. The grid is painted into that small canvas first,
  under the casings.
- **Lines:** all L lines from the shared script, with no offsets. Where lines overlap, the drawing order is the
  builder's call.
- **Bounds:** the round screen's edge clips the map as it does today.

### 6.5 Palette

- **Shared:** both mockups use one palette. Land stays black, water reads blue, green space dark green, sand dim tan,
  and streets a cool gray-blue that gets brighter by class.
- **Brightness:** every basemap color is dark enough that every line's on-black color, the trains and the pins keep
  clearly more contrast than any basemap color.
- **Lines nobody picked** (dock, drawn at 30%) must still read as L lines, not streets: their hue and dark casing set
  them apart. If a picture shows them getting lost in the streets, raise their dim level rather than flattening the
  streets into one shade.
- **Approval:** Leo approves the exact values from the exported pictures.

### 6.6 Accuracy

"Accurate" means every feature lands in the cell that contains its real position, at every zoom. That is an error of
at most one cell, coming only from the cell size. It's checked four ways:

1. **Tool tests:** the class mapping, the dropped tunnels and ramps, the lake polygon closed on the correct side,
   the 2 m simplification, and byte-identical output on a fixture.
2. **Landmark tests:** a Node script loads the committed shared script, rasterizes a small frame around each of about
   a dozen known points, and checks the class at the point. The coordinates are taken from OpenStreetMap:
   - a point in the lake
   - the river at the Loop
   - Grant Park, Lincoln Park, Humboldt Park and Graceland Cemetery
   - Oak Street Beach
   - a point on the street beside Belmont, Clark/Lake and Logan Square stations
3. **The L against the map:** the L comes from CTA's GTFS, a separate source. Exported pictures at a close, middle and
   system-wide dock zoom, plus the handheld page, must show:
   - the north side lines over their real alignment
   - the Loop over its streets
   - the Blue Line in the middle of the Kennedy and the Eisenhower
   - the Red Line in the middle of the Dan Ryan
   - the 22 on Clark
4. **Side by side:** the same boxes compared against openstreetmap.org's own map.

### 6.7 Pictures and docs

- **Pictures:** re-export `docs/ideas/2026-11-11-dock-ui-map.png` from the mockup (`#state=N&solo`). Add pictures of a
  zoomed-out dock map and the handheld map page.
- **Ideas README:** `docs/ideas/README.md` gets the new shared script, the OpenStreetMap credit and license, the
  snapshot date, and a note that the dock firmware hasn't been ported to the new map yet.

## 7. Left to the builder

- **Data:** the exact encoding and spatial index (within 700 KB, deterministic, portable to C++).
- **Download:** how the request is split to respect the Overpass API's limits, and where the raw cache lives
  (git-ignored).
- **Rasterizer:** the line and polygon-fill algorithms, and how it caches between frames.
- **Tuning:** the exact level-of-detail thresholds and street widths, tuned from pictures.
- **Handheld:** the drawing order of overlapping lines on its map.
- **Credit:** the exact placement and font of the OpenStreetMap credit on each page. Keep it small and dim, and clear
  of the recenter button.

## 8. As built (2026-11-12)

What changed from the plan above while building, and where things stand.

### 8.1 Data source and size

- **Source:** the public Overpass servers were at capacity, so the data comes from BBBike's ready-made Chicago
  extract instead. It's the same OpenStreetMap data as one file: OSM XML, gzipped, about 235 MB, snapshot
  2026-11-08, covering 41.643–42.093 N and 88.006–87.398 W.
  - `tools/basemap_fetch.py` downloads it into the git-ignored `data/osm/`. Its MD5 matched BBBike's `CHECKSUM.txt`.
  - `tools/basemap_build.py` reads it in three standard-library passes (relations, ways, nodes) and caches the
    parsed layers next to it.
- **Coverage:** the box is 41.643–42.093 N, 87.928–87.502 W, clipped to the extract. The southern margin past the
  last stop is therefore about 0.1 km, not 2 km.
- **Lake Michigan:** it isn't coastline in OSM. It's one huge `natural=water` multipolygon. The tool joins the
  outer ways that are in the extract into the shore, turns it to run south to north, and closes it far out east.
  The lake's inner rings (breakwaters, islands) go in as holes.
- **Size:** the first packing was 1.2 MB. Clipping side streets to within 1 km of a stop only brought it to
  1.03 MB, so the packing was made tighter instead, with nothing coarsened (format `CBM2`):
  - Road pieces are joined into strokes that run straight on through intersections (under 35°), then simplified
    again at the same 2 m.
  - Features are stored in blocks of one class, in 1 km tile order.
  - Every point is a delta from the one before it.

  The result is **579 KB** with every side street in the area kept: 18,325 side, 1,887 main and 69 expressway
  strokes, 3,909 green, 2,149 water and 72 sand features.
- **Files:** the output is split in two, data and code:
  - `docs/ideas/2026-11-12-chicago-basemap.js` (generated): the map, every L line at 2 m, and the 22's southbound
    shape.
  - `docs/ideas/2026-11-12-pixel-map.js` (hand-written): decoding, `raster()`, `paint()` and the styles.

### 8.2 The styles

Five styles, in `PixelMap.STYLES`, each switchable in both mockups' "Map style" control. All keep land black or
navy and stay darker than the lines:

| Style | What it looks like |
|---|---|
| Night | Black land, deep blue water, dark green parks, dim gray-blue streets brightening by class. The default. |
| Navy | The app's navy Chicago in pixels: navy land, darker water, blue streets. |
| 8-bit | A game overworld: checkered water and parks (anchored to the map, so they don't crawl), a bright shoreline. |
| Blueprint | Outlines only: the shore and park edges, thin streets, dark insides. |
| Quiet | Water, parks and the main streets and expressways; never side streets. |

`docs/ideas/2026-11-12-pixel-map-styles.png` shows all five side by side: the dock framed on the picks, the same
picks zoomed out, and the handheld map.

### 8.3 Other changes in the mockups

- **Dimmed lines:** in the dock mockup, lines nobody picked are now drawn solid, 40% of the way from the casing to
  their color.
  - The old 30% alpha stacked up where `seg`'s blocks overlap and came out nearly full strength. Now that every L
    line is drawn, that showed.
  - 40% keeps them readable as L colors against the streets.
- **Preview zoom:** the dock mockup has a preview-only "Map zoom" (framed on the picks, wider, whole city), plus
  `#...&mapstyle=…&zoom=…&pan=x,y` in the URL for picture exports. The device always frames the picks.
- **Handheld exports:** the handheld mockup takes `#map&mapstyle=…&solo` for picture exports.
- **Handheld charset:** its page now declares UTF-8. Served over HTTP, its "·" and "×" showed as "Â·" before.
- **Pictures:** `docs/ideas/2026-11-11-dock-ui-map.png` is re-exported in the Night style.

### 8.4 Checks

- **Tool tests:** `python3 -m unittest tools/test_basemap_build.py` has 16 tests: the classes, hidden roads and
  culverts, ring and lake building, closing areas cut by the extract's edge, dropping inner rings that lost their
  outer, strokes, the 2 m simplification, the XML reader, and a byte-exact pack round trip.
- **Landmarks:** `node tools/test_basemap_landmarks.mjs` passes 28 of 28.
  - It covers the lake, the river at the Loop, Grant Park, Lincoln Park, Humboldt Park, Graceland, Montrose Beach,
    two street corners, and the Loop as land.
  - It also checks CTA's own station positions: the Blue Line in the Kennedy (4) and the Eisenhower (4), and the
    Red Line in the Dan Ryan (7) all land on the expressway.
  - Three first guesses were wrong and were fixed, not the map: a point on the State St bridge, Belmont (Blue),
    which is in the Milwaukee subway, and Cermak-Chinatown, which is 620 m north of the Dan Ryan.
- **Speed:** rasterizing and painting take about 9 ms per pan step framed on the picks, and about 44 ms for the
  whole-city preview, in Node on a Mac.
- **Pictures:** checked by eye. The lakefront, harbors, Lincoln Park, Graceland with the 22 on Clark along its west
  edge, the North Branch, Navy Pier and Lake Shore Drive's two carriageways all sit where they should.

### 8.5 Fixed after a fresh review

A fresh reviewer read the finished work cold and found the following, all now fixed:

- **Handheld pan:** the map was panned in half pixels, so every cell smeared after a drag, and a recenter
  re-rasterized for about a minute while the glide shrank toward zero. Now:
  - the pan snaps to whole map pixels
  - the glide stops under half a pixel
  - the lines sit on the same half-pixel grid as the cells
- **Handheld sheet:** clicking a "Map · style" thumbnail didn't switch the style.
- **Rivers at the edge:** the Sanitary and Ship Canal and the Des Plaines are cut by the extract's edge, so their
  areas were dropped and only a one-cell centerline showed. Such areas are now closed along the edge (seven of
  them).
- **Holes drawn as areas:** a relation whose outer ring was dropped kept its inner rings, which then filled as if
  they were the area. It is now skipped (one case).
- **Culverts:** culverted and tunnelled waterways were drawn as surface water. They are now left out.
- **Wide roads at the edge:** they could pop in at the screen edge while panning. The cull now counts their width.

Kept as designed: streets tagged `tunnel=building_passage` stay hidden, under the "roads in tunnels are left out"
rule. That is about 5.6 km in all, for example 293 m of W Washington St.

## 9. On the dock (2026-11-13)

Leo picked **Quiet**, and the dock firmware draws it. The handheld's map screen is still a stretch goal.

### 9.1 Data and code

- **Data:** `tools/basemap_build.py` also writes `firmware/lib/cta/src/basemap_data.cpp`. It's the same packing
  without side streets (Quiet never draws them): 379 KB of flash.
  - The mockups' script comes out byte-identical.
  - The dock firmware is 2.74 MB of its 3.34 MB partition (81.9%).
- **Rasterizer:** `firmware/lib/cta/src/basemap.h/.cpp` is the rasterizer ported from `2026-11-12-pixel-map.js`.
  - It unpacks the map once into PSRAM. With the map unpacked, 1.87 MB of PSRAM is free.
  - `basemapRaster` fills a cell grid for a `MapFrame` and a pan, anchored to the map.
- **Drawing call:** `Canvas::cells` in `gfx2d` draws a cell grid in a palette, as runs of `fill565`. It is recorded
  as one call that references the grid (not copied), with a version number standing in for its cells in the bands'
  hashes. That keeps the map inside the recording's 4,096 calls.

### 9.2 Changes in `dock_ui.cpp`

- **Basemap:** the grid covers the map area plus 240 px each way and is made at an anchor pan, so a drag only moves
  it. It's rasterized again when the picks' frame changes or a pan nears the margin.
- **Lines:** the Brown and Purple offsets are gone, so every line runs on its real track. Unpicked lines are drawn
  solid, 40% of the way from the casing to their color.
- **Credit:** a pixel (c) with "OpenStreetMap contributors" at the map's bottom left. The 12 px font has no "©".
- **L tracks:** they stay at `train_map.cpp`'s 5 m. That's under a fifth of a cell at the usual framing (about
  28 m cells).

### 9.3 Checks

- **Native test:** `firmware/native/test/test_basemap` covers the landmarks, the stations in the expressway medians,
  and cells staying with the map as it pans. All native tests pass.
- **Preview:** `firmware/dock/tools/preview.cpp` renders the map the same as the mockup's Quiet style. Its recorded,
  band-by-band frames match the plain picture on every screen, including the map and a panned map.
- **On the board:** the map frame took 29 ms (6 ms of it drawing), checked with `frame_grab`.
