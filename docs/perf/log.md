# Dock performance log

One entry per change, newest last. How it works is in the spec: `docs/superpowers/specs/2026-11-12-dock-performance-analysis-design.md`.

- Speed first, heat second. The dock runs from the wall, so there's no current model: heat is the chip's own temperature sensor.
- Raw runs are in `runs/` (every line the logger printed: `H` column names, `P` one sample a second, `E` events). Reports are in `reports/`; worker briefs in `briefs/`.
- A change passes only if a speed score gets better beyond noise, none gets worse, the chip runs no hotter, and the panel shows no new late frames (spec section 6).

How to run a test:

```bash
python3 tools/dock_perf_run.py --change r1 --note "what changed"
python3 tools/dock_perf_report.py docs/perf/runs/*-baseline-*.csv docs/perf/runs/*-r1-*.csv --out docs/perf/reports/r1.html
```

---
