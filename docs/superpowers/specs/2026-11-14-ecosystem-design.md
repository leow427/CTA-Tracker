# One ecosystem: the app, dock, and wearable working together

- **Date:** 2026-11-14
- **Owner:** Leo
- **Due:** the final showcase, 2026-11-23 (about 9 working days)
- **Status:** Approved by Leo, 2026-11-14. **Changed 2026-11-14 after the day-1 spike (Leo): no Bluetooth for the
  showcase.** Pairing is over the network with the code on screen, Wi-Fi comes from `secrets.h`, and step 7 (the relay)
  is dropped. Bluetooth is future work (section 12). Results: `docs/ecosystem/day1-link.md`.
  **Changed again 2026-11-14 after the ESP-IDF port (Leo): Bluetooth first for the wearable, the dock Wi-Fi only.**
  On ESP-IDF, Bluetooth fits in internal RAM (controller in flash, buffers in PSRAM). The phone and the wearable talk
  over a bonded Bluetooth link: setup, every message, and the relay of arrivals, so the wearable works with no Wi-Fi
  (step 7 is back if its gates pass by 11/20 18:00). The wearable's Wi-Fi path stays a working fallback, its own
  pairing by code included ("still implement Wi-Fi on the wearable, just in case Bluetooth doesn't work out"). The dock
  is unchanged: Wi-Fi only, pairing over the network. Plan: `docs/superpowers/plans/2026-11-14-wearable-ble-link.md`.
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
same network, and features that only matter away from home don't have to be fully functional (Leo, 2026-11-14). The
dock's link is the local network. The wearable's link is Bluetooth first, with the local network as its fallback
(section 4), so the wearable doesn't depend on the router letting the phone reach it.

## 2. What the showcase shows

In the order it would be shown. Steps 1–6 and 8 are committed; step 7 is back, conditionally (below).
Section 9 has two extras built only if there's time.

1. **Pairing.** A "Dock found · Set up" card slides up in the app. The device shows a 6-digit code and the user types
   it into the app. The wearable pairs the same way over Bluetooth: Trackie shows a passkey and the user types it
   into iOS's prompt. If Bluetooth isn't available, the wearable shows a network pairing code like the dock's.
2. **One Trackie.** Changing Trackie's name or color in the app changes Trackie on the dock and the wearable.
3. **Dock stops both ways.** Picking a stop on the dock's screen shows in the app; picking one in the app changes
   the dock's section and its LED row.
4. **Nearby on the wrist.** Waking the wearable makes it ask the phone where it is, and it fills with nearby stops.
   This replaces `tools/send_nearby.py`. While the app is open, the wearable mirrors the phone: pan the app's map
   somewhere and the wearable shows the same stops the phone's list does (Leo, 2026-11-14).
5. **Send to Trackie.** In the app's stop detail, "Send to Trackie" pins that stop to the wearable's Home card.
6. **Follow a train.** Choosing one departure in the app makes the wearable count down to that vehicle.
7. **No Wi-Fi.** With the wearable's Wi-Fi off, the phone keeps its cards updated over Bluetooth. **Conditional
   (Leo, 2026-11-14):** shown only if the wearable's RAM, power and latency gates (section 6) pass by 11/20 18:00;
   otherwise it stays cut and Bluetooth still carries steps 4–6.
8. **Docking.** Setting the wearable on the dock puts Trackie to sleep. Find my handheld shows where it was last
   seen.

Also folded in from `todo.md`: **F1** (the wearable saves at least 5 Wi-Fi networks and joins the strongest, set
from the app over Bluetooth, with `secrets.h` as the fallback list) and **F5** (tap Trackie for the reason behind his mood, which mostly exists on the
wearable already).

## 3. Goals, non-goals, anti-goals

**Goals**

- Steps 1–6 and 8 work on the real hardware, on one network, repeatably, for a long live demo; step 7 too if its
  gates pass.
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
- Anyone on the network or in Bluetooth range can change the devices: HTTP writes need the token set at pairing,
  and Bluetooth writes need the bonded, encrypted link (section 4.3).
- The dock stutters or the wearable runs out of internal RAM because of the new link.
- A Wi-Fi password ever crosses the network, or anything but the bonded Bluetooth link, or gets logged or stored
  on the phone.

## 4. Architecture

### 4.1 Approaches considered

