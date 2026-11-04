> **Superseded.** These are the original dock notes from 2026-10-21. Where they disagree with `docs/superpowers/specs/2026-11-03-cta-tracker-design.md`, the spec wins.

# Transit dock: design notes

Oct 21, 2026 · @Leo Walsh

## Concept

The dock is the system's home base: it shows when to leave for your saved routes, charges the wearables, and knows who is home.

- **Where it fits:** the wearable (Waveshare ESP32-S3 1.75" AMOLED), the no-login phone app, and this dock make up the system.
- **Works in any city:** the face shows your saved routes, not a city map, so one design works anywhere, for buses and trains.
- **Hidden until lit:** the whole front is smoked acrylic, so the screen and LEDs disappear when they're off.
- **Prototype vs product:** the prototype board is milled on the class Carvera; the product board is ordered from a fab.

## Front face layout

The screen tells you when to leave, the strips show where your rides are, and the bays show who's home.

&#91;embedded content: dock front · screen, 3 approach strips, 2 bays\]

- **Screen (left):** a dead-front color OLED with the pixel train character, whose mood follows delays, early arrivals, cancellations, and weather, plus the time-to-leave countdown and next departure.
- **Approach strips (right):** one gold trace per saved route, each a row of LEDs ending at a pad for your stop. A lit LED is a vehicle that steps toward your stop as it gets closer, and its color shows the line.
- **Whole-strip effects:** a strip can show mood too, like pulsing amber for a delay.
- **City edition (optional):** a full city transit map as an aesthetic faceplate, separate from the everyday design.
- **Bays (bottom):** one per wearable; see charging bays below.

## Dead-front display

The screen sits behind dark acrylic, so it's invisible when off and its lit pixels glow through when on.

&#91;embedded content: dead-front stack-up · acrylic, PCB, OLED\]

- **Why OLED:** black OLED pixels emit no light, so they vanish behind the tint. An LCD's backlight leaks through as a faint glowing rectangle and ruins the effect.
- **Why smoked acrylic:** it's transparent, just dark, so text stays sharp. Car dashboards and smart-home gadgets hide their screens the same way.
- **The window:** the OLED sits in a window cut through the PCB and presses flush against the back of the acrylic.
- **LED glow-through:** route LEDs mount on the back of the board and shine through bare FR4, so the front shows only traces until a vehicle lights up.

## PCB

One board design, made two ways: milled on the Carvera for the prototype, ordered from a fab for the product.

|  | Prototype | Production |
| --- | --- | --- |
| Made by | Milled on the class Carvera | Ordered from a PCB fab |
| Front finish | Raw copper traces, clear-coated so they don't tarnish | Black solder mask with exposed gold (ENIG) traces |
| LED glow-through | Free: bare FR4 everywhere copper was milled away | Leave small spots with no mask or copper over each LED |
| Look | Handmade, warm copper | Finished product |

**Design rules that work for both:** wide traces and clearances, as few vias as possible, and 0805 or larger parts.

**What's on the board:**

- Its own ESP32-S3 module, not a second Waveshare board, so the PCB does real work
- USB-C power input and a programming connection
- Data lines for the route LEDs, mounted on the back so they glow through the board
- A window cut through the board for the OLED, plus its connector
- Charging bay contacts and a status LED for each bay

## Charging bays and presence

Each wearable gets its own bay along the front edge, and a docked wearable doubles as a "home" signal for the partner feature, no GPS needed.

- Each bay has a charge-status LED and detects whether a wearable is docked.
- Docked means home; an empty bay means away.
- The number of bays matches the number of wearables in the household.

| Connector | Pros | Cons |
| --- | --- | --- |
| Upright male USB-C on the dock | Uses the module's existing USB-C port; keeps data access for flashing and syncing | Wearable must be placed precisely; the plug takes leverage |
| Magnets + pogo pins | Forgiving drop-in; no stress on a connector | Must tap the module's 5V pads (check the schematic); needs a tiny contact PCB inside the wearable |

## Enclosure and materials

A laser-cut smoked acrylic front over the whole face, with a 3D-printed body built around it.

- **Front:** smoked acrylic, 2–3 mm, cut on the IRL laser cutter. It's transparent, just dark, so text stays sharp and the gold traces show faintly through.
- **Body:** 3D printed, holding the acrylic, the PCB, and the bays.
- **Tint:** the tint percentage decides how visible the traces are and how bright the screen needs to be. Still to pick.

**Fallback: a 3D-printed window instead of acrylic.** Printed plastic diffuses light into a soft, milky glow rather than crisp text. To make it work:

- Print the window area just 2 layers thick (about 0.4 mm) in white or natural filament.
- Press the screen flat against the plastic; even a 1 mm air gap blurs text.
- Use chunky pixel fonts and big icons. The train character works well; fine text like departure times is borderline.

## Starting parts list

A first pass at the dock's parts; quantities depend on the open questions below.

| Part | Choice | Notes |
| --- | --- | --- |
| Controller | ESP32-S3 module | Soldered to your own PCB |
| Screen | 1.5" color OLED, SSD1351, 128×128, SPI | OLED so black pixels vanish behind the tint; an LCD's backlight would show |
| Route LEDs | Addressable RGB LEDs | Mounted on the back of the board; color shows the line |
| Front panel | Smoked acrylic, 2–3 mm | Laser cut at IRL; tint still to pick |
| PCB | Milled FR4 (prototype), fab board with black mask + ENIG (production) | Same design for both |
| Power | USB-C input | Feeds the board and the bays |
| Bay connectors | Upright USB-C, or magnets + pogo pins | See charging bays |
| Body | 3D printed | Holds the acrylic, PCB, and bays |

## Open questions and decisions

- [ ] How many routes on the face: 3 or 5? This sets the board size and whether the screen sits beside or above the strips.
- [ ] How many minutes does a strip cover from end to end?
- [ ] How many charging bays?
- [ ] Bay connector: upright USB-C or magnets + pogo pins?
- [ ] Which smoked acrylic tint?
- [ ] Where does the dock get departure data for your city, and does it fetch it over home Wi-Fi itself?
- [ ] Partner sharing without logins: host the relay on the homelab, pair by scanning a QR code on the dock?
- [ ] Wearable: GPS version or not? The phone can supply location over BLE, which saves cost and battery.
