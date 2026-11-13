**core33** (3 runs: 2026-11-12-core33-2.csv, 2026-11-13-core33-1.csv, 2026-11-13-core33-2.csv)

| Phase | Est. mA | Noise | vs ref | CPU busy c0/c1 % | Frames/s | FPS moving | Slowest frame ms | Tap→screen ms | Request ms (ok/all) | Lowest RAM KB |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 Dim idle | 41.0 | 0.1 | – | 11 / 4 | 1.0 | – | – | – | 562 (45/45) | 22 |
| 2 Awake, still | 50.1 | 1.5 | – | 11 / 7 | 2.0 | – | – | – | 412 (26/26) | 22 |
| 3 Scroll | 89.1 | 0.0 | – | 40 / 99 | 73.5 | 75.7 | 49 | 18 (max 31) | 698 (20/20) | 22 |
| 4 Screen churn | 57.7 | 2.2 | – | 19 / 22 | 7.5 | 34.8 | 47 | 24 (max 45) | 457 (20/20) | 22 |
| 5 Network burst | 69.4 | 10.4 | – | 28 / 4 | 1.0 | – | – | – | 684 (260/260) | 19 |
| 6 Everything | 109.5 | 7.2 | – | 49 / 68 | 47.9 | 66.0 | 52 | 22 (max 42) | 1095 (224/225) | 19 |
| 7 Cool-down | 41.9 | 3.6 | – | 12 / 4 | 1.0 | – | – | – | 651 (24/24) | 19 |

Projected battery life (typical day, 2000 mAh): **46.8 h** at 42.7 mA average. Model model-v2; every figure is an estimate.
Excluded (disturbed): 2026-11-13-core33-1.csv phase 3, 2026-11-13-core33-1.csv phase 4.

**core32h** (3 runs: 2026-11-13-core32h-1.csv, 2026-11-13-core32h-2.csv, 2026-11-13-core32h-3.csv)

| Phase | Est. mA | Noise | vs ref | CPU busy c0/c1 % | Frames/s | FPS moving | Slowest frame ms | Tap→screen ms | Request ms (ok/all) | Lowest RAM KB |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 Dim idle | 40.7 | 1.0 | -0.6% | 10 / 4 | 1.0 | – | – | – | 515 (46/46) | 44 |
| 2 Awake, still | 48.2 | 1.0 | -3.8% | 10 / 5 | 2.0 | – | – | – | 348 (26/26) | 44 |
| 3 Scroll | 90.1 | 4.3 | +1.2% | 34 / 98 | 86.5 | 89.2 | 36 | 14 (max 25) | 711 (31/31) | 44 |
| 4 Screen churn | 59.5 | 0.4 | +3.1% | 18 / 21 | 10.0 | 48.3 | 37 | 18 (max 37) | 523 (20/20) | 44 |
| 5 Network burst | 68.2 | 0.7 | -1.7% | 26 / 4 | 1.0 | – | – | – | 614 (261/261) | 44 |
| 6 Everything | 98.7 | 11.7 | -9.8% | 39 / 68 | 57.5 | 78.5 | 38 | 17 (max 36) | 576 (262/262) | 44 |
| 7 Cool-down | 41.2 | 1.5 | -1.7% | 11 / 4 | 1.0 | – | – | – | 569 (24/24) | 44 |

Projected battery life (typical day, 2000 mAh): **47.2 h** at 42.4 mA average. Model model-v2; every figure is an estimate.
Excluded (disturbed): 2026-11-13-core32h-3.csv phase 4.

