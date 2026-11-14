# One ecosystem: the app, dock, and wearable working together

- **Date:** 2026-11-14
- **Owner:** Leo
- **Due:** the final showcase, 2026-11-23 (about 9 working days)
- **Status:** Approved by Leo, 2026-11-14
- **Builds on:** `2026-11-03-cta-tracker-design.md` (the base spec) and `2026-11-04-ios-app-design.md` (the app spec).
  Where this document and those disagree on how the devices talk to each other, this one wins.

"Wearable" and "handheld" mean the same device here (the 1.75" round board).

## 1. Intent

The dock firmware, the wearable firmware, and the iPhone app each work on their own: each polls CTA itself and shows
live arrivals. What's missing is the link between them. The base spec put that link on Bluetooth (section 10), and
none of it is built: neither board has any Bluetooth code, and the app has the message encoders
(`CTAKit/BLEMessages.swift`) but no CoreBluetooth.

The showcase is a longer demo, so several features have to work end to end, and it has to feel like one product: you
set things on the phone and the dock and the wearable follow, and the other way around.

**The showcase runs on one Wi-Fi network the whole time.** The phone, the dock, and the wearable are always on the
same network. That's why the link after setup is the local network, not Bluetooth (section 4), and why features that
only matter away from home don't have to be fully functional (Leo, 2026-11-14).

## 2. What the showcase shows

In the order it would be shown. Steps 1–8 are committed; section 9 has two extras built only if there's time.

1. **Pairing.** A "Dock found · Set up" card slides up in the app. The device shows a 6-digit code, the user types
   it into iOS's pairing prompt, and the app sends the Wi-Fi network. The same flow pairs the wearable.
2. **One Trackie.** Changing Trackie's name or color in the app changes Trackie on the dock and the wearable.
3. **Dock stops both ways.** Picking a stop on the dock's screen shows in the app; picking one in the app changes
   the dock's section and its LED row.
4. **Nearby on the wrist.** Waking the wearable makes it ask the phone where it is, and it fills with nearby stops.
   This replaces `tools/send_nearby.py`. While the app is open, the wearable mirrors the phone: pan the app's map
   somewhere and the wearable shows the same stops the phone's list does (Leo, 2026-11-14).
5. **Send to Trackie.** In the app's stop detail, "Send to Trackie" pins that stop to the wearable's Home card.
6. **Follow a train.** Choosing one departure in the app makes the wearable count down to that vehicle.
7. **No Wi-Fi.** With the wearable's Wi-Fi off, the phone keeps its cards updated over Bluetooth. (Last; drops first.)
8. **Docking.** Setting the wearable on the dock puts Trackie to sleep. Find my handheld shows where it was last
   seen.

Also folded in from `todo.md`: **F1** (the wearable saves at least 5 Wi-Fi networks and joins the strongest, set from
the app instead of `secrets.h`) and **F5** (tap Trackie for the reason behind his mood, which mostly exists on the
wearable already).

## 3. Goals, non-goals, anti-goals

**Goals**

- Steps 1–8 work on the real hardware, on one network, repeatably, for a long live demo.
- Pairing can be shown live more than once: a device can be reset to fresh without reflashing (section 5.4).
- The dock's frame rate and the wearable's battery behavior don't get worse than today's merged numbers
  (`docs/perf/summary.md`, `docs/power/summary.md`).

**Non-goals**

- Working away from home: the dock following you across town, the relay while the app is in the background,
  find my handheld beyond "last seen on the network."
- Any server or internet relay between devices (base spec §2 still holds).
- New ideas beyond this list. Leo cut the big ones on 2026-11-14: Live Activity, a live location across the city,
  the coming-home view, and F8–F15 (numbered as in `todo.md` at `f376f0a`).
- Widgets. They aren't part of this design; they stay as the app spec has them and are scheduled separately.
- More than one wearable. The demo has one (Leo, 2026-11-14), so "Send to Trackie," "Follow," the mirrored stops,
  and find my handheld all go to the one paired wearable; the app needs no way to choose between wearables.

**The demo hardware (Leo, 2026-11-14):** one dock and one wearable, the wearable with its battery fitted and docked
on the dock's charging plug, so docked detection (base spec 8.4) and the battery fields in `info` work as built.

**Anti-goals** (failures even if everything technically works)

- A flaky demo: a feature that works one time in three is worse than one that isn't there. Each step must recover by
  itself from a dropped request or a rebooted device.
- Anyone on the network can change the devices: writes need the token set at pairing (section 4.3).
- The dock stutters or the wearable runs out of internal RAM because of the new link.
- A Wi-Fi password ever crosses the network in the clear, or gets logged or stored on the phone.

## 4. Architecture

### 4.1 Approaches considered

| | How | Trade-off |
|---|---|---|
| **A. Bluetooth for everything** (base spec §10 as written) | Pairing, Wi-Fi, config, nearby, relay, and the new messages all over BLE | Matches the spec and works away from home. But Bluetooth runs all the time on boards with only 44–49 KB of internal RAM to spare at their worst moment, messages are capped at 512 bytes, and the dock and wearable still can't talk to each other. |
| **B. Bluetooth to set up, then the local network** (**picked**) | BLE only for the pairing code and handing over Wi-Fi; after that each device answers small HTTP requests on the home network | Bluetooth runs only during setup, no size cap, testable with `curl` from the Mac, and the dock and wearable can talk directly. Departs from spec §10; iOS asks once for local network permission; step 7 still needs a small BLE data path. |
| **C. Both** | Spec §10 as written, plus a local link between dock and wearable | The most work of the three. Dropped. |

Leo picked B on 2026-11-14, because the showcase stays on one network and there are about 10 days left.

### 4.2 The shape

```
                      ┌──────────── home Wi-Fi (one network) ────────────┐
                      │                                                  │
   ┌─────────┐  HTTP  │  ┌───────────┐   HTTP   ┌───────────┐            │
   │  Dock   │◄───────┼──┤ iPhone app├─────────►│ Wearable  │            │
   │ (4.3")  │◄───────┼──┴───────────┴──────────┤ (1.75")   │            │
   └────┬────┘  HTTP (dock ↔ wearable, extras)  └─────┬─────┘            │
        │                                             │                  │
        └──── BLE: setup only (pair, Wi-Fi) ──────────┘ + step 7 relay   │
                      └──────────────────────────────────────────────────┘
   Each device still polls CTA, alerts, and weather itself, exactly as today.
```

- **Bluetooth (setup):** the base spec's GATT service and UUIDs (`ble_ids.h`), cut down to what setup needs:
  `info` (read) and `wifi` (write, encrypted), plus one new characteristic for the pairing token (section 4.3).
  Passkey pairing stays as in base spec §10: the device shows 6 digits, iOS asks for them, the bond is stored.
- **When Bluetooth runs** (my call, to keep it off the RAM budget): only while the device has no working Wi-Fi
  (no network saved, or none joined for 60 s), and for 2 minutes after the app asks for it over the network (to add a
  Wi-Fi network later, section 5.1). Otherwise the stack is shut down and its memory freed.
- **Local network:** each device runs a small HTTP server on port 80 on its existing network task (core 0, never the
  dock's frame loop), and announces itself over mDNS as `cta-dock-xxxx.local` / `cta-pet-xxxx.local` (the same
  `xxxx` as its BLE name) with a Bonjour service `_ctatracker._tcp` whose TXT record carries `type=dock|handheld` and
  `id`. The app finds devices with `NWBrowser`. Bodies are JSON with the base spec's shapes wherever one exists.
- **Device → app:** while the app is in the foreground it polls each known device's `GET /info` every 2 s (my call:
  far simpler than the device pushing to the phone, and cheap on a LAN). Counters in `info` tell the app something
  changed (section 4.4). In the background the app polls nothing (base spec 6.6 still holds).