| | How | Trade-off |
|---|---|---|
| **A. Bluetooth for everything** (base spec §10 as written) | Pairing, Wi-Fi, config, nearby, relay, and the new messages all over BLE | Matches the spec and works away from home. But Bluetooth runs all the time on boards with only 44–49 KB of internal RAM to spare at their worst moment, messages are capped at 512 bytes, and the dock and wearable still can't talk to each other. |
| **B. Bluetooth to set up, then the local network** (**picked**; cut to the local network only after day 1, below) | BLE only for the pairing code and handing over Wi-Fi; after that each device answers small HTTP requests on the home network | Bluetooth runs only during setup, no size cap, testable with `curl` from the Mac, and the dock and wearable can talk directly. Departs from spec §10; iOS asks once for local network permission; step 7 still needs a small BLE data path. |
| **C. Both** | Spec §10 as written, plus a local link between dock and wearable | The most work of the three. Dropped. |

Leo picked B on 2026-11-14, because the showcase stays on one network and there are about 10 days left.

**Then the local network only (Leo, 2026-11-14, after the day-1 spike).** BLE pairing worked on both boards, but
Bluetooth costs about 50 KB of internal RAM, which would take both boards to about 0 KB free during a TLS handshake
(`docs/ecosystem/day1-link.md`). So B loses its Bluetooth part: pairing moves to the network (the device shows a
6-digit code, the app sends it with `POST /pair`), Wi-Fi comes from `secrets.h`, and step 7 is dropped. The rest of
B stands.

**Then Bluetooth first for the wearable (Leo, 2026-11-14, after the ESP-IDF port).** On native ESP-IDF the Bluetooth
controller runs from flash and its buffers move to PSRAM, so it fits next to Wi-Fi and TLS (measured on the wearable
in W5, re-measured in the plan's Task W6a). The wearable's link becomes Bluetooth for everything (closer to A, but on
one bonded link with the HTTP messages carried inside it), and the local network is its fallback. The dock stays on
the local network only (Leo: "the dock is only wifi"). Plan: `docs/superpowers/plans/2026-11-14-wearable-ble-link.md`.

### 4.2 The shape

```
                       Bluetooth (bonded, encrypted): setup, messages, status, relay
                     ┌────────────────────────────────────────────────┐
                     │                                                ▼
   ┌─────────┐  HTTP ┌────────────┐  HTTP (fallback, when BT is down) ┌───────────┐
   │  Dock   │◄──────┤ iPhone app ├ - - - - - - - - - - - - - - - - -►│ Wearable  │
   │ (4.3")  │       └────────────┘                                   │ (1.75")   │
   └─────────┘◄ - - - - - - HTTP (dock ↔ wearable, extras) - - - - - -┤           │
        └──────────────────── home Wi-Fi (one network) ───────────────┴───────────┘
   The dock polls CTA, alerts, and weather itself. The wearable does too when it's on Wi-Fi and the phone isn't
   relaying; while the phone relays over Bluetooth, it polls nothing.
```

- **Wi-Fi:** the dock joins the strongest of the networks compiled into the git-ignored `secrets.h`. The wearable
  joins the strongest of up to 5 networks set from the app over Bluetooth, with `secrets.h` as the fallback list
  when none is saved (section 5.1). Wi-Fi is optional on the wearable. No Wi-Fi password ever crosses the network.
- **Pairing:** the dock over the network, with a 6-digit code on its screen (section 4.3). The wearable over
  Bluetooth with a passkey, or, as the fallback, over the network with a code like the dock's.
- **Bluetooth (wearable only):** one GATT service, `link` for phone → wearable messages and `info` for status
  notifies (section 4.4). The app sends every wearable write over Bluetooth while it holds an encrypted link, and
  over HTTP otherwise.
- **Local network:** each device runs a small HTTP server on port 80 on its existing network task (core 0, never the
  dock's frame loop), and announces itself over mDNS as `cta-dock-xxxx.local` / `cta-pet-xxxx.local` (`xxxx` from
  its MAC) with a Bonjour service `_ctatracker._tcp` whose TXT record carries `type=dock|handheld` and
  `id`. The app finds devices with `NWBrowser`. Bodies are JSON with the base spec's shapes wherever one exists.
- **Device → app:** while the app is in the foreground it polls each known device's `GET /info` every 2 s (my call:
  far simpler than the device pushing to the phone, and cheap on a LAN). Counters in `info` tell the app something
  changed (section 4.4). In the background the app polls nothing (base spec 6.6 still holds). While the wearable is
  connected over Bluetooth, its `info` notifies replace its poll; the poll resumes at once when Bluetooth drops.

### 4.3 Pairing token

