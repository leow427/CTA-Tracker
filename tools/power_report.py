"""Turn power-analysis runs into a per-phase table and an HTML report (power analysis spec, sections 4-6).

Runs are grouped by their change ID (the `# change:` header), in the order given; the first group is the reference
each later group is compared with. Within a group, the spread of the runs' phase averages is the noise.

    python3 tools/power_report.py docs/power/runs/2026-11-09-baseline-*.csv --out docs/power/reports/baseline.html
    python3 tools/power_report.py docs/power/runs/*-baseline-*.csv docs/power/runs/*-c01-*.csv --out ...

Prints a Markdown table per group (for docs/power/log.md). Every current figure is a modeled estimate.
"""
import argparse
import html
import json
import math
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from power_model import PARTS, estimate, load, total  # noqa: E402

SCORED = [1, 2, 3, 4, 5, 6, 7]


def read_run(path, model):
    meta, cols, rows, events = {}, None, [], []
    drop = model["scoring"]["drop_first_s"] * 1000
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if line.startswith("# ") and ": " in line:
                k, v = line[2:].split(": ", 1)
                meta[k] = v
            elif line.startswith("H,"):
                cols = line[2:].split(",")
            elif line.startswith("P,") and cols:
                vals = line[2:].split(",")
                rows.append(dict(zip(cols, vals)))
            elif line.startswith("E,"):
                parts = line[2:].split(",")
                events.append((int(parts[0]), parts[1:]))
    starts = {}
    for ms, ev in events:
        if ev[0] == "phase":
            starts[int(ev[1])] = ms
    lit = 0.0
    out = []
    for r in rows:
        if r.get("lit"):
            lit = float(r["lit"])
        r["_lit_carry"] = lit
        parts = estimate(model, r)
        ms, ph = int(r["ms"]), int(r["phase"])
        out.append({"row": r, "ms": ms, "phase": ph, "parts": parts, "total": total(model, parts),
                    "scored": ph in starts and ms >= starts[ph] + drop})
    req = []   # (ms, kind, code, duration)
    for ms, ev in events:
        if ev[0] == "req_end" and len(ev) >= 5:
            req.append((ms, ev[1], int(ev[2]), int(ev[4])))
    return {"path": path, "name": os.path.basename(path), "meta": meta, "samples": out, "starts": starts,
            "requests": req, "aborted": meta.get("aborted")}


def fnum(r, k):
    v = r["row"].get(k, "")
    try:
        return float(v) if v != "" else None
    except ValueError:
        return None


def phase_stats(run, model):
    stats = {}
    for ph in SCORED:
        s = [x for x in run["samples"] if x["phase"] == ph and x["scored"]]
        if not s:
            continue
        t0 = run["starts"][ph]
        t1 = run["starts"].get(ph + 1, max(x["ms"] for x in run["samples"]) + 1000)
        fps = [v for v in (fnum(x, "move_fps") for x in s) if v is not None]
        slow = [v for v in (fnum(x, "slow_frame_ms") for x in s) if v is not None]
        tap = [v for v in (fnum(x, "tap_ms") for x in s) if v is not None]
        reqs = [d for (ms, _, code, d) in run["requests"] if t0 <= ms < t1]
        oks = [d for (ms, _, code, d) in run["requests"] if t0 <= ms < t1 and code == 200]
        tot = [x["total"] for x in s]
        stats[ph] = {
            "n": len(s),
            "ma": statistics.mean(tot),
            "ma_sd": statistics.pstdev(tot),
            "parts": {p: statistics.mean(x["parts"][p] for x in s) for p in PARTS},
            "busy0": statistics.mean(fnum(x, "busy0") or 0 for x in s),
            "busy1": statistics.mean(fnum(x, "busy1") or 0 for x in s),
            "frames": statistics.mean(fnum(x, "frames") or 0 for x in s),
            "fps": statistics.mean(fps) if fps else None,
            "slow": max(slow) if slow else None,
            "tap": statistics.mean(tap) if tap else None,
            "tap_max": max(tap) if tap else None,
            "req_n": len(reqs),
            "req_ok": len(oks),
            "req_ms": statistics.mean(oks) if oks else None,
            "int_min": min(fnum(x, "int_min") or 0 for x in s),
            "rssi": statistics.mean(v for v in (fnum(x, "rssi") for x in s) if v is not None)
            if any(fnum(x, "rssi") for x in s) else None,
        }
    return stats


