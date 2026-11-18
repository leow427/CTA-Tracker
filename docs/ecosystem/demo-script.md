# Showcase demo script (draft, 2026-11-17)

The steps in ecosystem spec §2, in order, with the exact taps. Draft: run it end to end on 11/19 (first full run)
and twice on 11/22 (dry run), and fix this file after each run.

**Setup for the day (Leo, 2026-11-17):** Trackie runs on **Bluetooth only**. The showcase network refuses its Wi-Fi,
so the phone fetches the CTA data and relays Trackie's cards. The dock is on the showcase network by itself. The
phone is on the showcase network (for the dock) and has Bluetooth on (for Trackie).

## Before the audience arrives

1. **Charge.** Trackie with its battery fitted, sitting on the dock's charging plug. Phone charged, Low Power Mode off.
2. **Phone:** Bluetooth on, location on (While Using), on the showcase network. Open the app once and check cards
   show live times (if "CTA data unavailable": the build is missing `Secrets.plist`; reinstall from main).
3. **Reset both devices** so pairing can be shown fresh (spec §5.4):
   - **Dock:** its Settings screen → **Pair again**. It shows a new 6-digit code.
   - **Trackie:** its Settings screen → **Pair again**. This also forgets its Bluetooth bond.
   - **App:** Settings → each device → **Forget**.
   - **iPhone:** Settings → Bluetooth → forget **Trackie-xxxx** (also an older **CTA-Pet-xxxx** entry if one is
     still listed).
4. **Lift Trackie off the dock** and keep it nearby (within a few metres of the phone the whole demo).
5. Leave the app open on the map near the showcase location.

## The demo

### 1. Pairing
- **Dock:** in the app, a "Dock found · Set up" card slides up. Tap **Set up**, type the 6-digit code from the
  dock's screen. The dock shows paired; the app lists it as online via Wi-Fi.
- **Trackie:** a "Trackie found · Set up" card slides up (Trackie shows its Pair screen after the reset). Tap
  **Set up**; on first use the app says "Bluetooth finds your dock and Trackie." and iOS asks for Bluetooth. iOS asks
  for the passkey shown on Trackie: type it. Name Trackie (or keep "Trackie").
- **Skip** the Wi-Fi step (Bluetooth only today). If a network is tried, the app says why it failed ("Trackie
  couldn't get in…"); that's expected on the showcase network.
- *If it fails:* dock code rejected → check the phone is on the showcase network, tap Pair again on the dock for a
  new code. Trackie not found → forget it in iOS Settings → Bluetooth and Pair again on Trackie.

### 2. One Trackie
- App: Settings → **Trackie** → change the color (e.g. Teal) and the name. Trackie changes on the dock and the wrist.

### 3. Dock stops both ways
- On the **dock**, tap a section and pick a stop: it shows in the app (Settings → the dock → Stops) within a poll.
- In the **app**, on a stop page, **Send** → **Send to dock** (or the dock's page → a section): the dock's section and
  its LED row change.
- Dock extras: tap a time tile to see the stops ahead; tap a stop name to peek at the other direction.

### 4. Nearby on the wrist
- Wake Trackie: it fills with the nearby stops from the phone.
- Pan the app's map somewhere else: Trackie shows the same stops as the phone's list.

### App tour (new 2026-11-17, fits after step 4)
- **Search:** type "clark belmont" (no "&" needed), then "belmnt" ("Showing close matches"), then "wrigley field":
  the map flies there and lists the stops near it. Recenter to come back.
- **Live vehicles:** open a Red Line card. Every Red Line train shows on the map as a round token. Tap a departure
  tile: the map flies to that train and the page says where it is ("Between … and …, N stops away"). Tap it again to
  clear.

### 5. Send to Trackie
- On a stop page, tap **Send** (beside ★). It reads Sending → Sent → **Pinned**; Trackie's Home card shows the stop.
- To clear: **Send** → Remove from Trackie, or swipe the card away on Trackie.

### 6. Follow a train
- On a stop page, long-press a departure tile → **Follow on Trackie**. Trackie counts down to that train
  ("Red · 4 min · to 95th"), then "Arrived".

### 7. No Wi-Fi
- Already the case today: Trackie has no Wi-Fi, and every card on it came from the phone over Bluetooth. Say so here.
- Show it: Trackie's Settings shows Wi-Fi not connected while its cards stay live.
- *Gate:* the 11/20 18:00 go/no-go (spec §6). If it fails, Trackie still works over Bluetooth for steps 4–6.

### 8. Docking
- Set Trackie on the dock's charging plug: Trackie goes to sleep (Docked).
- App: Settings → Trackie → **Last seen** shows "Connected now" while linked; after it drops off both links, the map
  pin of where the phone was.
- Battery: Trackie shows its percent (charging while docked).

## If something goes wrong
- **A card shows old times:** wait one poll (about 30 s); the app and dock retry on their own.
- **Trackie stops updating:** bring the phone closer; reopen the app (the relay runs while the app is open).
- **The dock shows offline in the app:** check the phone is on the showcase network; the dock rejoins on its own.
- **Anything stuck:** reset that device (Pair again) and pair it again; it takes under a minute.