### 4.3 Pairing token

At the end of setup the app writes a random 128-bit token over the encrypted BLE link. Every device paired from the
same app gets the same token (one household). Every HTTP request that changes something carries
`Authorization: Bearer <token>`; reads (`GET /info`) don't need it. The token lives in each device's NVS and in the
app's Keychain. Devices use it to talk to each other too (section 9). Plain HTTP on the home network is accepted for
the demo; the Wi-Fi password never goes over it.

### 4.4 Messages

New or changed shapes are marked. All JSON; IDs and names keep the base spec's sizes.

| Endpoint | Device | Body | Notes |
|---|---|---|---|
| `GET /info` | both | `{type, id, fw, battery, charging, wifi:{connected, ssid}, last_update, picks_ver, wake, docked}` | **New fields:** `picks_ver` (dock: goes up on every pick, from either side), `wake` (wearable: goes up on every wake, 8.2), `docked` (wearable, 8.4). |
| `GET /config`, `POST /config` | both | base spec §10 `config`: `{stops, pet, settings}`, any subset | `stops` is dock-only. The app writes `pet` and `settings` to every device on connect and on change (app spec §6). |
| `POST /nearby` | wearable | base spec §10 `nearby` | Parsed by the existing `cta::parseNearby`; same path as `send_nearby.py` today. |
| `POST /pin`, `DELETE /pin` | wearable | **new:** one picked stop, the `ConfigStop` shape `{type, route, dir, id, name}` | Step 5. |
| `POST /follow`, `DELETE /follow` | wearable | **new:** `{type, route, dir, id, name, vehicle, dest}` | Step 6. `vehicle` is the train run number (`rn`) or bus vehicle ID (`vid`). |
| `POST /ble` | both | `{}` | Opens Bluetooth for 2 minutes (adding a Wi-Fi network). |
| `POST /reset` | both | `{}` | Forgets Wi-Fi, bonds, and token, then reboots fresh (section 5.4). |
| BLE `snapshot` | wearable | base spec §10, unchanged | Step 7 only. |

