Report: docs/power/reports/r3a.html
**r2** (2 runs: 2026-11-10-r2-4.csv, 2026-11-10-r2-5.csv)

| Phase | Est. mA | Noise | vs ref | CPU busy c0/c1 % | Frames/s | FPS moving | Slowest frame ms | Tap→screen ms | Request ms (ok/all) | Lowest RAM KB |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 Dim idle | 40.8 | 0.0 | – | 11 / 4 | 1.0 | – | – | – | 538 (32/32) | 25 |
| 2 Awake, still | 48.3 | 1.3 | – | 13 / 6 | 2.0 | – | – | – | 710 (16/16) | 25 |
| 3 Scroll | 89.4 | 0.9 | – | 40 / 99 | 77.7 | 79.9 | 49 | 17 (max 28) | 810 (21/21) | 25 |
| 4 Screen churn | 56.9 | 1.8 | – | 20 / 22 | 8.4 | 41.1 | 48 | 21 (max 40) | 682 (20/20) | 25 |
| 5 Network burst | 68.3 | 1.3 | – | 25 / 4 | 1.0 | – | – | – | 654 (175/175) | 22 |
| 6 Everything | 99.1 | 3.9 | – | 44 / 69 | 49.5 | 68.2 | 50 | 19 (max 51) | 755 (167/167) | 21 |
| 7 Cool-down | 42.2 | 1.4 | – | 12 / 4 | 1.0 | – | – | – | 676 (17/17) | 21 |

Projected battery life (typical day, 2000 mAh): **47.1 h** at 42.4 mA average. Model model-v2; every figure is an estimate.

**r3a** (2 runs: 2026-11-10-r3a-1.csv, 2026-11-10-r3a-2.csv)

| Phase | Est. mA | Noise | vs ref | CPU busy c0/c1 % | Frames/s | FPS moving | Slowest frame ms | Tap→screen ms | Request ms (ok/all) | Lowest RAM KB |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 Dim idle | 40.1 | 1.0 | -1.6% | 10 / 4 | 1.0 | – | – | – | 478 (30/30) | 25 |
| 2 Awake, still | 48.7 | 1.4 | +0.8% | 13 / 6 | 2.0 | – | – | – | 699 (16/16) | 25 |
| 3 Scroll | 86.4 | 0.4 | -3.4% | 24 / 99 | 76.0 | 78.3 | 48 | 18 (max 30) | 743 (22/22) | 25 |
| 4 Screen churn | 53.9 | 0.5 | -5.2% | 14 / 24 | 8.6 | 41.0 | 51 | 21 (max 47) | 315 (20/20) | 25 |
| 5 Network burst | 70.5 | 4.0 | +3.2% | 28 / 4 | 1.0 | – | – | – | 709 (174/174) | 21 |
| 6 Everything | 106.0 | 8.6 | +7.0% | 41 / 70 | 46.4 | 63.7 | 50 | 20 (max 51) | 899 (166/166) | 21 |
| 7 Cool-down | 41.2 | 0.1 | -2.3% | 11 / 4 | 1.0 | – | – | – | 561 (16/16) | 21 |

Projected battery life (typical day, 2000 mAh): **47.9 h** at 41.8 mA average. Model model-v2; every figure is an estimate.