def projection(stats, model):
    w = model["typical_day"]["phase_weights"]
    have = {int(k): v for k, v in w.items() if int(k) in stats}
    if len(have) != len(w):
        return None
    avg = sum(stats[k]["ma"] * v for k, v in have.items()) / sum(have.values())
    return {"avg_ma": avg, "hours": model["battery"]["capacity_mah"] / avg}


def run_mah(run):
    s = run["samples"]
    return sum(x["total"] for x in s) / 3600.0   # one sample a second


def group_runs(runs):
    groups = []
    for r in runs:
        ch = r["meta"].get("change", "?")
        if not groups or groups[-1]["change"] != ch:
            if any(g["change"] == ch for g in groups):
                next(g for g in groups if g["change"] == ch)["runs"].append(r)
                continue
            groups.append({"change": ch, "runs": []})
        groups[-1]["runs"].append(r)
    return groups


def summarize(group, model):
    per = [phase_stats(r, model) for r in group["runs"]]
    phases = sorted(set().union(*[p.keys() for p in per]))
    out = {}
    for ph in phases:
        vals = [p[ph] for p in per if ph in p]
        ma = [v["ma"] for v in vals]

        def mean_of(k):
            xs = [v[k] for v in vals if v[k] is not None]
            return statistics.mean(xs) if xs else None
        out[ph] = {
            "ma": statistics.mean(ma),
            "noise": (max(ma) - min(ma)) if len(ma) > 1 else None,
            "parts": {p: statistics.mean(v["parts"][p] for v in vals) for p in PARTS},
            **{k: mean_of(k) for k in ("busy0", "busy1", "frames", "fps", "tap", "req_ms", "rssi")},
            "slow": max((v["slow"] for v in vals if v["slow"] is not None), default=None),
            "tap_max": max((v["tap_max"] for v in vals if v["tap_max"] is not None), default=None),
            "int_min": min(v["int_min"] for v in vals),
            "req_n": sum(v["req_n"] for v in vals),
            "req_ok": sum(v["req_ok"] for v in vals),
        }
    group["phases"] = out
    group["projection"] = projection(out, model)
    return group


def fmt(v, spec=".0f", dash="–"):
    return dash if v is None else format(v, spec)


