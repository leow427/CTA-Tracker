# LED bridge

A fallback for the dock's route LED board. If the dock's own wire to the board doesn't work, a spare ESP32-S3 dev
board sits by the LED board, joins the dock's network (see Networks), finds the dock by mDNS (`_ctatracker._tcp`
with `type=dock`), reads `GET /leds` every 500 ms and drives the 21 LEDs itself. It uses the dock's LED driver settings and
`cta::ledFrame` at 25 Hz, so the rows look the same: LED 7 breathes, and a row goes dark by itself once its data is
stale (also when the dock stops answering).

`GET /leds` answers one line, `cta::formatLedState`'s: the dock's Unix time, night (0/1), then mask, color, positions'
time and due for each of the 3 rows. No token: it's read-only.

## Wiring

- S3 GPIO 4 → the LED board's DATA screw (`LED_DATA_PIN` in `src/main.cpp` to change it).
- S3 GND → the GND screw beside it.
- S3 5V pin ← the LED board's 5 V (the board's supply powers the S3).

The LED board's SN74AHCT125 takes the S3's 3.3 V data.

## Build and flash

Any ESP32-S3 board: the build assumes 4 MB flash and no PSRAM. It needs `firmware/secrets.h` (git-ignored; template
`firmware/secrets.example.h`) and uses `WIFI_MAC` and `LEDBRIDGE_OTA_KEY` too if they're set.

## Networks

The bridge must share a network with the dock. It knows two from `secrets.h`, skipping one whose name is empty:

1. the dock's, `DOCK_WIFI_SSID` / `DOCK_WIFI_PASS` (tried first);
2. the handheld's, `WIFI_SSID` / `WIFI_PASS`, joined as WPA2-Personal (`WIFI_EAP_USER` is ignored).

It scans and joins the first of the two in sight. On a drop, or after 60 s on a network without finding the dock, it
moves to the other one if it's in sight. Serial names the pair ("dock net" or "handheld net"), never the network.

```bash
cd firmware/ledbridge
~/.platformio/penv/bin/pio device list            # a native-USB S3 is USB 303A:1001
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbmodem<the S3's>
```

- Always pass `--upload-port`, found by VID:PID: the wearable is also a native-USB S3 (303A:1001), and the dock is a
  CH343 (1A86:55D3). Unplug the wearable first if both show 303A:1001.
- A board with a USB-UART chip shows that chip's VID:PID instead (CP2102 10C4:EA60, CH340 1A86:7523).
- Build output goes to `~/.platformio-ledbridge/build` (ESP-IDF refuses a path with a space). It shares the dock's
  PlatformIO folder, `~/.platformio-dock`, so don't build both at once.
- Serial at 115200 on the same port: the IDF console is on UART0 and USB-Serial-JTAG.

## Updating over Wi-Fi (OTA)

With `LEDBRIDGE_OTA_KEY` set in `firmware/secrets.h`, the bridge takes a new `firmware.bin` on `POST /ota` (port 80)
with the key in an `X-OTA-Key` header; without it, OTA is off. Build, then send the image:

```bash
curl --data-binary @$HOME/.platformio-ledbridge/build/ledbridge/firmware.bin -H "X-OTA-Key: <key>" http://<bridge-ip>/ota
```

- The bridge's IP is in its serial log (`ledbridge: Wi-Fi joined (…), IP …`); `ledbridge.local` works too where mDNS
  does (`ping ledbridge.local`).
- It writes the other of two app slots (`partitions.csv`), reboots into it and logs the running version at boot. A
  new image keeps itself once it has joined Wi-Fi; one that reboots before that rolls back to the old one. A new
  image that hangs without rebooting doesn't roll back: flash it over USB.
- The first flash with this layout must be over USB.

## What boot should look like

1. Self-test, before Wi-Fi: each LED lights dim white in chain order, row 1 LED 1 to row 3 LED 7, about 50 ms each
   (about 1 s in all), then all go off. A gap or a wrong order is a wiring or chain fault.
2. Serial: `ledbridge: running <version>`, `ledbridge: joining the dock net`, `ledbridge: Wi-Fi joined (dock net),
   IP <ip>`, `ledbridge: dock found at <ip>`, `ledbridge: first good frame`. The rows then follow the dock.
3. `ledbridge: fetch failed (N in a row)` at most every 10 s while the dock doesn't answer; after about 3 s of
   failures the bridge asks mDNS for the dock again.
