Report: docs/power/reports/baseline-after-run-2.html
**baseline** (2 runs: 2026-11-09-baseline-1.csv, 2026-11-09-baseline-2.csv)

| Phase | Est. mA | Noise | vs ref | CPU busy c0/c1 % | Frames/s | FPS moving | Slowest frame ms | Tap→screen ms | Request ms (ok/all) | Lowest RAM KB |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 Dim idle | 67.6 | 28.4 | – | 10 / 2 | 2.0 | – | – | – | 1099 (27/31) | 21 |
| 2 Awake, still | 58.0 | 1.3 | – | 10 / 4 | 2.0 | – | – | – | 687 (18/18) | 21 |
| 3 Scroll | 107.9 | 4.4 | – | 61 / 100 | 36.6 | 37.3 | 86 | – | 1837 (19/19) | 21 |
| 4 Screen churn | 65.3 | 1.7 | – | 18 / 24 | 8.0 | 37.4 | 66 | 23 (max 65) | 830 (19/19) | 21 |
| 5 Network burst | 85.6 | 0.6 | – | 38 / 3 | 2.0 | – | – | – | 853 (174/174) | 21 |
| 6 Everything | 123.4 | 1.3 | – | 65 / 70 | 32.9 | 46.2 | 84 | 30 (max 74) | 1367 (145/145) | 21 |
| 7 Cool-down | 51.0 | 1.6 | – | 8 / 2 | 2.0 | – | – | – | 855 (17/17) | 21 |

Projected battery life (typical day, 2000 mAh): **29.5 h** at 67.7 mA average. Model model-v2; every figure is an estimate.

