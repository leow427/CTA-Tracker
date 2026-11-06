# Ideas (temporary)

Exploratory work that isn't part of the spec yet. Nothing here is a contract: the spec in `docs/superpowers/specs/` wins wherever the two disagree. Files get promoted into the spec or deleted once a decision is made.

| File | What it is |
|---|---|
| `2026-11-04-handheld-ui-mockup.html` | Interactive mockup of the handheld's round 466×466 AMOLED UI, restyled on 2026-11-05 to match the iPhone app: night navy, line-colored pixel-plate cards, the app's pixel minutes, and Trackie (the app's 32×32 sprite) on a cream tile with a speech bubble. Home, Trains/Buses lists, stop detail, a pixel line map (swipe left from Home: the L lines drawn on the device from `data/train_shapes.json`, nearby stops, live trains, recenter), dim always-on, docked, pairing and first-run states, plus a panel-power meter. Open it in a browser. |
| `2026-11-05-ios-diorama-mockup.html` | Scratch mockup of the iPhone app's diorama map (see the iOS redesign spec). |

Open questions from the handheld mockup:

1. Navy instead of black on the awake screens: worth the battery cost? The mockup's power meter compares them.
2. Trackie on the cream tile at Home (like the app), or straight on navy?
3. His bubble says a mood word ("Go now!") and switches to the reason when tapped. Right split?
4. Train rows are one card per station, colored by its soonest line. One card per line and direction instead, like the app?
5. The map: a handheld screen for the alpha, or a stretch item after the core path works? It's not in the spec yet.
6. Spec 8.1 still says "the pet centered on black" and rows with a line-color dot. Update it if this look is kept.
