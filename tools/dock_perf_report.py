"""Turn dock performance runs into per-phase tables and an HTML report (dock performance analysis spec, sections 4-6).

Runs are grouped by their change ID (the `# change:` header), in the order given; the first group is the reference
each later group is compared with. Within a group, the spread (max - min) of the runs' phase values is the noise. A
difference counts only beyond twice the reference's noise (or, with one reference run, a fixed floor per metric).

    python3 tools/dock_perf_report.py docs/perf/runs/*-baseline-*.csv --out docs/perf/reports/baseline.html
    python3 tools/dock_perf_report.py docs/perf/runs/*-baseline-*.csv docs/perf/runs/*-r1-*.csv --out ...

Prints a Markdown table per group, and a comparison table per later group (for docs/perf/log.md).
"""
import argparse
import html
import os
import statistics
import sys

SCORED = [1, 2, 3, 4, 5, 6, 7]
PHASES = {1: "Idle", 2: "Screen churn", 3: "Map pan", 4: "Picker scroll", 5: "Network burst", 6: "Mixed", 7: "Cool-down"}
DROP_MS = 10000   # the first 10 s of each phase is not scored

# key: (label, unit, better: -1 lower / +1 higher / 0 neither, floor: the smallest difference that counts with no noise)
METRICS = {
    "temp": ("Chip temp", "°C", -1, 1.0),
    "busy0": ("Core 0 busy", "%", -1, 3.0),
    "busy1": ("Core 1 busy", "%", -1, 3.0),
    "fps": ("Frames/s", "", 0, 0),
    "move_fps": ("FPS while dragging", "", 1, 1.0),
    "frame": ("Frame time", "ms", -1, 2.0),
    "draw": ("  draw", "ms", -1, 2.0),
    "copy": ("  copy", "ms", -1, 1.0),
    "wait": ("  wait for panel", "ms", -1, 2.0),
    "bands": ("Bands copied/frame", "", 0, 0),
    "slow": ("Slowest frame (avg/s)", "ms", -1, 5.0),
    "slow_max": ("Slowest frame (max)", "ms", -1, 15.0),
    "tap": ("Tap to screen", "ms", -1, 5.0),
    "tap_max": ("Tap to screen (max)", "ms", -1, 20.0),
    "drag": ("Drag to screen", "ms", -1, 5.0),
    "drag_max": ("Drag to screen (max)", "ms", -1, 20.0),
    "list": ("Picker list load", "ms", -1, 100.0),
    "req": ("Request (ok)", "ms", -1, 100.0),
    "req_fail": ("Failed requests", "", -1, 1.0),
    "late": ("Late panel frames", "", -1, 1.0),
    "recover": ("Panel restarts", "", -1, 1.0),
    "int_min": ("Lowest free internal RAM", "KB", 1, 2.0),
    "age": ("Data age", "s", -1, 5.0),
}
BOOT = {"wifi": "Wi-Fi up", "clock": "Clock set", "loaded": "Every stop loaded"}


