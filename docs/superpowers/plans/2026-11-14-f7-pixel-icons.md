# F7: pixel train and bus symbols on the iOS map

Track F7 of the ecosystem plan (`2026-11-14-ecosystem.md`). The map's stop icons become pixel versions of the
cards' SF Symbols, `tram.fill.tunnel` and `bus.fill`, under the app spec's 4.6 pixel rules.

## Decisions (Leo, 2026-11-14)

1. **Single-line station: tunnel A.** A one-art-pixel arch in the line's color, a navy gap, and the tram front
   inside, also in the line's color.
2. **Station on several lines: M3.** A cream tram in the same tunnel; the arch splits into one run of pixels per
   line, left foot over the top to the right foot, in the station's line order (CTA order, as
   `StopIcons.routes`). Every line shows, up to the Loop's 6. No color strip, so every station icon has the
   same height.
3. **Bus stop: bus A.** The `bus.fill` shape: narrower body, side mirrors, big windshield, two headlights with a
   navy grille, dark tires, in the bus blue.

Unchanged: 16×16 grids, the one-pixel white halo (18×18 art), whole device pixels per art pixel with
nearest-neighbor scaling (30 pt target), and the open stop ranked first in collisions (`DioramaMap`'s `rank`).

## Palette

`.KCDWYHT`: clear, outline (navy), body, glass, glint, headlight, halo, tunnel arch. For one line, slot 7
is the line's color; with several lines each arch pixel moves to slot 8 + its line's position.
At most 8 + 6 = 14 slots, inside the spec's 16.

## Files

- `ios/CTAKit/Sources/CTAKit/StopArt.swift`: the grids, the arch path and split (`stationGrid(lines:)`), and the
  halo. In CTAKit so the tests reach it; the app has no test target of its own.
- `ios/CTATracker/Map/StopIcons.swift`: colors the grid (palette above) and upscales it, as before.
- `ios/CTAKit/Tests/CTAKitTests/StopArtTests.swift`: both grids are 16×16 in the charset; one line keeps the arch
  in slot 7; for 2–6 lines only arch pixels change, each belongs to a line, the walk along the arch meets the
  lines once each in order with even runs; the arch path is connected; the drawn height is the same for any
  line count; the halo rings the art.

Check: `swift test --package-path ios/CTAKit`, and the app builds for the simulator without signing
(`xcodebuild build -project ios/CTATracker.xcodeproj -scheme CTATracker -destination 'generic/platform=iOS Simulator' CODE_SIGNING_ALLOWED=NO`).
Rendered output was compared by eye with the candidate previews in `art/build/f7/` (local, gitignored).

## For F6

A moving vehicle uses the tram (or bus) without the tunnel arch, so it never reads as a station. The tram
alone is `StopArt.station` rows 2–13, columns 3–12, with the two `T` corners in row 2 cleared; the bus is
`StopArt.bus` as is. F6 adds that crop when it draws vehicles; F7 adds no code for it.
