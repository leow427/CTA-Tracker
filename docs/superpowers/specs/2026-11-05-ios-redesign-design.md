# CTA Tracker iPhone app: redesign

- **Date:** 2026-11-05
- **Owner:** Leo
- **Status:** Draft
- **Amends:** `2026-11-04-ios-app-design.md` (the app spec)

The first build works but looks basic. This redesign gives the app character: a 3D "diorama" map of Chicago on Mapbox, and bold line-colored cards that scroll straight over the map, like the Transit app, instead of sitting in a system sheet. Data, polling, cards rules, favorites, BLE, settings, and errors stay as the app spec says. Where this doc and the app spec disagree, this doc wins.

Reference: the scratch mockup in `docs/ideas/2026-11-05-ios-diorama-mockup.html`. Its map look carries over. Its bottom panel and white cards do not.

## 0. Revision: night navy (2026-11-05, after the first build)

Leo's verdict on the first build: it still looked like the old app, and the Liquid Glass had to go. The look below replaces sections 3, 4 (layout), 5, and 6 (light) wherever they disagree. Mechanics stay: the list over the map, pass-through touches, search pinning, the zoom into detail.

- **Identity.** Night navy (`0x0B1A36`) for the map and every panel, with headlight yellow (`0xFFD95A`, the pixel pet's yellow) as the signature color. The pixel pet, live glyph, and alert glyph carry the character. The app is always dark.
- **No Liquid Glass.** Controls and chips are solid raised navy (`0x17294F`) with a faint rim.
- **Map.** Mapbox Standard in daylight with a navy palette for land, roads, water, and buildings, still in 3D, so the L lines glow. It no longer follows the sun, and the on-phone sun times are removed.
- **Main screen.** At top left is the pet in a cream circle with a gear badge (Settings), with the "Near place" pill tucked under it. Recenter is a round button that turns yellow while following. A headlight-yellow search bar sits on the navy panel's rounded top edge, and the All · Trains · Buses menu is at its end. The list is rows on navy, with no cards: a huge route number in its line color, "→ destination" in color ("West to Downtown" for buses), the stop, and big minutes on the right. Rows are split by hairlines.
- **Line colors on navy.** Big type is lifted toward white until it reaches 3:1, and smaller text until it reaches 4.5:1 (`LineColors.onNavy`). Buses use a bright blue (`0x2F7BFF`).
- **Stop detail.** The line's huge number sits over the map, with a red × (close) and ★ down the right. The navy page has "→ destination", big departure tiles (the next one solid in the line color, the later ones tinted navy), the chips (Delayed, freshness), the alerts, and the stop's other lines on a timeline in their colors.

## 1. Decisions

| Topic | Decision |
|---|---|
| Minimum iOS | iOS 26, for Liquid Glass (`glassEffect`) and current SwiftUI APIs. |
| Map | Mapbox Maps SDK v11 (Swift Package), SwiftUI `Map`, Standard style. Replaces MapKit. |
| Map look | Diorama: a muted Chicago with 3D buildings and a tilted camera. Light follows the real sun (dawn, day, dusk, night). The L lines and the cards carry the color. |
| Main screen | No sheet. A full-screen map, glass controls on top, and one scrolling list of cards whose top starts partway down the screen and scrolls up over the map. |
| Cards | Thick tiles filled with the line color, with a visible edge, a highlight, and a shadow on the map. They sink when pressed. |
| Stop detail | A full-screen page over the map, opened from a card. A row of big departure tiles, a chip row, then the rest of the stop. |
| Type | SF Pro Rounded. Heavy for route numbers and minutes. The pixel font is no longer used for minutes. |
| Pixel art | The pet, vehicle sprites on the map, and the small glyphs (live, alert, dock, handheld). |
| Secrets | The Mapbox public token goes in the gitignored `Secrets.plist` as `MAPBOX_TOKEN`, next to the CTA keys. |

## 2. Main screen

Replaces app spec sections 3 (sheet parts) and 4.1.

```
Map (full screen, always there)
 ├─ top, glass: gear · "Near place" capsule with pet · recenter
 └─ card list (one ScrollView over the map)
     ├─ search capsule · All · Trains · Buses · line filter chip
     ├─ ★ favorites (≤3)
     └─ nearby cards
 Pushed over the map: stop detail (section 4)
 Presented as sheets: Settings, setup (unchanged)
```

- **Resting position:** the list's top sits about 55% down the screen, so the first card and part of the second show under an open map.
- **Scrolling up** slides the cards over the map. Once the search capsule reaches the top controls, it pins there, and a solid background (Paper in light, Ink in dark) fades in behind the cards so they stay readable.
- **Scrolling back down** returns to the resting position. The list can't be dragged below it.
- **The map stays usable** in the open area above the list. The list's empty top area passes touches through to the map. This is the main technical risk, so the first checkpoint proves it.
- **Map framing:** the map's camera padding matches the list's resting top, so the user's dot and the map center sit in the visible part.
- **Panning, recenter, the "Near" label, the line filter, and first launch** work as in the app spec. Line taps (iOS milestone 4) will use Mapbox's layer tap handler instead of the nearest-line search.
- **Refresh:** pull down at the resting position refreshes, as before.

## 3. Cards

Replaces the look in app spec 4.3. Content and rules are unchanged.

- **Tile:** filled with the line color. Buses use one shared color, Ink (section 5). 16 pt side margins, 12 pt gaps, 22 pt continuous corners.
- **Depth:**
  - a 4 pt bottom edge in the line color darkened about 25%, so the tile reads as a physical plate;
  - a 1 pt top highlight (white at about 25%);
  - a tight contact shadow plus a soft, wide shadow onto the map.
- **Press:** the tile sinks 3 pt onto its edge on a spring, with a light haptic, then opens stop detail.
- **Layout:**
  - **Left:** the big route number ("66") or line name ("Blue"), rounded heavy, with the alert glyph beside it when an alert is active. Then "→ Navy Pier" and "Chicago & Damen · 2 min walk".
  - **Right:** big minutes, rounded heavy, with the pixel live glyph. "min" underneath. Due, Sched, and clock times as in the app spec.
  - **Below:** later departures in one small line ("also 22, 29").
- **Chips** sit on the tile in white at about 20% with white text: Delayed and the alert. The Delayed chip uses amber on any tile where that reads better.
- **Text color:** white, or dark text on Yellow (and any color where white fails 4.5:1). The contrast test now also covers Ink.

## 4. Stop detail

Replaces the look in app spec 5.1. Contents, grouping, and live tracking (5.2) are unchanged.

- **Opening:** the card's tile grows into the detail header (a zoom transition). The map flies to the line, dims the other lines, and shows its vehicles live.
- **Navigation:** a `NavigationStack` with a clear background over the map, so back is the system back button and edge swipe.
- **Layout, top to bottom:**
  1. The open map, about the top 40%, framing the stop and the next vehicles.
  2. The big route number or line name, and "→ destination".
  3. A row of departure tiles that scrolls sideways: the next arrival largest and brightest, the later ones in a darker shade, each with minutes and the live glyph or a Sched label.
  4. A chip row: Delayed, ⚠ alert (tap to expand the text), ★, and "updated 12 s ago".
  5. "Also at this stop": the other groups as compact tiles. Tapping one makes it the top group and points the live map at its line.
- **Vehicles:** pixel sprites as Mapbox view annotations, never rotated, with a separate heading arrow (app spec 4.6). They glide to each new position over about 1 s.

## 5. Visual design

Replaces app spec 4.6 where they differ. Haptics, accessibility, and Reduce Motion rules carry over.

- **Color:** more of it, but each color has one job.
  - The 8 CTA line colors fill train cards, tiles, and lines.
  - **Ink** `#1C2A4A`: bus tiles, dark backgrounds, and dark text.
  - **Paper** `#F6F2EA`: the light background behind the scrolled-up list.
  - **Amber** `#FFB000`: Delayed.
  - **Signal green** `#34C759`: the live glyph.
  - System blue is still off (`.tint` stays neutral).
- **Depth:** from light, glass, edges, and shadows, not glows or gradients on text. Tiles use the edge, highlight, and shadow in section 3.
- **Glass:** the top controls, search capsule, and switch use Liquid Glass. Reduce Transparency swaps them for solid Paper or Ink.
- **Type:** SF Pro Rounded throughout, following Dynamic Type. Route numbers and minutes are heavy. Destinations are bold, secondary lines are medium.
- **Pixel art:** whole-number scales, nearest-neighbor, never rotated or smoothed. The pet, vehicles, and glyphs only.
- **Motion:** springs as before. The tile press, the tile-to-header zoom, and the background fade are the only new motion. Pixel sprites still step at 4 fps.

## 6. Map

- **Style:** Mapbox Standard with a muted theme (`faded` or `monochrome`, picked on the phone), 3D buildings on, POI labels off, pitch about 55°.
- **Light:** the preset follows the sun: dawn and dusk within 45 minutes of sunrise and sunset, night after dark. Sunrise and sunset are computed on the phone for the map center (NOAA's sunrise equation), so the light needs no network. Where the sun doesn't rise or set that day, it uses day.
- **L lines:** a GeoJSON source from the bundled `train_shapes.json`, one line layer with a white casing, in the line colors. They stay bright at night (emissive strength 1) and show through buildings at reduced opacity where hidden.
- **Station dots, bus pins, and vehicles** keep the app spec's rules, drawn as Mapbox layers (dots, pins) and view annotations (vehicles).
- **Attribution:** the Mapbox logo and info button must stay visible. They sit at the bottom of the open map area, above the list's resting top.
- **Reverse geocoding** for the "Near" label stays on `CLGeocoder`. It's deprecated in iOS 26, but its replacement, `MKReverseGeocodingRequest`, gives no neighborhood or street name, which the label needs. MapKit is no longer used.

## 7. Code changes

- **Replaced:** `MapScreen` (Mapbox map and the overlay list instead of the sheet), `RouteSheet` (becomes the list over the map), `RouteCard` (tile look), `StopDetail` (new layout), and `Theme` (colors, rounded type, tile style).
- **New:** a small `SunTimes` value in CTAKit (pure, tested: preset for a given time and sunrise or sunset), and the scroll pass-through helper.
- **Unchanged:** CTAKit's models, parsers, `CardBuilder`, `StopIndex`, `ArrivalsStore`, `LiveLineStore`, BLE, settings, and widgets.
- **Project:** deployment target 26.0, the `mapbox-maps-ios` package, and `MAPBOX_TOKEN` added to `Secrets.example.plist`.

## 8. Testing

Adds to app spec section 9.

- `SunTimes`: dawn, day, dusk, and night at the edges, plus the no-weather fallback.
- Contrast: every tile color, including Ink, against its text color.
- Previews for each tile state in light and dark, and the detail page from fixtures.
- On the phone: the list passes map pans through, the scroll pins and fades correctly, the map frames the visible area, and every light preset looks right.

## 9. Open items

- **Mapbox token:** Leo creates a public token on mapbox.com. The Swift Package download needs no token (checked 2026-11-05 with 11.34.1).
- **Theme:** `faded` or `monochrome`, chosen on the phone.

## 10. Out of scope

Elevated L tracks drawn at track height, custom map styles in Mapbox Studio, per-route bus colors, trip planning, and swiping a card between directions.
