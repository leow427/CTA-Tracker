"""Downloads the OpenStreetMap data behind the pixel map (tools/basemap_build.py's input) into data/osm/ (git-ignored).

It is BBBike's ready-made Chicago extract (OSM XML, gzip, about 235 MB; it covers 41.64-42.09 N, 88.01-87.40 W, the
whole CTA area), which the public Overpass servers can't serve in one go. Kept when already there; delete it to fetch
a newer one.

Run from the repo root: python3 tools/basemap_fetch.py
"""
import os
import shutil
import urllib.request

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "data", "osm", "Chicago.osm.gz")
URL = "https://download.bbbike.org/osm/bbbike/Chicago/Chicago.osm.gz"


def main():
    if os.path.exists(OUT):
        print(f"basemap_fetch: {os.path.relpath(OUT, ROOT)} is already there")
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    req = urllib.request.Request(URL, headers={"User-Agent": "basemap-build/1.0"})
    with urllib.request.urlopen(req, timeout=60) as r, open(OUT + ".part", "wb") as fh:
        shutil.copyfileobj(r, fh, 1 << 20)
    os.replace(OUT + ".part", OUT)
    print(f"basemap_fetch: wrote {os.path.relpath(OUT, ROOT)} ({os.path.getsize(OUT) / 1e6:.0f} MB)")


if __name__ == "__main__":
    main()