def markdown(group, ref, model):
    names = model["scoring"]["phase_names"]
    lines = [f"**{group['change']}** ({len(group['runs'])} run{'s' if len(group['runs']) != 1 else ''}: "
             + ", ".join(r["name"] for r in group["runs"]) + ")", "",
             "| Phase | Est. mA | Noise | vs ref | CPU busy c0/c1 % | Frames/s | FPS moving | Slowest frame ms "
             "| Tap→screen ms | Request ms (ok/all) | Lowest RAM KB |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    for ph, s in group["phases"].items():
        vs = "–"
        if ref is not None and ref is not group and ph in ref["phases"]:
            r = ref["phases"][ph]["ma"]
            vs = f"{(s['ma'] - r) / r * 100:+.1f}%"
        lines.append(
            f"| {ph} {names[str(ph)]} | {s['ma']:.1f} | {fmt(s['noise'], '.1f')} | {vs} | "
            f"{s['busy0']:.0f} / {s['busy1']:.0f} | {s['frames']:.1f} | {fmt(s['fps'], '.1f')} | {fmt(s['slow'])} | "
            f"{fmt(s['tap']) + ' (max ' + fmt(s['tap_max']) + ')' if s['tap'] is not None else '–'} | "
            f"{fmt(s['req_ms']) + ' (' + str(s['req_ok']) + '/' + str(s['req_n']) + ')' if s['req_n'] else '–'} | "
            f"{s['int_min'] / 1024:.0f} |")
    p = group["projection"]
    if p:
        lines += ["", f"Projected battery life (typical day, 2000 mAh): **{p['hours']:.1f} h** "
                      f"at {p['avg_ma']:.1f} mA average. Model {model['version']}; every figure is an estimate."]
    aborted = [r["name"] for r in group["runs"] if r["aborted"]]
    if aborted:
        lines.append(f"Aborted runs: {', '.join(aborted)}.")
    return "\n".join(lines)


# ---------- HTML ----------

COLORS = {"cpu": 1, "screen": 2, "wifi": 3, "ble": 4, "memory": 5, "fixed": 6}
LABELS = {"cpu": "CPU", "screen": "Screen", "wifi": "Wi-Fi", "ble": "BLE", "memory": "Memory", "fixed": "Fixed"}

CSS = """
.viz-root{color-scheme:light;--surface-0:#f4f3f0;--surface-1:#fcfcfb;--text-primary:#0b0b0b;--text-secondary:#52514e;
--text-muted:#77766f;--grid:#e4e3de;--band:#efeee9;
--series-1:#2a78d6;--series-2:#eb6834;--series-3:#1baf7a;--series-4:#eda100;--series-5:#e87ba4;--series-6:#008300;}
@media (prefers-color-scheme: dark){:root:where(:not([data-theme="light"])) .viz-root{color-scheme:dark;
--surface-0:#111110;--surface-1:#1a1a19;--text-primary:#f0efec;--text-secondary:#c3c2b7;--text-muted:#94938a;
--grid:#2c2c2a;--band:#232321;--series-1:#3987e5;--series-2:#d95926;--series-3:#199e70;--series-4:#c98500;
--series-5:#d55181;--series-6:#008300;}}
:root[data-theme="dark"] .viz-root{color-scheme:dark;--surface-0:#111110;--surface-1:#1a1a19;--text-primary:#f0efec;
--text-secondary:#c3c2b7;--text-muted:#94938a;--grid:#2c2c2a;--band:#232321;--series-1:#3987e5;--series-2:#d95926;
--series-3:#199e70;--series-4:#c98500;--series-5:#d55181;--series-6:#008300;}
body{margin:0;background:var(--surface-0);}
.viz-root{background:var(--surface-0);color:var(--text-primary);font:14px/1.45 -apple-system,BlinkMacSystemFont,
"Segoe UI",system-ui,sans-serif;padding:24px 16px 48px;min-height:100vh;box-sizing:border-box}
.wrap{max-width:1040px;margin:0 auto}
h1{font-size:22px;margin:0 0 4px}h2{font-size:16px;margin:32px 0 8px}
.sub{color:var(--text-secondary);margin:0 0 16px}
.card{background:var(--surface-1);border-radius:12px;padding:16px;margin:12px 0;overflow-x:auto}
.stats{display:flex;flex-wrap:wrap;gap:12px}.stat{background:var(--surface-1);border-radius:12px;padding:12px 16px;
min-width:150px}.stat b{display:block;font-size:24px;font-variant-numeric:tabular-nums}.stat span{color:var(--text-secondary);font-size:12px}
table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums;font-size:13px}
th,td{padding:6px 8px;text-align:right;border-bottom:1px solid var(--grid);white-space:nowrap}
th:first-child,td:first-child{text-align:left}th{color:var(--text-secondary);font-weight:500}
.legend{display:flex;flex-wrap:wrap;gap:14px;font-size:12px;color:var(--text-secondary);margin:4px 0 8px}
.legend i{display:inline-block;width:10px;height:10px;border-radius:3px;margin-right:6px;vertical-align:-1px}
svg text{fill:var(--text-muted);font-size:11px}.tip{position:fixed;pointer-events:none;background:var(--surface-1);
color:var(--text-primary);border-radius:8px;padding:8px 10px;font-size:12px;box-shadow:0 4px 16px rgba(0,0,0,.18);
display:none;font-variant-numeric:tabular-nums;z-index:9}.tip .r{display:flex;justify-content:space-between;gap:14px}
.note{color:var(--text-secondary);font-size:12px}
"""


def legend(keys):
    return '<div class="legend">' + "".join(
        f'<span><i style="background:var(--series-{COLORS[k]})"></i>{LABELS[k]}</span>' for k in keys) + "</div>"


def area_chart(run, model, cid):
    s = run["samples"]
    if not s:
        return ""
    W, H, L, R, T, B = 1000, 260, 44, 8, 8, 24
    t0 = s[0]["ms"]
    xmax = max(1, (s[-1]["ms"] - t0) / 1000)
    ymax = max(x["total"] for x in s)
    ymax = math.ceil(ymax / 20) * 20 or 20
    X = lambda t: L + (t / xmax) * (W - L - R)  # noqa: E731
    Y = lambda v: T + (1 - v / ymax) * (H - T - B)  # noqa: E731
    keys = [k for k in PARTS if any(x["parts"][k] > 0.01 for x in s)]
    out = [f'<svg viewBox="0 0 {W} {H}" width="100%" role="img" aria-label="Estimated current over time by part" '
           f'id="{cid}">']
    names = model["scoring"]["phase_names"]
    for ph, ms in sorted(run["starts"].items()):
        x0 = X((ms - t0) / 1000)
        nxt = run["starts"].get(ph + 1)
        x1 = X((nxt - t0) / 1000) if nxt else W - R
        if ph % 2:
            out.append(f'<rect x="{x0:.1f}" y="{T}" width="{x1 - x0:.1f}" height="{H - T - B}" fill="var(--band)"/>')
        label = f"{ph} {names[str(ph)]}" if x1 - x0 > 90 else str(ph)
        out.append(f'<text x="{x0 + 4:.1f}" y="{T + 12}">{html.escape(label)}</text>')
    for i in range(0, 5):
        v = ymax * i / 4
        out.append(f'<line x1="{L}" x2="{W - R}" y1="{Y(v):.1f}" y2="{Y(v):.1f}" stroke="var(--grid)"/>'
                   f'<text x="{L - 6}" y="{Y(v) + 4:.1f}" text-anchor="end">{v:.0f}</text>')
    for m in range(0, int(xmax) + 1, 120):
        out.append(f'<text x="{X(m):.1f}" y="{H - 6}" text-anchor="middle">{m // 60} min</text>')
    base = [0.0] * len(s)
    for k in keys:
        top = [b + x["parts"][k] for b, x in zip(base, s)]
        pts = [f"{X((x['ms'] - t0) / 1000):.1f},{Y(v):.1f}" for x, v in zip(s, top)]
        back = [f"{X((x['ms'] - t0) / 1000):.1f},{Y(v):.1f}" for x, v in reversed(list(zip(s, base)))]
        out.append(f'<polygon points="{" ".join(pts + back)}" fill="var(--series-{COLORS[k]})" fill-opacity=".9"/>')
        base = top
    out.append(f'<line class="xh" x1="0" x2="0" y1="{T}" y2="{H - B}" stroke="var(--text-secondary)" '
               f'stroke-width="1" style="display:none"/>')
    out.append("</svg>")
    data = [[round((x["ms"] - t0) / 1000), x["phase"], round(x["total"], 1)] + [round(x["parts"][k], 1) for k in keys]
            for x in s]
    out.append(f'<script>hover("{cid}",{json.dumps(data)},{json.dumps([LABELS[k] for k in keys])},{L},{W - R},{xmax});'
               f'</script>')
    return legend(keys) + "".join(out)


def cpu_chart(run, cid):
    s = run["samples"]
    if not s:
        return ""
    W, H, L, R, T, B = 1000, 160, 44, 8, 8, 24
    t0 = s[0]["ms"]
    xmax = max(1, (s[-1]["ms"] - t0) / 1000)
    X = lambda t: L + (t / xmax) * (W - L - R)  # noqa: E731
    Y = lambda v: T + (1 - v / 100) * (H - T - B)  # noqa: E731
    out = [f'<svg viewBox="0 0 {W} {H}" width="100%" role="img" aria-label="CPU busy percent per core">']
    for v in (0, 50, 100):
        out.append(f'<line x1="{L}" x2="{W - R}" y1="{Y(v):.1f}" y2="{Y(v):.1f}" stroke="var(--grid)"/>'
                   f'<text x="{L - 6}" y="{Y(v) + 4:.1f}" text-anchor="end">{v}%</text>')
    for m in range(0, int(xmax) + 1, 120):
        out.append(f'<text x="{X(m):.1f}" y="{H - 6}" text-anchor="middle">{m // 60} min</text>')
    for core, slot in (("busy0", 1), ("busy1", 2)):
        pts = " ".join(f"{X((x['ms'] - t0) / 1000):.1f},{Y(fnum(x, core) or 0):.1f}" for x in s)
        out.append(f'<polyline points="{pts}" fill="none" stroke="var(--series-{slot})" stroke-width="2" '
                   f'stroke-linejoin="round"/>')
    out.append("</svg>")
    lg = ('<div class="legend"><span><i style="background:var(--series-1)"></i>Core 0 (sending, touch, network)</span>'
          '<span><i style="background:var(--series-2)"></i>Core 1 (drawing)</span></div>')
    return lg + "".join(out)


def bar_chart(groups, model):
    phases = SCORED
    W, H, L, R, T, B = 1000, 240, 44, 8, 8, 40
    n = len(groups)
    ymax = max((g["phases"][p]["ma"] for g in groups for p in phases if p in g["phases"]), default=1)
    ymax = math.ceil(ymax / 20) * 20 or 20
    Y = lambda v: T + (1 - v / ymax) * (H - T - B)  # noqa: E731
    slot_w = (W - L - R) / len(phases)
    bw = min(28, (slot_w - 16) / max(1, n) - 2)
    out = [f'<svg viewBox="0 0 {W} {H}" width="100%" role="img" aria-label="Average estimated current per phase">']
    for i in range(0, 5):
        v = ymax * i / 4
        out.append(f'<line x1="{L}" x2="{W - R}" y1="{Y(v):.1f}" y2="{Y(v):.1f}" stroke="var(--grid)"/>'
                   f'<text x="{L - 6}" y="{Y(v) + 4:.1f}" text-anchor="end">{v:.0f}</text>')
    names = model["scoring"]["phase_names"]
    for pi, p in enumerate(phases):
        cx = L + slot_w * (pi + 0.5)
        out.append(f'<text x="{cx:.1f}" y="{H - 22}" text-anchor="middle">{p} {html.escape(names[str(p)])}</text>')
        x = cx - (n * (bw + 2)) / 2
        for gi, g in enumerate(groups):
            if p in g["phases"]:
                v = g["phases"][p]["ma"]
                y = Y(v)
                h = H - B - y
                r = min(4, bw / 2, h)
                out.append(
                    f'<path d="M{x:.1f},{H - B} V{y + r:.1f} Q{x:.1f},{y:.1f} {x + r:.1f},{y:.1f} H{x + bw - r:.1f} '
                    f'Q{x + bw:.1f},{y:.1f} {x + bw:.1f},{y + r:.1f} V{H - B} Z" fill="var(--series-{gi % 8 + 1})">'
                    f'<title>{html.escape(g["change"])}, phase {p}: {v:.1f} mA (estimate)</title></path>')
            x += bw + 2
    out.append("</svg>")
    lg = '<div class="legend">' + "".join(
        f'<span><i style="background:var(--series-{i % 8 + 1})"></i>{html.escape(g["change"])}</span>'
        for i, g in enumerate(groups)) + "</div>"
    return lg + "".join(out)


HOVER_JS = """
function hover(id,data,labels,x0,x1,xmax){const svg=document.getElementById(id);const xh=svg.querySelector('.xh');
const tip=document.getElementById('tip');svg.addEventListener('mousemove',e=>{const r=svg.getBoundingClientRect();
const vx=(e.clientX-r.left)/r.width*1000;if(vx<x0||vx>x1){tip.style.display='none';xh.style.display='none';return}
const t=(vx-x0)/(x1-x0)*xmax;let b=data[0];for(const d of data){if(Math.abs(d[0]-t)<Math.abs(b[0]-t))b=d}
const px=x0+b[0]/xmax*(x1-x0);xh.setAttribute('x1',px);xh.setAttribute('x2',px);xh.style.display='';
let h='<b>'+Math.floor(b[0]/60)+':'+String(b[0]%60).padStart(2,'0')+' · phase '+b[1]+'</b>';
h+='<div class="r"><span>Total</span><span>'+b[2]+' mA</span></div>';
labels.forEach((l,i)=>{h+='<div class="r"><span>'+l+'</span><span>'+b[3+i]+' mA</span></div>'});
tip.innerHTML=h;tip.style.display='block';tip.style.left=Math.min(e.clientX+14,innerWidth-190)+'px';
tip.style.top=(e.clientY+14)+'px'});svg.addEventListener('mouseleave',()=>{tip.style.display='none';
xh.style.display='none'})}
"""


def report_html(groups, model, title):
    names = model["scoring"]["phase_names"]
    last = groups[-1]
    p = last["projection"]
    stats = []
    if p:
        stats.append(f'<div class="stat"><b>{p["hours"]:.1f} h</b><span>projected battery life, {html.escape(last["change"])}</span></div>')
        stats.append(f'<div class="stat"><b>{p["avg_ma"]:.1f} mA</b><span>typical-day average (estimate)</span></div>')
    if len(groups) > 1 and groups[0]["projection"] and p:
        d = (p["avg_ma"] - groups[0]["projection"]["avg_ma"]) / groups[0]["projection"]["avg_ma"] * 100
        stats.append(f'<div class="stat"><b>{d:+.1f}%</b><span>vs {html.escape(groups[0]["change"])}</span></div>')
    body = [f'<div class="viz-root"><div class="wrap"><h1>{html.escape(title)}</h1>',
            f'<p class="sub">Model {model["version"]} · every current figure is a modeled estimate (no battery, no meter) · '
            f'stress test {html.escape(last["runs"][0]["meta"].get("stress", "?"))}</p>',
            f'<div class="stats">{"".join(stats)}</div>',
            '<h2>Average estimated current per phase</h2><div class="card">', bar_chart(groups, model), '</div>']
    for gi, g in enumerate(groups):
        run = g["runs"][0]
        body.append(f'<h2>{html.escape(g["change"])}: current over time by part ({html.escape(run["name"])})</h2>')
        body.append('<div class="card">' + area_chart(run, model, f"area{gi}") + '</div>')
        body.append('<div class="card">' + cpu_chart(run, f"cpu{gi}") + '</div>')
        rows = []
        for ph, s in g["phases"].items():
            rows.append(
                f"<tr><td>{ph} {html.escape(names[str(ph)])}</td><td>{s['ma']:.1f}</td><td>{fmt(s['noise'], '.1f')}</td>"
                + "".join(f"<td>{s['parts'][k]:.1f}</td>" for k in PARTS)
                + f"<td>{s['busy0']:.0f} / {s['busy1']:.0f}</td><td>{fmt(s['fps'], '.1f')}</td><td>{fmt(s['slow'])}</td>"
                f"<td>{fmt(s['tap'])}</td><td>{fmt(s['req_ms'])} ({s['req_ok']}/{s['req_n']})</td>"
                f"<td>{s['int_min'] / 1024:.0f}</td></tr>")
        body.append('<div class="card"><table><tr><th>Phase</th><th>Est. mA</th><th>Noise</th>'
                    + "".join(f"<th>{LABELS[k]}</th>" for k in PARTS)
                    + '<th>CPU c0/c1 %</th><th>FPS moving</th><th>Slowest ms</th><th>Tap→screen ms</th>'
                    '<th>Request ms (ok/all)</th><th>Lowest RAM KB</th></tr>' + "".join(rows) + '</table>'
                    f'<p class="note">{len(g["runs"])} run(s): {html.escape(", ".join(r["name"] for r in g["runs"]))}. '
                    'Noise is the range of the runs\' phase averages.</p></div>')
    body.append('</div></div><div class="tip" id="tip"></div>')
    return (f'<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,'
            f'initial-scale=1"><title>Handheld power report</title><style>{CSS}</style><script>{HOVER_JS}</script>'
            f'</head><body>{"".join(body)}</body></html>')


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("runs", nargs="+")
    ap.add_argument("--out", help="write the HTML report here")
    ap.add_argument("--title", default="Handheld power report")
    args = ap.parse_args()
    model = load()
    runs = [read_run(p, model) for p in args.runs]
    groups = [summarize(g, model) for g in group_runs(runs)]
    ref = groups[0]
    for g in groups:
        print(markdown(g, ref, model))
        print()
    if args.out:
        os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
        with open(args.out, "w") as f:
            f.write(report_html(groups, model, args.title))
        print(f"Report: {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