**On the dock, and on the wearable as its fallback:** a device with no token shows a 6-digit code on its screen, new at every boot and never logged. The app sends that code
with a random 128-bit token in `POST /pair {"code", "token"}`. An unpaired device stays in pairing mode until it's
paired, with no time window (Leo, 2026-11-14). It compares the code in constant time. After 5 wrong codes it waits
30 s (`423` with `retry_after`) and then shows a new code. To pair again, the user opens the device's own Settings
page (the dock's gear, the wearable's swipe-left page) and taps Pair again, which wipes its token or bond and shows a
fresh code. Every device paired from the same app gets the same token (one household).

Every HTTP request that changes something carries `Authorization: Bearer <token>`. Reads (`GET /info`) and
`POST /pair` don't need it. The token lives in each device's NVS and in the app's Keychain. Devices use it to talk to
each other too (section 9). Plain HTTP on the home network is accepted for the demo. That includes the one `/pair`
request: someone sniffing the network at that moment could see the token. The code proves the user can see the
device. No Wi-Fi password ever goes over the network.

**On the wearable, first: the Bluetooth bond.** The wearable shows a 6-digit passkey in Trackie's bubble (never
logged), and the user types it into iOS's pairing prompt (display-only passkey, bonding, MITM protection, LE Secure
Connections). The bond authenticates every Bluetooth write. Once the link is encrypted, the app writes the household
token over it (`POST /token`), so the wearable's HTTP fallback works with the same token as the dock. The wearable
offers the network code only while it has no token and is on Wi-Fi; a token from either path closes the other. A
reset clears the bond and gives the wearable a new random Bluetooth address, so the iPhone pairs afresh (unverified;
if not, the user forgets the old entry in iOS Settings → Bluetooth).

### 4.4 Messages

New or changed shapes are marked. All JSON; IDs and names keep the base spec's sizes.

