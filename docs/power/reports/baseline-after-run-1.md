**baseline** (1 run: 2026-11-09-baseline-1.csv)

| Phase | Est. mA | Noise | vs ref | CPU busy c0/c1 % | Frames/s | FPS moving | Slowest frame ms | Tap→screen ms | Request ms (ok/all) | Lowest RAM KB |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 Dim idle | 81.8 | – | – | 12 / 2 | 2.0 | – | – | – | 1211 (12/16) | 25 |
| 2 Awake, still | 57.4 | – | – | 9 / 4 | 2.0 | – | – | – | 633 (10/10) | 25 |
| 3 Scroll | 110.1 | – | – | 62 / 100 | 36.6 | 37.4 | 58 | – | 2186 (9/9) | 25 |
| 4 Screen churn | 66.1 | – | – | 18 / 25 | 8.3 | 38.1 | 53 | 23 (max 36) | 950 (10/10) | 25 |
| 5 Network burst | 85.9 | – | – | 38 / 3 | 2.0 | – | – | – | 860 (87/87) | 25 |
| 6 Everything | 124.1 | – | – | 66 / 71 | 32.9 | 46.3 | 83 | 30 (max 73) | 1373 (73/73) | 21 |
| 7 Cool-down | 50.2 | – | – | 7 / 2 | 2.0 | – | – | – | 762 (8/8) | 21 |

Projected battery life (typical day, 2000 mAh): **24.8 h** at 80.5 mA average. Model model-v1; every figure is an estimate.