The shared C++ library gets parsers for the new bodies next to `nearby_msg` and `picks`, with native tests; CTAKit
gets the matching encoders next to `BLEMessages.swift`.

## 5. The features

### 5.1 Pairing and Wi-Fi (step 1, F1)

- The app's found card and setup flow are as app spec §6 has them: Pair ("Type the code shown on your dock") →
  Wi-Fi (name pre-filled from `NEHotspotNetwork.fetchCurrent`, editable) → Pet (first device only) → done.
- After the Wi-Fi write, the device joins, starts mDNS and the HTTP server, and the app waits until it shows up on the
  network (about 20 s at most; spec 8.3's join limit) before saying "Ready." The token write is the last BLE step.
- **Settings → Devices** lists each device from its `info`: online or not connected, last update, Wi-Fi network,
  and (wearable) battery and charging. A device's page has its Wi-Fi networks, its stops (dock), and Forget.
- **F1:** the wearable keeps up to 5 networks (slot 0 is home, used by docked detection, 8.4); the dock keeps 1.
  The wearable joins the strongest saved network in range and moves to the next strongest when the link drops, as
  `todo.md` F1 describes. "Add network" on the device page calls `POST /ble`, then writes the network over the
  encrypted BLE link, so a password never crosses the network. `secrets.h` Wi-Fi stays as a build-time fallback.
- iOS needs `NSLocalNetworkUsageDescription` and `NSBonjourServices` (`_ctatracker._tcp`) in the app's Info.plist.

### 5.2 One Trackie (step 2)

- **Settings → Pet:** a name and one of 6 color presets. The presets are new: `art/presets.txt` (doesn't exist yet;
  `PetConfig` already expects colors for palette slots 2 and 3). The app's own Trackie uses the same palette swap.
- Written as `config.pet` to every paired device; each device swaps palette slots 2 and 3 at runtime and redraws
  Trackie. The name shows wherever the devices already name Trackie, if anywhere; no new layout for it (my call).
- **Device settings** (screen brightness, LED brightness, night hours) ride the same `config` write (my call: it's
  the same path and base spec demo check 9 lists it).

### 5.3 Dock stops both ways (step 3)

- The dock's device page shows its 3 sections. Tapping one opens the same picker as search; choosing a card (a stop
  plus a line or route and direction) sends `config.stops` with all 3 sections. The dock saves and applies them
  through the same path as its on-screen picker (`store::savePicks`, `net::setPicks`).
