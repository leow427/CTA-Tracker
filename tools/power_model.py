"""The handheld's power model (power analysis spec, section 4): estimated current from one power-log sample.

The firmware logs raw facts once a second (`P,` lines). This turns each into milliamps per part, using the numbers
in power_model.json, so improving a number re-scores every past run the same way. Every result is an estimate.

    from power_model import load, estimate
    model = load()
    parts = estimate(model, row)   # row: dict of column name -> string, from a P line
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PARTS = ["cpu", "screen", "wifi", "ble", "memory", "fixed"]


def load(path=None):
    with open(path or os.path.join(HERE, "power_model.json")) as f:
        return json.load(f)


def num(row, key, default=0.0):
    v = row.get(key, "")
    try:
        return float(v) if v != "" else default
    except ValueError:
        return default


def nearest_key(table, mhz):
    keys = sorted(int(k) for k in table)
    best = min(keys, key=lambda k: abs(k - mhz))
    return table[str(best)]


CLOCKS = ["80", "160", "240"]   # MHz, as in power_model.json's cpu tables


def cpu_ma(model, row):
    """The CPU's milliamps for one sample.

    Since model-v2 the firmware can change the clock within a second (the dim screen runs at 80 MHz, requests and
    anything the user can see at 240), so a sample logs the ms spent at each clock (clk_ms_*) and the busy core-ms at
    each (clk_busy_*, both cores together), and each part is costed at its own clock. Older runs have no such
    columns and ran one clock all second, read from the cpu_mhz snapshot.
    """
    cpu_m = model["cpu"]
    t = {f: num(row, "clk_ms_" + f) for f in CLOCKS}
    span = sum(t.values())
    if span > 0:
        return sum(t[f] * cpu_m["idle_ma"][f] + num(row, "clk_busy_" + f) * cpu_m["per_busy_core_ma"][f]
                   for f in CLOCKS) / span
    mhz = num(row, "cpu_mhz", 240)
    busy = (num(row, "busy0") + num(row, "busy1")) / 100.0   # busy cores, 0-2
    return nearest_key(cpu_m["idle_ma"], mhz) + busy * nearest_key(cpu_m["per_busy_core_ma"], mhz)


def estimate(model, row):
    """Milliamps per part for one sample (a dict of the P line's columns)."""
    cpu = cpu_ma(model, row)

    scr = model["screen"]
    lit = num(row, "lit", -1)
    if lit < 0:
        lit = row.get("_lit_carry", 0.0)   # no frame this second: the screen still shows the last one
    bright = num(row, "bright") / 255.0
    screen = (scr["driver_on_ma"] + scr["full_white_full_brightness_ma"] * lit * bright
              + scr["send_ma_per_mpx_per_s"] * num(row, "px_sent") / 1e6)

    w = model["wifi"]
    mode, status = num(row, "wifi_mode", 0), num(row, "wifi_status", -1)
    if mode == 0:
        wifi = w["off_ma"]
    elif status != 3:   # WL_CONNECTED
        wifi = w["joining_ma"]
    else:
        ps = str(int(num(row, "wifi_ps", 1)))
        idle = w["connected_idle_ma_by_ps"].get(ps, w["connected_idle_ma_by_ps"]["1"])
        active_s = min(1.0, num(row, "req_ms") / 1000.0)
        rssi = num(row, "rssi", w["request_rssi_ref_dbm"])
        weak = max(0.0, w["request_rssi_ref_dbm"] - rssi) * w["request_extra_per_db_below_ref"]
        wifi = idle * (1 - active_s) + active_s * w["request_active_ma"] * (1 + weak)

    ble = 0.0
    if row.get("ble", "") == "adv":
        ble = model["ble"]["advertising_ma"]
    elif row.get("ble", "") == "conn":
        ble = model["ble"]["connected_ma"]

    return {"cpu": cpu, "screen": screen, "wifi": wifi, "ble": ble,
            "memory": model["psram_flash"]["ma"], "fixed": model["fixed"]["ma"]}


def total(model, parts):
    return sum(parts.values()) * model["battery"]["rail_to_battery"]