def num(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def read_run(path):
    meta, cols, rows, events = {}, None, [], []
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("# ") and ": " in line:
                k, v = line[2:].split(": ", 1)
                meta[k] = v
            elif line.startswith("H,"):
                cols = line[2:].split(",")
            # Another task's serial print can land inside a perf line ("E,5[23956,req_5start,w"): a line that isn't
            # whole is skipped.
            elif line.startswith("P,") and cols:
                vals = line[2:].split(",")
                if len(vals) == len(cols) and vals[0].isdigit():
                    rows.append(dict(zip(cols, vals)))
            elif line.startswith("E,"):
                parts = line[2:].split(",")
                if parts[0].isdigit() and len(parts) > 1:
                    events.append((int(parts[0]), parts[1:]))
    starts, boot = {}, {}
    for ms, ev in events:
        if ev[0] == "phase":
            starts[int(ev[1])] = ms
        elif ev[0] == "boot" and len(ev) > 1 and "=" in ev[1]:
            for kv in ev[1:]:
                k, v = kv.split("=")
                boot[k] = int(v) / 1000 if int(v) else None
    order = sorted(starts)
    ends = {p: (starts[order[i + 1]] if i + 1 < len(order) else 1 << 62) for i, p in enumerate(order)}
    return {"path": path, "name": os.path.basename(path), "meta": meta, "rows": rows, "events": events,
            "starts": starts, "ends": ends, "boot": boot}


def window(run, p):
    if p not in run["starts"]:
        return None
    return run["starts"][p] + DROP_MS, run["ends"][p]


def phase_metrics(run, p):
    w = window(run, p)
    if not w:
        return {}
    lo, hi = w
    rows = [r for r in run["rows"] if lo <= int(r["ms"]) < hi and int(r["phase"]) == p]
    if not rows:
        return {}
    col = lambda k: [x for x in (num(r.get(k)) for r in rows) if x is not None]  # noqa: E731
    mean = lambda xs: statistics.fmean(xs) if xs else None  # noqa: E731
    m = {"temp": mean(col("cpu_c")), "busy0": mean(col("busy0")), "busy1": mean(col("busy1")), "fps": mean(col("frames"))}
    frames = sum(col("frames"))
    if frames:
        d, c, wt = sum(col("draw_ms")), sum(col("copy_ms")), sum(col("wait_ms"))
        m.update(frame=(d + c + wt) / frames, draw=d / frames, copy=c / frames, wait=wt / frames,
                 bands=sum(col("bands")) / frames)
    m["move_fps"] = mean(col("move_fps"))
    m["slow"] = mean(col("slow_frame_ms"))
    m["slow_max"] = max(col("slow_frame_ms")) if col("slow_frame_ms") else None
    m["drag"] = mean(col("drag_ms"))
    m["drag_max"] = max(col("drag_max_ms")) if col("drag_max_ms") else None
    m["late"] = sum(col("scan_late"))
    rec = col("recoveries")
    m["recover"] = rec[-1] - rec[0] if rec else None
    m["int_min"] = min(col("int_min")) / 1024 if col("int_min") else None
    m["age"] = mean(col("data_age"))
    taps, lists, reqs, fails = [], [], [], 0
    for ms, ev in run["events"]:
        if not (lo <= ms < hi):
            continue
        if ev[0] == "tap":
            taps.append(int(ev[1]))
        elif ev[0] == "list":
            lists.append(int(ev[1]))
        elif ev[0] == "req_end" and len(ev) >= 5:
            if int(ev[2]) == 200:
                reqs.append(int(ev[4]))
            else:
                fails += 1
    m["tap"] = mean(taps)
    m["tap_max"] = max(taps) if taps else None
    m["list"] = mean(lists)
    m["req"] = mean(reqs)
    m["req_fail"] = fails
    m["_n"] = {"tap": len(taps), "list": len(lists), "req": len(reqs) + fails}
    return m


def group_runs(paths):
    groups = []
    for p in paths:
        r = read_run(p)
        ch = r["meta"].get("change", "?")
        for g in groups:
            if g["change"] == ch:
                g["runs"].append(r)
                break
        else:
            groups.append({"change": ch, "runs": [r]})
    for g in groups:
        g["per"] = {p: [phase_metrics(r, p) for r in g["runs"]] for p in SCORED}
        g["agg"], g["noise"] = {}, {}
        for p in SCORED:
            g["agg"][p], g["noise"][p] = {}, {}
            for k in METRICS:
                vals = [m[k] for m in g["per"][p] if m.get(k) is not None]
                g["agg"][p][k] = statistics.fmean(vals) if vals else None
                g["noise"][p][k] = (max(vals) - min(vals)) if len(vals) >= 2 else None
        g["boot"] = {}
        for k in BOOT:
            vals = [r["boot"][k] for r in g["runs"] if r["boot"].get(k)]
            g["boot"][k] = statistics.fmean(vals) if vals else None
    return groups


def fmt(v, k=None):
    if v is None:
        return "–"
    if k == "int_min":
        return f"{v:.0f}"
    return f"{v:.0f}" if abs(v) >= 100 else f"{v:.1f}"


def verdict(ref, g, p, k):
    """'better', 'worse', or '' (within noise), and the threshold used."""
    a, b = ref["agg"][p].get(k), g["agg"][p].get(k)
    sign, floor = METRICS[k][2], METRICS[k][3]
    if a is None or b is None or sign == 0:
        return "", None
    n = ref["noise"][p].get(k)
    thr = max(2 * n, floor) if n is not None else floor
    d = (b - a) * sign
    if d > thr:
        return "better", thr
    if d < -thr:
        return "worse", thr
    return "", thr


def table(g):
    cols = [("temp", "Temp °C"), ("busy0", "Busy c0 %"), ("busy1", "Busy c1 %"), ("fps", "Frames/s"),
            ("move_fps", "FPS dragging"), ("frame", "Frame ms"), ("draw", "draw"), ("copy", "copy"), ("wait", "wait"),
            ("slow_max", "Slowest ms"), ("tap", "Tap ms"), ("tap_max", "Tap max"), ("drag", "Drag ms"),
            ("list", "List ms"), ("req", "Request ms"), ("late", "Late scans"), ("int_min", "Min int KB")]
    out = ["| Phase | " + " | ".join(c[1] for c in cols) + " |", "|---" * (len(cols) + 1) + "|"]
    for p in SCORED:
        a = g["agg"][p]
        out.append(f"| {p} {PHASES[p]} | " + " | ".join(fmt(a.get(k), k) for k, _ in cols) + " |")
    boot = ", ".join(f"{BOOT[k]} {g['boot'][k]:.1f} s" for k in BOOT if g["boot"].get(k))
    if boot:
        out.append("")
        out.append(f"After boot: {boot}.")
    return "\n".join(out)


def compare(ref, g):
    out = [f"| Phase | Metric | {ref['change']} | {g['change']} | Change | Counts beyond | Verdict |",
           "|---|---|---|---|---|---|---|"]
    better = worse = 0
    for p in SCORED:
        for k, (label, unit, sign, _) in METRICS.items():
            if sign == 0:
                continue
            v, thr = verdict(ref, g, p, k)
            a, b = ref["agg"][p].get(k), g["agg"][p].get(k)
            if a is None and b is None:
                continue
            if v == "better":
                better += 1
            elif v == "worse":
                worse += 1
            if not v:
                continue
            ch = f"{(b - a):+.1f}" if a is not None and b is not None else "–"
            pct = f" ({(b - a) / a * 100:+.0f}%)" if a and b is not None else ""
            out.append(f"| {p} {PHASES[p]} | {label.strip()} | {fmt(a, k)} | {fmt(b, k)} | {ch}{pct} | "
                       f"{fmt(thr)} {unit} | **{v}** |")
    for k in BOOT:
        a, b = ref["boot"].get(k), g["boot"].get(k)
        if a and b:
            out.append(f"| boot | {BOOT[k]} (s) | {a:.1f} | {b:.1f} | {b - a:+.1f} | – | (info) |")
    out.append("")
    out.append(f"{better} better and {worse} worse beyond noise; everything else within noise.")
    return "\n".join(out)


# ---------- HTML ----------

COLORS = ["#8e8e93", "#0a84ff", "#30d158", "#ff9f0a", "#bf5af2", "#ff453a"]


def bars_svg(groups, k):
    label, unit, _, _ = METRICS[k]
    vals = [[g["agg"][p].get(k) for g in groups] for p in SCORED]
    top = max([v for row in vals for v in row if v is not None] or [1]) or 1
    W, H, pad = 720, 200, 30
    bw = (W - pad) / len(SCORED)
    n = len(groups)
    parts = [f'<svg viewBox="0 0 {W} {H + 40}" width="100%" role="img" aria-label="{html.escape(label)} by phase">']
    for i, row in enumerate(vals):
        x0 = pad + i * bw
        for j, v in enumerate(row):
            if v is None:
                continue
            h = v / top * H
            x = x0 + 6 + j * (bw - 12) / n
            parts.append(f'<rect x="{x:.1f}" y="{H - h:.1f}" width="{(bw - 12) / n - 2:.1f}" height="{h:.1f}" '
                         f'fill="{COLORS[j % len(COLORS)]}"><title>{html.escape(groups[j]["change"])}: {fmt(v, k)} {unit}'
                         f'</title></rect>')
        parts.append(f'<text x="{x0 + bw / 2:.1f}" y="{H + 16}" font-size="11" text-anchor="middle">'
                     f'{i + 1} {PHASES[i + 1]}</text>')
    parts.append(f'<text x="0" y="12" font-size="11">{fmt(top, k)} {unit}</text></svg>')
    return "".join(parts)


def line_svg(groups, col, label):
    W, H = 720, 180
    series, lo, hi, tmax = [], None, None, 1
    for j, g in enumerate(groups):
        r = g["runs"][0]
        if 1 not in r["starts"]:
            continue
        t0 = r["starts"][1]
        pts = [((int(x["ms"]) - t0) / 1000, num(x.get(col))) for x in r["rows"] if int(x["ms"]) >= t0]
        pts = [(t, v) for t, v in pts if v is not None]
        if not pts:
            continue
        series.append((j, pts))
        vs = [v for _, v in pts]
        lo = min(vs + ([lo] if lo is not None else []))
        hi = max(vs + ([hi] if hi is not None else []))
        tmax = max(tmax, pts[-1][0])
    if not series:
        return ""
    hi = hi if hi > lo else lo + 1
    parts = [f'<svg viewBox="0 0 {W} {H + 20}" width="100%" role="img" aria-label="{html.escape(label)} over the run">']
    for j, pts in series:
        d = " ".join(f"{t / tmax * W:.1f},{H - (v - lo) / (hi - lo) * H:.1f}" for t, v in pts)
        parts.append(f'<polyline fill="none" stroke="{COLORS[j % len(COLORS)]}" stroke-width="1.5" points="{d}"/>')
    parts.append(f'<text x="0" y="12" font-size="11">{hi:.1f}</text><text x="0" y="{H}" font-size="11">{lo:.1f}</text>'
                 f'<text x="{W}" y="{H + 16}" font-size="11" text-anchor="end">{tmax / 60:.0f} min</text></svg>')
    return "".join(parts)


def md_to_html_table(md):
    lines = [l for l in md.splitlines() if l.startswith("|")]
    if not lines:
        return ""
    out = ["<table>"]
    for i, l in enumerate(lines):
        if i == 1:
            continue
        cells = [c.strip() for c in l.strip("|").split("|")]
        tag = "th" if i == 0 else "td"
        out.append("<tr>" + "".join(f"<{tag}>{html.escape(c).replace('**', '')}</{tag}>" for c in cells) + "</tr>")
    out.append("</table>")
    rest = [l for l in md.splitlines() if l and not l.startswith("|")]
    return "\n".join(out) + "".join(f"<p>{html.escape(l)}</p>" for l in rest)


def write_html(groups, path):
    legend = " ".join(f'<span style="color:{COLORS[j % len(COLORS)]}">■</span> {html.escape(g["change"])} '
                      f'({len(g["runs"])} run{"s" if len(g["runs"]) != 1 else ""})' for j, g in enumerate(groups))
    body = [f"<h1>Dock performance</h1><p>{legend}</p>"]
    for g in groups:
        body.append(f"<h2>{html.escape(g['change'])}</h2>" + md_to_html_table(table(g)))
        body.append("<p class=note>Runs: " + ", ".join(html.escape(r["name"]) for r in g["runs"]) + "</p>")
    for g in groups[1:]:
        body.append(f"<h2>{html.escape(g['change'])} against {html.escape(groups[0]['change'])}</h2>"
                    + md_to_html_table(compare(groups[0], g)))
    for k in ["frame", "move_fps", "tap", "drag", "req", "busy1", "busy0", "temp"]:
        body.append(f"<h3>{html.escape(METRICS[k][0].strip())} ({METRICS[k][1] or 'count'})</h3>" + bars_svg(groups, k))
    body.append("<h3>Chip temperature over the run (first run of each group)</h3>" + line_svg(groups, "cpu_c", "temp"))
    body.append("<h3>Core 1 busy % over the run</h3>" + line_svg(groups, "busy1", "busy1"))
    css = ("body{font:14px -apple-system,system-ui,sans-serif;max-width:1100px;margin:24px auto;padding:0 16px;"
           "background:#fff;color:#1c1c1e}table{border-collapse:collapse;font-size:12px;margin:8px 0}"
           "td,th{border:1px solid #d1d1d6;padding:3px 6px;text-align:right}th{background:#f2f2f7}"
           ".note{color:#6e6e73;font-size:12px}svg text{fill:#3a3a3c}"
           "@media (prefers-color-scheme:dark){body{background:#1c1c1e;color:#f2f2f7}th{background:#2c2c2e}"
           "td,th{border-color:#3a3a3c}svg text{fill:#d1d1d6}}")
    with open(path, "w") as f:
        f.write(f"<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
                f"<title>Dock performance</title><style>{css}</style>{''.join(body)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--out", help="HTML report path; a .md with the same tables goes next to it")
    args = ap.parse_args()
    groups = group_runs(args.runs)
    md = []
    for g in groups:
        md.append(f"## {g['change']} ({len(g['runs'])} run{'s' if len(g['runs']) != 1 else ''})\n\n{table(g)}\n")
    for g in groups[1:]:
        md.append(f"## {g['change']} against {groups[0]['change']}\n\n{compare(groups[0], g)}\n")
    text = "\n".join(md)
    print(text)
    if args.out:
        os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
        write_html(groups, args.out)
        with open(os.path.splitext(args.out)[0] + ".md", "w") as f:
            f.write(text)
        print(f"-> {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