- A pick on the dock raises `picks_ver`; the app sees it in its next `info` poll and reads `GET /config`. The most
  recent pick wins (base spec 7.2).

### 5.4 Resetting for the demo

`POST /reset`, a serial command, and a hidden gesture on each device (the builder picks one that can't happen by
accident) wipe Wi-Fi, bonds, and the token and reboot to the fresh "Open the CTA app" state. iOS doesn't let apps
remove a Bluetooth bond, so after a reset the user removes the device in iOS Settings → Bluetooth before pairing it
again; the demo script says so.

### 5.5 Nearby on the wrist (step 4)

- On every wake (8.2) the wearable raises `wake`. When the app sees it go up, it takes one location fix and sends
  `POST /nearby`, built exactly as `send_nearby.py` builds it today (5 stations, 5 bus stops, `close`).
- The app also sends `nearby` when a wearable first comes online. `send_nearby.py` stays as a tool for testing
  without the phone.
- **Mirror the phone (Leo, 2026-11-14).** While the app is in the foreground, the `nearby` it sends is built from the
  stops behind the phone's card list for the current map center (the list already follows a panned map, base spec
  9.1), not a fresh location fix: the first 5 stations and first 5 bus stops in the list's order, and `close` the
  first 3 of either kind. That way the wearable's stops are the phone's stops. `lat`/`lon` are the map center
  (the wearable uses them only for weather).
- It's sent when the list settles after a pan, at most once every 10 s and only when the stops changed (my call: each
  new list makes the wearable drop its data and poll about 6 requests, so sending on every frame of a pan would only
  flood CTA and blank its cards). The recenter button sends the list for the user's own location again.
- When the app goes to the background the wearable keeps the last list it got; its next wake asks again (above), and
  the app, if open, answers with what its list shows then.
- A pinned or followed stop (5.6, 5.7) stays on the wearable's Home card across new lists.

### 5.6 Send to Trackie (step 5)

- Stop detail gets a "Send to Trackie" button next to the ★, shown only while a paired wearable is online. It sends
  `POST /pin` with that card's stop, line or route, and direction. Tapping cards elsewhere in the app never sends
  anything.
- On the wearable, the pinned stop becomes the Home card with a ★ marker, ahead of the usual most-urgent rule. It's
  polled at the close stations' rate (every 60 s, spec 6.6) even when it isn't in the nearby list, and it counts as a
  close stop for Hyped (6.3).
- The pin clears on: a swipe on the pinned card (the builder fits the gesture into `docs/handheld-ui.md`), a new pin,
  docking, `DELETE /pin`, or a reboot (my call: kept in RAM only).

### 5.7 Follow a train (step 6)

- The app needs each departure's vehicle: keep `rn` from Train Tracker arrivals and `vid` from Bus Tracker
  predictions on `Prediction` (the data part of `todo.md` F6; the parsers already read both feeds into `Vehicle`).
- In stop detail, a departure tile offers "Follow on Trackie" (the builder picks the gesture: a long press or a small
  button on the open tile). It sends `POST /follow`, which also pins that stop.
- The wearable's Home card counts down to that vehicle, matched by `rn`/`vid` in the predictions it polls for the
  stop: "Red · 4 min · to 95th." When it's due it says so and clears a minute later. If the vehicle drops out of the
  predictions before it's due, the card says the train (or bus) was lost, then falls back to the pin. `DELETE /follow`
  or a new follow replaces it.
- F6's map display (every vehicle on the line in the app's map) isn't part of this design.

### 5.8 Relay with no Wi-Fi (step 7; last, drops first)

When the wearable's Wi-Fi has been down for 60 s, its Bluetooth comes on (4.2). The app, in the foreground, connects
to it and writes one `snapshot` per card every 60 s, as base spec 8.3 step 4 describes. Cards show the relayed data
as they show polled data. Only this needs a Bluetooth data path after setup.

### 5.9 Docking and find my handheld (step 8)