| Endpoint | Device | Body | Notes |
|---|---|---|---|
| `GET /info` | both | `{type, id, fw, battery, charging, wifi:{connected, ssid}, last_update, picks_ver, wake, docked, paired}` | **New fields:** `picks_ver` (dock: goes up on every pick, from either side), `wake` (wearable: goes up on every wake, 8.2), `docked` (wearable, 8.4), `paired` (has a token; false raises the app's found card). |
| `POST /pair` | both | **new:** `{code, token}` | Pairing (4.3). No bearer token. HTTP only; on the wearable, the fallback. |
| `GET /config`, `POST /config` | both | base spec §10 `config`: `{stops, pet, settings}`, any subset | `stops` is dock-only. The app writes `pet` and `settings` to every device on connect and on change (app spec §6). |
| `POST /nearby` | wearable | base spec §10 `nearby` | Parsed by the existing `cta::parseNearby`; same path as `send_nearby.py` today. |
| `POST /pin`, `DELETE /pin` | wearable | **new:** one picked stop, the `ConfigStop` shape `{type, route, dir, id, name}` | Step 5. |
| `POST /follow`, `DELETE /follow` | wearable | **new:** `{type, route, dir, id, name, vehicle, dest}` | Step 6. `vehicle` is the train run number (`rn`) or bus vehicle ID (`vid`). |
| `POST /reset` | both | `{}` | Forgets the token, then reboots fresh (section 5.4). The wearable also clears its bonds and makes a new Bluetooth address. |
| `POST /token` | wearable | `{token}` | **Bluetooth only** (404 on HTTP). The household token, written after the bond. |
| `POST /wifi` | wearable | `{slot, ssid, pass}`, slots 0–4 | **Bluetooth only** (404 on HTTP). Saves a network; the wearable rejoins strongest-first. |
| `POST /relay` | wearable | `{now, wmo?, temp?}` | The relay tick: sets the clock if unset, weather if present; over Bluetooth it marks the wearable "relaying" for 90 s, and it stops its own CTA polling meanwhile. |
| `POST /stop` | wearable | `{id, type, records: [stop record…]}` | Relayed arrivals for one stop (base spec 6.2 records); the newest `ts` wins. |

`POST /ble` is gone. The wearable's `info` also carries `wifi.rssi`, `settings`, `relaying` and `pin` (5.6).

**Over Bluetooth (wearable).** The same messages ride one GATT service (UUIDs in `ble_ids.h`):

| Characteristic | Properties | Payload |
|---|---|---|
| Service `d8d4388c-…` | | advertised as `Trackie-xxxx` |
| `info` `fb360799-…` | read, notify (encrypted, authenticated) | the `GET /info` JSON; notified on subscribe, on any change, and every 30 s, at most once a second |
| `link` `4a1eedc7-…` (new) | write with response (encrypted, authenticated) | an envelope `<POST or DELETE> <path>`, a newline, then the JSON body of the HTTP message above |

Messages longer than one write are split into chunks with a 1-byte header (first, last, a 6-bit sequence), up to
8 KB. The wearable answers each write with success, or an ATT error that maps to 400, 413, 404, or a broken
sequence. One dispatcher on the wearable serves both transports with the same parsers. The plan's "The link" section
has the details.

The shared C++ library gets parsers for the new bodies next to `nearby_msg` and `picks`, with native tests; CTAKit
gets the matching encoders next to `BLEMessages.swift`.

## 5. The features

### 5.1 Pairing and Wi-Fi (step 1, F1)

**The dock** (unchanged):

- A fresh dock is already on the network (Wi-Fi from `secrets.h`). The app finds it by Bonjour, and a `GET /info`
  with `paired: false` raises the "Dock found · Set up" (or "Trackie found · Set up") card.
- The setup flow follows app spec §6, minus the Wi-Fi step: Pair ("Type the code shown on your dock") → Pet (first
  device only) → done. A wrong code says so. During the 30 s cooldown the app says a new code will appear on the device.
- The dock shows the code on its setup screen while it has no token, for as long as it takes. During a cooldown it
  shows "Too many tries, new code in N s".

**The wearable,** Bluetooth first:

- A fresh wearable advertises `Trackie-xxxx`; the app, in the foreground, raises "Trackie found · Set up". **Changed
  2026-11-17 (Leo: the user only ever sees "Trackie", never "pet" or "wearable"):** the name was `CTA-Pet-xxxx`; the
  app still accepts that prefix, so a board flashed before the rename keeps working until it's reflashed.
- **Pair:** "Type the code shown on Trackie when your iPhone asks." Trackie's bubble shows the passkey
  (`docs/handheld-ui.md`'s Pairing state). Once bonded, the app writes the household token.
- **Wi-Fi (optional, "Skip"):** network name and password, "Add another" up to 5, home first. Each goes over the
  bonded link (`POST /wifi`); the app watches `info.wifi.connected` for up to 20 s ("Couldn't join. Check the
  password."). The password is never stored or logged on the phone.
- **Pet** (first device only) → **Ready.**
- **The fallback:** when Bluetooth isn't available (off or not allowed on the phone, or the wearable not seen) or the
  Bluetooth pairing fails, and the wearable is on the network unpaired, the app uses the dock's flow: Trackie's bubble
  shows a 6-digit code and the app sends it with `POST /pair` (4.3). Wi-Fi then comes from the wearable's saved list
  or `secrets.h`. Pairing stays open until paired, with the same cooldown and new code after 5 wrong tries.

**Both:**

- **Settings → Devices** lists each device from its `info`: online or not connected, last update, Wi-Fi network,
  and (wearable) battery and charging. A device's page has its stops (dock) and Forget.
- **F1:** both devices take up to 5 networks: the dock from `secrets.h`; the wearable from the app over Bluetooth,
  with `secrets.h` as the fallback list when it has none saved. Home, and the showcase network (the lab network
  checked on 11/16; Leo, 2026-11-16). Slot 0 is home on the wearable, used by docked detection (8.4). Each joins the strongest network in range
  and moves to the next strongest when the link drops, as `todo.md` F1 describes.
- iOS needs `NSLocalNetworkUsageDescription` and `NSBonjourServices` (`_ctatracker._tcp`) in the app's Info.plist,
  `NSBluetoothAlwaysUsageDescription`, and the `bluetooth-central` background mode (it keeps the connection and the
  status notifies; the relay runs in the foreground only).

### 5.2 One Trackie (step 2)

- **Settings → Pet:** a name and one of 6 color presets. The presets are new: `art/presets.txt` (doesn't exist yet;
  `PetConfig` already expects colors for palette slots 2 and 3). The app's own Trackie uses the same palette swap.
- Written as `config.pet` to every paired device; each device swaps palette slots 2 and 3 at runtime and redraws
  Trackie. The name shows wherever the devices already name Trackie, if anywhere; no new layout for it (my call).
- **Device settings** (screen brightness, LED brightness, night hours) ride the same `config` write (my call: it's
  the same path and base spec demo check 9 lists it).

- **Settings layout (Leo, 2026-11-16, after the Task 13 check):** what all devices share sits at the top of
  Settings; each device's own controls live on its device page.
  - Top level: **Trackie** (name, color), **Quiet hours** (start, end, on/off; both devices), Devices, About.
  - **Dock page:** Stops (5.3), Screen brightness, Route LEDs (brightness and an Off switch).
  - **Wearable page:** Screen brightness, battery and charging, Wi-Fi (the network it's on, and "Add a network",
    which reuses the setup flow's Wi-Fi step), and Last seen (find my handheld, 5.9).
  - The app keeps settings per device and sends each its own `settings` (the message doesn't change; quiet hours
    are copied into each). LED brightness is dock only.
  - The dock's backlight is on or off only (the CH422G's EXIO2), so its screen brightness dims the picture in
    software, with the same black veil as night (darker of the two wins; never below about 20% so it stays readable).
  - **On the devices too (Leo, 2026-11-16):** each device's own Settings screen has its screen brightness (and the
    dock's Route LEDs) as − / + steps, and shows the quiet hours (set in the app). A change made on a device is
    saved there and shows in the app: the dock's `info` carries `settings` like the wearable's, and the app adopts a
    value it didn't send. The device owns its own brightness; quiet hours and Trackie stay the app's.

### 5.3 Dock stops both ways (step 3)

- The dock's device page shows its 3 sections. Tapping one opens the same picker as search; choosing a card (a stop
  plus a line or route and direction) sends `config.stops` with all 3 sections. The dock saves and applies them
  through the same path as its on-screen picker (`store::savePicks`, `net::setPicks`).
- A pick on the dock raises `picks_ver`; the app sees it in its next `info` poll and reads `GET /config`. The most
  recent pick wins (base spec 7.2).

### 5.4 Resetting for the demo

`POST /reset`, a serial command, and a hidden gesture on each device (the builder picks one that can't happen by
accident) wipe the token and reboot to the fresh state, showing a new pairing code. The dock has no Bluetooth bond,
so it can be paired again at once. The wearable also clears its bonds and takes a new random Bluetooth address, so
the iPhone sees a new device and asks for the passkey again (unverified; if not, the demo script adds "Forget
Trackie-xxxx in iOS Settings → Bluetooth").

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
- `nearby`, the pin and the follow (5.5–5.7) go over Bluetooth while the wearable is connected, and over HTTP
  otherwise; the wearable's `wake` arrives in an `info` notify instead of a poll.

### 5.6 Send to Trackie (step 5)

- Stop detail gets a "Send to Trackie" button next to the ★, shown only while a paired wearable is online. It sends
  `POST /pin` with that card's stop, line or route, and direction. Tapping cards elsewhere in the app never sends
  anything.
- **The button says what it does (2026-11-17):** a paper plane and the word "Send" under ★ (a bare icon went
  unnoticed); "Pinned" with a pin on the pinned stop (VoiceOver hears it as the button's value); "Sending" or
  "Removing" while a request is out; then for 2 s a check and "Sent" or "Removed", or a ⚠ and "Failed". The plate
  keeps the widest word's width in every state. The word follows Dynamic Type up to AX1; larger sizes show it in
  the long-press viewer.
- On the wearable, the pinned stop becomes the Home card with a ★ marker, ahead of the usual most-urgent rule. It's
  polled at the close stations' rate (every 60 s, spec 6.6) even when it isn't in the nearby list, and it counts as a
  close stop for Hyped (6.3).
- The pin clears on: a swipe on the pinned card (the builder fits the gesture into `docs/handheld-ui.md`), a new pin,
  docking, `DELETE /pin`, a reboot (my call: kept in RAM only), or the end of a follow (5.7).
- **Removing it from the app (Leo, 2026-11-16):** while a stop is pinned, the stop page's Send menu shows "Remove
  from Trackie", and the wearable's device page shows the pinned stop with a Remove button; both send `DELETE /pin`.
  The wearable's `info` carries `pin` (the pinned stop's id, or null), so a pin cleared on the wearable itself (a
  swipe, docking, a passed follow) clears in the app too and isn't sent again on the next reconnect.

### 5.7 Follow a train (step 6)

- The app needs each departure's vehicle: keep `rn` from Train Tracker arrivals and `vid` from Bus Tracker
  predictions on `Prediction` (the data part of `todo.md` F6; the parsers already read both feeds into `Vehicle`).
- In stop detail, a departure tile offers "Follow on Trackie" (the builder picks the gesture: a long press or a small
  button on the open tile). It sends `POST /follow`, which also pins that stop.
- The wearable's Home card counts down to that vehicle, matched by `rn`/`vid` in the predictions it polls for the
  stop: "Red · 4 min · to 95th." When it's due it says so; once it has arrived, "Arrived" shows for 30 s, then the follow and the pin it made clear and
  Home goes back to the closest stops. If the vehicle drops out of the predictions before it's due, the card says the
  train (or bus) was lost for 30 s, then the same: the follow and its pin clear (Leo 2026-11-15; it used to fall
  back to the pin). The app stops re-sending a follow whose departure has passed and forgets its pin too.
  `DELETE /follow` or a new follow replaces it.
- F6's map display (every vehicle on the line in the app's map) isn't part of this design.

### 5.8 Relay with no Wi-Fi (step 7): back

Back 2026-11-14 with Bluetooth first (Leo), shown in the showcase if the gates pass by 11/20 18:00.

- While the app is in the foreground and the wearable is connected over Bluetooth, the app sends `POST /relay` every
  30 s (the time, and the weather if it has it), and `POST /stop` for each of the wearable's stops (the last
  `nearby` plus the pin) as soon as the phone fetches its arrivals, each stop at least every 60 s. A wearable that
  connects or reconnects gets all its stops at once. The relay rides the app's own arrivals fetches, so a stop both
  on screen and on the wrist costs one request.
- Records are base spec 6.2 stop records with the vehicle (`veh`), one per line and direction, keyed as the wearable's
  own cards, so Follow (5.7) runs on relayed data too.
- While relaying, the wearable polls nothing (no CTA, alerts or weather). It resumes its own polling, if it has
  Wi-Fi, 90 s after the last tick. With no Wi-Fi at all, `/relay` sets its clock.
- In the background the app keeps the connection and the status, and relays nothing. Without the phone and without
  Wi-Fi the wearable shows its last data greying out, as it does today.
- The demo shows it with serial `W` (Wi-Fi off until reboot), or F3's switch if F3 lands.

### 5.9 Docking and find my handheld (step 8)

- Docked detection and Trackie asleep are already built (8.4). The wearable reports `docked` in `info`. **Changed
  2026-11-14 (Leo):** docked = USB power and a battery, plus slot 0 joined only if any Wi-Fi network is saved, so a
  wearable on Bluetooth alone still sleeps on the dock.
- **Find my handheld:** when a wearable that was online misses 3 polls in a row (about 6 s) with neither link
  working (Bluetooth disconnected and HTTP not answering), the app saves the
  phone's location and the time. **Changed 2026-11-16 (Settings layout):** the wearable's device page has a "Last
  seen" section, not a Settings screen: a small map with a pin and "Last seen 4:12 PM", or "Connected now" while it's
  online; tapping the map opens Apple Maps there. With location off or no fix it shows "Last seen 4:12 PM · location
  unavailable". A drop found in the first 8 s after the app comes back to the foreground isn't saved: it happened
  while the app wasn't watching, so the phone's place then would be wrong (the earlier pin stays). On the one demo
  network, powering the wearable off with the app open shows it.

### 5.10 Dock touch extras (Leo, 2026-11-16)

Not a showcase step: built after Task 14 with the time it saved, and the first thing dropped if the full run
struggles. Dock only; nothing changes in the app or the messages.

- **The stops ahead.** Tapping an arrival time on a stop card opens a large overlay: the big time, the line and
  destination, then that vehicle's next stops (about 5) with their times. One CTA request on tap: Follow This Train
  (`ttfollow`, by the arrival's run number) for trains, bus predictions by vehicle (`getpredictions?vid=`) for buses.
  Tap anywhere to close; it also closes after 30 s, or when the vehicle drops out. A failed request says so in the
  overlay.
- **The other direction, as a peek.** Tapping a stop card (outside its times) flips it to the other direction for
  30 s, or until tapped again: trains show the station's opposite platform; buses the closest stop on the same route
  going the other way (one request on tap). The saved pick, its LED row and `picks_ver` don't change. Holding a card
  still opens the picker.

## 6. Constraints

- **Time:** about 9 working days. Steps 1–6 and 8 first, in the order of section 10; section 9 slips first, then
  step 7.
- **Internal RAM:** the foundation plan's floor is 40 KB free internal RAM at the worst moment, on both boards. On
  the wearable the worst moment is Bluetooth connected and relaying, Wi-Fi joined, a TLS handshake, and an HTTP
  burst. The gate: ≥ 40 KB in each of three 10-minute cases (Bluetooth connected and relaying; connected, not
  relaying, with TLS polling and an HTTP burst; advertising with Wi-Fi polling), and no leak over 3 Bluetooth off/on
  cycles. W5 measured 67.1 KB connected (idle) before the controller moved to flash.
- **The wearable's battery with Bluetooth:** a power A/B against main in the same session. Bluetooth on with the phone
  away (advertising, Wi-Fi polling) projects ≥ 43.6 h (5% under today's 45.9 h). Relaying projects no worse than Wi-Fi
  polling in the same session, with a floor of 44.7 h. Bluetooth's radio current is modeled from Espressif's
  datasheet (the board has no current sensor); a discharge run per mode is the real check, after the showcase.
- **Latency over Bluetooth:** pin or follow on the wrist ≤ 1 s; a new `nearby` fills its first cards ≤ 10 s;
  relayed cards never older than 60 s while connected; reconnect after a wearable reboot ≤ 10 s; the HTTP fallback
  takes over ≤ 6 s after Bluetooth drops.
- **The dock's frame loop:** all new work runs on the network task (core 0). The dock perf runner
  (`tools/dock_perf_run.py`) must show no worse frames than `docs/perf/summary.md`.
- **The wearable's battery:** the server and the app's 2 s polls keep its radio busier while the app is open. Check
  with the power tools (`docs/power/`) that the dim, app-closed case is unchanged.
- **Toolchain:** both boards on native ESP-IDF 5.4.1 (PlatformIO `framework = espidf`, pioarduino 54.03.20), with
  IDF's cJSON; no Arduino core or library, and no Arduino build or fallback (Leo, 2026-11-14; port plan
  `docs/superpowers/plans/2026-11-14-esp-idf-port.md`). Bluetooth is NimBLE through IDF, on the wearable only. The
  day-1 spike's result (on the old Arduino build) is in `docs/ecosystem/day1-link.md`.
- **Security:** the public repo rules of base spec §12 hold. The token, Wi-Fi passwords and the passkey are never
  logged, never committed, and the app keeps the token in the Keychain. `/wifi` and `/token` are accepted only over
  the bonded Bluetooth link.
- **Docs:** base spec §8.3, §8.4, §9.5, and §10 point here (done 2026-11-14).

### 6.1 The rest of `todo.md`, built in the same window

Leo wants most of the todo list done for the showcase too (2026-11-14): F2 (battery level on the wearable), F3 (a
settings page on the wearable), F4 (a new font on all three), F6's map display, F7 (pixel train and bus symbols on the
iOS map), and the bugs B1 and B2. They're planned separately, but they touch this design in these places:

- **F3, the wearable's settings page,** is where the wearable's side of this design shows: the networks from F1
  (which one it's on, in range or not) and pairing and unpairing (the reset of 5.4). If F3 lands first, the reset
  goes there; if not, it's the hidden gesture and serial command this design specifies (my call). Networks come from
  the app over Bluetooth or `secrets.h` (5.1); F3 never takes a password on the watch. F3 may show "Phone:
  connected" and hold step 7's Wi-Fi off switch.
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
| Bluetooth doesn't fit in internal RAM next to Wi-Fi and TLS | **Happened** on the Arduino build (day 1: about 50 KB on both boards). On ESP-IDF it fits on the wearable (W5: 67.1 KB connected, before the controller moved to flash). If the RAM gate fails after the plan's ladder (11/18), Bluetooth carries status and messages but not the relay, Wi-Fi is required again, and step 7 stays cut. |
| Bluetooth is unstable on the wearable: drops, or Wi-Fi/Bluetooth coexistence stalls TLS | The wearable's whole Wi-Fi path stays built and tested: the HTTP link, its own polling, and pairing by code (4.3). If Bluetooth isn't stable by 11/19 12:00, the wearable stops starting it, and the app's Bluetooth is switched off by a flag; nothing new has to be built. |
| iOS background limits | The relay runs only with the app in the foreground (Auto-Lock off on the demo iPhone). In the background the connection and status stay; a force-quit app is never relaunched (base spec 9.5), and the wearable falls back to its own Wi-Fi polling if it has Wi-Fi. |
| Re-pairing after a reset: iOS can't remove a bond from an app, and a wearable that forgot its bond gets refused | A reset gives the wearable a new random Bluetooth address, so the iPhone sees a new device (unverified). If that fails, the demo script adds "Forget Trackie-xxxx in iOS Settings → Bluetooth"; pairing by code is the last resort. |
| The phone can't reach the boards on a network | The wearable no longer needs it: Bluetooth doesn't go through the router. The dock still does. **Happened at home** on day 1: the Mac and both boards reached each other, but the iPhone's connections never reached either board, likely the router keeping the phone apart from the 2.4 GHz boards. Campus is checked on campus or at the 11/22 dry run; if it fails there too, the phone's hotspot is the demo network (compiled into both boards' `secrets.h` lists). |
| The showcase network keeps the phone and the dock apart, or refuses a board | The showcase runs on the lab network checked on 11/16 (Leo, 2026-11-16; no hotspot; names stay out of the repo). There the phone reached the dock and paired it by code, but the access point refused the wearable at authentication with either MAC: register the wearable's MAC with the network's admin, or the wearable shows Wi-Fi-free steps only (it needs Wi-Fi only for the HTTP fallback). Home still keeps the phone and the boards apart (day 1). |
| iOS local network permission is denied | The app explains it and links to Settings, the way it handles location. |
| A device reboots mid-demo | Everything the app sent that must survive is in flash (token, picks, pet, settings, nearby); the pin and follow are re-sent by the app when it sees the device come back (my call). |

## 8. Decided by Leo vs. left to the builder

**Leo decided (2026-11-14):** the 8 steps and the F1/F5 fold-in; approach B; one network for the whole showcase;
"away" features needn't fully work; no big builds; Trackie's look follows the app; pairing shown in the app for
both devices; tapping a stop sends it to the wearable (as "Send to Trackie" and "Follow"); the wearable mirrors the
stops on the phone's list while the app is open.

Also: one wearable in the demo, with its battery in and docked on the dock's plug; most of `todo.md` done in the same
window (6.1). After the day-1 spike: no Bluetooth for the showcase, pairing over the network with the code on screen
(plan Appendix A), Wi-Fi from `secrets.h`, step 7 dropped, Bluetooth as future work. After the ESP-IDF port:
Bluetooth first for the wearable with the relay, the dock Wi-Fi only; docked on power and a battery when no network
is saved; step 7 back if its gates pass by 11/20 18:00; a new Bluetooth address on reset, with Forget as the
fallback; the plan's five defaults (no polling while relaying, one GATT service, Wi-Fi over Bluetooth, foreground
relay, the power gate); and the wearable keeps its full Wi-Fi path, pairing by code included, as a real fallback.

**My calls, marked above:** the 2 s `info` poll; the pairing token; the extra `info` fields; the pin kept in RAM
only; device settings riding along; the reset paths; re-sending pin and follow after a reboot; the mirror's 10 s
limit.

**Left to the builder:** HTTP server and mDNS libraries, the exact JSON field
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
   *Done 2026-11-14:* the fallback (4.1); home failed the phone check; the showcase network is the lab network (Leo, 2026-11-16), checked on 11/16: phone ↔ dock and pairing work; the wearable is refused
   (`docs/ecosystem/day1-link.md`).
2. The network layer on both boards (server, mDNS, token, `info`, `config`) and the app's device discovery, Devices
   list, and setup flow (step 1).
3. Steps 2 and 3 (pet and settings, dock stops both ways).
4. Steps 4, 5, 6 (nearby, pin, follow), with the `rn`/`vid` data in CTAKit.
5. Step 8 (find my handheld) and the reset paths.
6. The wearable's Bluetooth link, setup and relay (step 7), in the order of the Bluetooth plan. Its pairing by code
   lands first, so the Wi-Fi fallback is whole before any Bluetooth code goes in.
7. F1 and F5 polish, then section 9.
8. A full dry run of section 2 on the hardware, on one network, at least twice in a row.

## 11. Testing

- **Shared C++ library (native, Unity):** Bluetooth frames and the envelope; parsing every new body (pin, follow,
  pair, relay tick, relayed stop, the new `info` fields, trip),
  including malformed ones that must leave the old state alone, as `parseNearby` and `decodePicks` do; the pairing
  rules (right code once, open until paired, a 30 s cooldown and a new code after 5 wrong tries, reset on Pair again).
- **CTAKit (Swift Testing):** the same frame vectors as the C++ tests; encoders for every new body, the `rn`/`vid` on `Prediction` from the shared fixtures,
  and the token never appearing in any description or log string (as `WifiMessage` does for passwords).
- **On the Mac:** a small script (or `curl` lines in the READMEs) that drives each endpoint, so the firmware can be
  tested before the app side exists.
- **On hardware:** the section 2 run (over Bluetooth, and with the wearable on the HTTP fallback), the dock perf
  runner, the wearable's RAM, power and latency gates (section 6), and a reset → pair → demo cycle, by passkey and by
  code.

## 12. Bluetooth: built for the wearable

Was "Future work: Bluetooth" (dropped for the showcase on the day-1 spike, 2026-11-14). Back the same day after the
ESP-IDF port (Leo): the wearable is Bluetooth first, with the relay, and the dock stays Wi-Fi only. The design is in
sections 4–6 above; the GATT, frames, envelope, messages, status, relay, iOS background behaviour, gates and tasks are
in `docs/superpowers/plans/2026-11-14-wearable-ble-link.md`.

What changed from the old future-work note: Bluetooth runs all the time on the wearable (advertising whenever no phone
is connected), not only during setup or for 2 minutes after `POST /ble`, which is gone. The base spec's `wifi`,
`config`, `request`, `nearby` and `snapshot` characteristics become paths on one `link` characteristic. The relay
sends whole stops (`POST /stop`) every 30–60 s in the foreground, not one `snapshot` per card. Leo's earlier "for a
real version I'd likely use a more powerful device" still stands for after the showcase.
