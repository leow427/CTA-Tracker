Report: docs/power/reports/final-v2.html
**r1v2n** (2 runs: 2026-11-10-r1v2n-1.csv, 2026-11-10-r1v2n-2.csv)

| Phase | Est. mA | Noise | vs ref | CPU busy c0/c1 % | Frames/s | FPS moving | Slowest frame ms | Tap→screen ms | Request ms (ok/all) | Lowest RAM KB |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 Dim idle | 43.6 | 0.1 | – | 13 / 6 | 2.0 | – | – | – | 951 (32/32) | 23 |
| 2 Awake, still | 58.3 | 0.3 | – | 9 / 4 | 2.0 | – | – | – | 885 (16/16) | 23 |
| 3 Scroll | 81.5 | 2.2 | – | 28 / 97 | 88.3 | 92.2 | 36 | 14 (max 27) | 1159 (22/22) | 22 |
| 4 Screen churn | 64.8 | 1.1 | – | 17 / 21 | 8.2 | 41.9 | 63 | 23 (max 141) | 810 (19/19) | 22 |
| 5 Network burst | 86.2 | 1.5 | – | 39 / 5 | 2.0 | – | – | – | 1023 (169/170) | 21 |
| 6 Everything | 117.1 | 0.7 | – | 53 / 68 | 52.9 | 72.0 | 52 | 19 (max 46) | 1323 (150/151) | 21 |
| 7 Cool-down | 43.3 | 0.2 | – | 13 / 6 | 2.0 | – | – | – | 824 (16/16) | 21 |

Projected battery life (typical day, 2000 mAh): **43.9 h** at 45.6 mA average. Model model-v2; every figure is an estimate.

**r2** (3 runs: 2026-11-10-r2-3.csv, 2026-11-10-r2-4.csv, 2026-11-10-r2-5.csv)

| Phase | Est. mA | Noise | vs ref | CPU busy c0/c1 % | Frames/s | FPS moving | Slowest frame ms | Tap→screen ms | Request ms (ok/all) | Lowest RAM KB |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 Dim idle | 41.6 | 2.5 | -4.5% | 12 / 4 | 1.0 | – | – | – | 666 (48/48) | 25 |
| 2 Awake, still | 48.7 | 1.8 | -16.5% | 13 / 6 | 2.0 | – | – | – | 755 (24/24) | 25 |
| 3 Scroll | 87.4 | 6.4 | +7.3% | 37 / 98 | 83.5 | 86.3 | 49 | 16 (max 28) | 832 (32/32) | 25 |
| 4 Screen churn | 56.8 | 1.8 | -12.4% | 20 / 22 | 8.3 | 40.7 | 48 | 21 (max 40) | 640 (30/30) | 25 |
| 5 Network burst | 71.6 | 10.4 | -17.0% | 27 / 4 | 1.0 | – | – | – | 737 (260/260) | 21 |
| 6 Everything | 99.3 | 3.9 | -15.2% | 43 / 68 | 52.8 | 72.8 | 50 | 19 (max 51) | 780 (249/249) | 21 |
| 7 Cool-down | 41.8 | 1.8 | -3.4% | 12 / 4 | 1.0 | – | – | – | 717 (26/26) | 21 |

Projected battery life (typical day, 2000 mAh): **46.3 h** at 43.2 mA average. Model model-v2; every figure is an estimate.

**r3** (2 runs: 2026-11-10-r3-1.csv, 2026-11-10-r3-2.csv)

| Phase | Est. mA | Noise | vs ref | CPU busy c0/c1 % | Frames/s | FPS moving | Slowest frame ms | Tap→screen ms | Request ms (ok/all) | Lowest RAM KB |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 Dim idle | 42.8 | 0.8 | -1.9% | 13 / 3 | 1.0 | – | – | – | 744 (29/29) | 22 |
| 2 Awake, still | 46.2 | 0.6 | -20.7% | 11 / 5 | 2.0 | – | – | – | 319 (16/16) | 22 |
| 3 Scroll | 88.7 | 9.2 | +8.9% | 56 / 91 | 122.1 | 128.3 | 47 | 12 (max 26) | 1542 (15/15) | 22 |
| 4 Screen churn | 53.6 | 2.6 | -17.2% | 18 / 21 | 9.5 | 47.2 | 48 | 17 (max 28) | 795 (17/17) | 22 |
| 5 Network burst | 69.2 | 2.3 | -19.8% | 28 / 3 | 1.0 | – | – | – | 665 (174/174) | 21 |
| 6 Everything | 111.0 | 1.9 | -5.2% | 58 / 65 | 68.9 | 93.2 | 41 | 16 (max 41) | 1123 (153/153) | 21 |
| 7 Cool-down | 43.2 | 0.5 | -0.2% | 14 / 3 | 1.0 | – | – | – | 805 (18/18) | 21 |

Projected battery life (typical day, 2000 mAh): **45.4 h** at 44.0 mA average. Model model-v2; every figure is an estimate.

**r3a** (2 runs: 2026-11-10-r3a-1.csv, 2026-11-10-r3a-2.csv)

| Phase | Est. mA | Noise | vs ref | CPU busy c0/c1 % | Frames/s | FPS moving | Slowest frame ms | Tap→screen ms | Request ms (ok/all) | Lowest RAM KB |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 Dim idle | 40.1 | 1.0 | -7.9% | 10 / 4 | 1.0 | – | – | – | 478 (30/30) | 25 |
| 2 Awake, still | 48.7 | 1.4 | -16.5% | 13 / 6 | 2.0 | – | – | – | 699 (16/16) | 25 |
| 3 Scroll | 86.4 | 0.4 | +6.0% | 24 / 99 | 76.0 | 78.3 | 48 | 18 (max 30) | 743 (22/22) | 25 |
| 4 Screen churn | 53.9 | 0.5 | -16.8% | 14 / 24 | 8.6 | 41.0 | 51 | 21 (max 47) | 315 (20/20) | 25 |
| 5 Network burst | 70.5 | 4.0 | -18.2% | 28 / 4 | 1.0 | – | – | – | 709 (174/174) | 21 |
| 6 Everything | 106.0 | 8.6 | -9.5% | 41 / 70 | 46.4 | 63.7 | 50 | 20 (max 51) | 899 (166/166) | 21 |
| 7 Cool-down | 41.2 | 0.1 | -4.8% | 11 / 4 | 1.0 | – | – | – | 561 (16/16) | 21 |

Projected battery life (typical day, 2000 mAh): **47.9 h** at 41.8 mA average. Model model-v2; every figure is an estimate.