- Docked detection and Trackie asleep are already built (8.4). The wearable reports `docked` in `info`.
- **Find my handheld:** when a wearable that was online misses 3 polls in a row (about 6 s), the app saves the
  phone's location and the time, and Settings → Find my handheld shows them as a map pin, or "Connected now." On the
  one demo network, powering the wearable off shows it.

## 6. Constraints

- **Time:** about 9 working days. Steps 1–6 and 8 first, in the order of section 10; step 7 and section 9 slip first.
- **Internal RAM:** the foundation plan's floor is 40 KB free internal RAM at the worst moment. Today: the wearable
  about 44 KB, the dock 49–51 KB, both during a TLS handshake. Bluetooth, the HTTP server, and mDNS all take some.
- **The dock's frame loop:** all new work runs on the network task (core 0). The dock perf runner
  (`tools/dock_perf_run.py`) must show no worse frames than `docs/perf/summary.md`.
- **The wearable's battery:** the server and the app's 2 s polls keep its radio busier while the app is open. Check
  with the power tools (`docs/power/`) that the dim, app-closed case is unchanged.
- **Toolchain:** both boards on arduino-esp32 3.2.0 high_perf (the README's Toolchain section). Base spec §15 lists
  NimBLE-Arduino against the core's own BLE as unverified; the builder picks whichever runs here.
- **Security:** the public repo rules of base spec §12 hold. The token and Wi-Fi passwords are never logged, never
  committed, and the app keeps the token in the Keychain.
- **Docs:** base spec §8.3, §9.5, and §10 get updated to point here when this is built.

### 6.1 The rest of `todo.md`, built in the same window

Leo wants most of the todo list done for the showcase too (2026-11-14): F2 (battery level on the wearable), F3 (a
settings page on the wearable), F4 (a new font on all three), F6's map display, F7 (pixel train and bus symbols on the
iOS map), and the bugs B1 and B2. They're planned separately, but they touch this design in these places:

- **F3, the wearable's settings page,** is where the wearable's side of this design shows: the saved networks from
  F1 (forget, move up), pairing and unpairing (the reset of 5.4), and the Wi-Fi off switch for step 7. If F3 lands
  first, those controls go there; if not, they're the hidden gesture and serial command this design specifies
  (my call). Adding a network still comes from the app over Bluetooth (5.1); F3 never takes a password on the watch.
- **F2, the battery level,** reads the same power state that `info` reports; one source for both.
- **F4, the new font,** changes text on every screen this design adds (setup messages, the ★ card, the countdown).
  New screens use the existing text routines (`firmware/lib/gfx` fonts, `Theme` in the app), so the swap stays in one
  place.
- **B2 and the dock's band copies by DMA** (merged to main as `d0769a0`, picker scroll at 34 fps) change what the dock
  is doing on core 0, where the HTTP server runs. The perf check in section 6 runs against those numbers, not the
  older ones.

## 7. Risks

| Risk | Mitigation |
|---|---|
| Bluetooth doesn't fit in internal RAM next to Wi-Fi and TLS | The first task proves it on both boards (section 10). Fallback: pairing over the network instead. Wi-Fi comes from `secrets.h`, the device shows the 6-digit code on screen, and the user types it into the app, which then gets the token over HTTP. The pairing moment survives; only the Wi-Fi handover is lost. |
| The campus IoT network blocks devices from reaching each other or blocks multicast (common on campus networks; the phone can join it, Leo, 2026-11-14) | The showcase runs on the campus IoT network, and Leo develops and tests on his home network (2026-11-14; network names stay out of the repo). Day 1 checks both (section 10). Both go in the wearable's saved list (F1); for the showcase the campus network is slot 0, home, so docking works there (8.4). The app remembers each device's last IP address and tries it directly, and a device page shows its address, so blocked mDNS alone isn't fatal. If the boards and the phone can't reach each other there at all, the phone's hotspot is the demo network. Assumed: the IoT network takes a password only (my call); one that signs in with a username (PEAP, which the wearable's `WIFI_EAP_USER` already supports) would need a username field in the `wifi` message. |
| The fallback hotspot is the same iPhone that runs the app | Then `NEHotspotNetwork` can't pre-fill the network name (the user types it), and whether the app can reach boards on its own hotspot is untested; check it only if the campus network fails day 1. |
| iOS local network permission is denied | The app explains it and links to Settings, the way it handles location. |
| A device reboots mid-demo | Everything the app sent that must survive is in flash (Wi-Fi, token, picks, pet, settings, nearby); the pin and follow are re-sent by the app when it sees the device come back (my call). |

