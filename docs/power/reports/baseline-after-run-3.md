Report: docs/power/reports/baseline-after-run-3.html
**baseline** (3 runs: 2026-11-09-baseline-1.csv, 2026-11-09-baseline-2.csv, 2026-11-09-baseline-3.csv)

| Phase | Est. mA | Noise | vs ref | CPU busy c0/c1 % | Frames/s | FPS moving | Slowest frame ms | Tap→screen ms | Request ms (ok/all) | Lowest RAM KB |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 Dim idle | 62.3 | 30.0 | – | 10 / 2 | 2.0 | – | – | – | 981 (42/46) | 21 |
| 2 Awake, still | 57.7 | 1.5 | – | 10 / 4 | 2.0 | – | – | – | 700 (26/26) | 21 |
| 3 Scroll | 100.7 | 23.8 | – | 52 / 99 | 50.4 | 52.0 | 86 | – | 1622 (30/30) | 21 |
| 4 Screen churn | 65.5 | 1.7 | – | 18 / 23 | 7.9 | 37.1 | 66 | 23 (max 65) | 779 (29/29) | 21 |
| 5 Network burst | 85.6 | 0.6 | – | 38 / 3 | 2.0 | – | – | – | 858 (261/261) | 21 |
| 6 Everything | 123.1 | 1.5 | – | 65 / 70 | 33.7 | 47.2 | 84 | 30 (max 76) | 1343 (220/220) | 21 |
| 7 Cool-down | 51.3 | 1.7 | – | 8 / 2 | 2.0 | – | – | – | 805 (25/25) | 21 |

Projected battery life (typical day, 2000 mAh): **31.8 h** at 62.8 mA average. Model model-v2; every figure is an estimate.

