"""Grabs the frame on the dock's screen over serial ("f" command) and saves it as a PNG, turned upright (portrait).

    python3 tools/frame_grab.py <serial port> <out.png> [--ccw]

Uses pyserial (PlatformIO's Python has it: ~/.platformio/penv/bin/python). --ccw when DISPLAY_ROTATE_CW is 0.
"""
import struct
import sys
import time
import zlib

import serial


def png(path, w, h, rgb):
    raw = b"".join(b"\x00" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main():
    port, out = sys.argv[1], sys.argv[2]
    cw = "--ccw" not in sys.argv
    s = serial.Serial(port, 115200, timeout=1)
    s.reset_input_buffer()
    s.write(b"f")
    pw = ph = None
    px = None
    lost = 0
    t0 = time.time()
    while time.time() - t0 < 60:
        line = s.readline().decode("ascii", "replace").strip()
        if line.startswith("FRAME "):
            pw, ph = map(int, line.split()[1:3])
            px = [0xF81F] * (pw * ph)   # magenta where a line got lost
        elif line.startswith("F:") and px is not None:
            try:
                _, at, hexs = line.split(":")
                at = int(at, 16)
                for k in range(0, len(hexs) - 7, 8):
                    run, c = int(hexs[k:k + 4], 16), int(hexs[k + 4:k + 8], 16)
                    px[at:at + run] = [c] * run
                    at += run
            except ValueError:
                lost += 1
        elif line == "END" and px is not None:
            break
    if px is None:
        sys.exit("frame_grab: no frame came")
    if lost:
        print(f"frame_grab: {lost} garbled lines (magenta)")
    w, h = ph, pw   # portrait
    rgb = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            X, Y = (y, ph - 1 - x) if cw else (pw - 1 - y, x)   # bands.h's mapping
            c = px[Y * pw + X]
            i = (y * w + x) * 3
            rgb[i], rgb[i + 1], rgb[i + 2] = (c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3
    png(out, w, h, rgb)
    print(f"frame_grab: {len(px)} pixels -> {out}")


if __name__ == "__main__":
    main()