## 8. Decided by Leo vs. left to the builder

**Leo decided (2026-11-14):** the 8 steps and the F1/F5 fold-in; approach B; one network for the whole showcase;
"away" features needn't fully work; no big builds; Trackie's look follows the app; pairing shown in the app for
both devices; tapping a stop sends it to the wearable (as "Send to Trackie" and "Follow"); the wearable mirrors the
stops on the phone's list while the app is open.

Also: one wearable in the demo, with its battery in and docked on the dock's plug; most of `todo.md` done in the same
window (6.1).

**My calls, marked above:** when Bluetooth runs; the 2 s `info` poll; the pairing token; the extra `info` fields; the
pin kept in RAM only; device settings riding along; the reset paths; re-sending pin and follow after a reboot; the
mirror's 10 s limit; a hidden gesture and serial command for step 7's Wi-Fi off switch until F3's page exists.

**Left to the builder:** HTTP server and mDNS libraries, NimBLE-Arduino or the core's BLE, the exact JSON field
names beyond those above, the gestures (pin swipe, follow, reset), the layout of the ★ and countdown on the wearable
within `docs/handheld-ui.md`, and the setup screens' look within app spec §4.6.

## 9. Extras, only if steps 1–6 and 8 are done

1. **The dock follows your train.** On a follow, the app also tells the dock (`POST /trip` with the follow body plus
   Trackie's name). The dock already polls vehicle positions and draws vehicles on its map; it draws Trackie on that
   vehicle with "Trackie · Red Line · at Fullerton." Before the vehicle reaches the stop, Trackie waits at the stop;
   after, he rides it. Because the demo is on one network, the wearable tells the dock directly when the follow ends,
   so the dock doesn't have to guess. It also ends at the end of the line, if the vehicle drops out of the feed, or
   after 90 minutes.
2. **Trackie hops between devices.** When the wearable docks it tells the dock over the network; Trackie's sleep
   animation on the wearable and a hop-in on the dock's status strip play together, and the reverse on undock.

## 10. Order of work

1. **Prove the link on both boards (day 1–2):** Bluetooth pairing with the passkey, then Wi-Fi with a TLS request
   running, the HTTP server, and mDNS, all at once, with internal RAM logged. Decide BLE or the fallback (section 7)
   here. On both the campus IoT network and the home network, check that the iPhone can find the boards by mDNS
   and reach them by IP, and that the two boards can reach each other; this settles which network the demo uses.
2. The network layer on both boards (server, mDNS, token, `info`, `config`) and the app's device discovery, Devices
   list, and setup flow (step 1).
3. Steps 2 and 3 (pet and settings, dock stops both ways).
4. Steps 4, 5, 6 (nearby, pin, follow), with the `rn`/`vid` data in CTAKit.
5. Step 8 (find my handheld) and the reset paths.
6. F1 and F5 polish, then step 7, then section 9.
7. A full dry run of section 2 on the hardware, on one network, at least twice in a row.

## 11. Testing

- **Shared C++ library (native, Unity):** parsing every new body (pin, follow, the new `info` fields, trip), including
  malformed ones that must leave the old state alone, as `parseNearby` and `decodePicks` do.
- **CTAKit (Swift Testing):** encoders for every new body, the `rn`/`vid` on `Prediction` from the shared fixtures,
  and the token never appearing in any description or log string (as `WifiMessage` does for passwords).
- **On the Mac:** a small script (or `curl` lines in the READMEs) that drives each endpoint, so the firmware can be
  tested before the app side exists.
- **On hardware:** the section 2 run, the dock perf runner, a power run of the wearable dim with the app closed, and
  a reset → pair → demo cycle.
