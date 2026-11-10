Report: docs/power/reports/r1v2-after-run-1.html
**r1v2** (1 run: 2026-11-09-r1v2-1.csv)

| Phase | Est. mA | Noise | vs ref | CPU busy c0/c1 % | Frames/s | FPS moving | Slowest frame ms | Tap→screen ms | Request ms (ok/all) | Lowest RAM KB |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 Dim idle | 43.3 | – | – | 13 / 6 | 2.0 | – | – | – | 839 (15/15) | 27 |
| 2 Awake, still | 58.6 | – | – | 9 / 4 | 2.0 | – | – | – | 822 (8/8) | 27 |
| 3 Scroll | 92.9 | – | – | 44 / 99 | 59.9 | 62.6 | 76 | 20 (max 30) | 1197 (11/11) | 25 |
| 4 Screen churn | 65.5 | – | – | 17 / 23 | 8.0 | 39.2 | 52 | 23 (max 40) | 825 (9/9) | 25 |
| 5 Network burst | 84.9 | – | – | 39 / 5 | 2.0 | – | – | – | 1005 (87/87) | 21 |
| 6 Everything | 123.0 | – | – | 60 / 70 | 36.8 | 51.0 | 72 | 28 (max 73) | 1378 (74/74) | 21 |
| 7 Cool-down | 43.2 | – | – | 13 / 6 | 2.0 | – | – | – | 1222 (9/9) | 21 |

Projected battery life (typical day, 2000 mAh): **43.9 h** at 45.6 mA average. Model model-v2; every figure is an estimate.

