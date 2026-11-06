# Ideas (temporary)

Exploratory work that isn't part of the spec yet. Nothing here is a contract: the spec in `docs/superpowers/specs/` wins wherever the two disagree. Files get promoted into the spec or deleted once a decision is made.

| File | What it is |
|---|---|
| `2026-11-04-handheld-ui-mockup.html` | Interactive mockup of the handheld's round 466×466 AMOLED UI, restyled on 2026-11-05 to match the iPhone app: night navy, line-colored pixel-plate cards, the app's pixel minutes, and Trackie (the app's 32×32 sprite) on a cream tile with a speech bubble. Home, Trains/Buses lists, stop detail, a pixel line map (swipe left from Home: the L lines drawn on the device from `data/train_shapes.json`, nearby stops, live trains, recenter), dim always-on, docked, pairing and first-run states, plus a panel-power meter. Open it in a browser. |
| `2026-11-05-ios-diorama-mockup.html` | Scratch mockup of the iPhone app's diorama map (see the iOS redesign spec). |

Questions from the handheld mockup, answered by Leo on 2026-11-06 (spec §8.1 and Plan 3 now follow these):

1. ~~Navy or black on the awake screens?~~ **Black.** The mockup still draws navy.
2. **Still open:** Trackie on the cream tile at Home (like the app), or straight on the background?
3. ~~Mood word in the bubble, the reason on tap?~~ **Yes.**
4. ~~One card per station, or per line and direction?~~ **One card per line and direction at each station.**
5. ~~The map: alpha or stretch?~~ **Stretch** (spec §2, Plan 3 milestone 10).
6. ~~Update spec §8.1 to this look?~~ **Done.**
