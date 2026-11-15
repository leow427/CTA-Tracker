## fpsref (1 run)

| Phase | Temp °C | Busy c0 % | Busy c1 % | Frames/s | FPS dragging | Frame ms | draw | copy | wait | Slowest ms | Tap ms | Tap max | Drag ms | List ms | Request ms | Late scans | Min int KB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 Idle | 39.0 | 4.6 | 7.4 | 2.2 | – | 20.5 | 4.9 | 0.9 | 14.7 | – | – | – | – | – | 220 | 0.0 | 51 |
| 2 Screen churn | 40.3 | 6.2 | 13.7 | 3.7 | – | 36.4 | 13.4 | 8.2 | 14.8 | – | 57.8 | 92.0 | – | 786 | 313 | 0.0 | 48 |
| 3 Map pan | 42.3 | 5.8 | 76.0 | 10.9 | 11.0 | 89.3 | 51.9 | 17.3 | 20.1 | 176 | 118 | 176 | 91.5 | – | 191 | 0.0 | 48 |
| 4 Picker scroll | 43.0 | 5.4 | 67.2 | 16.2 | 17.1 | 55.7 | 22.1 | 17.7 | 15.9 | 59.0 | 58.0 | 59.0 | 57.9 | – | 182 | 0.0 | 48 |
| 5 Network burst | 43.0 | 12.0 | 9.4 | 3.1 | – | 24.5 | 7.4 | 2.5 | 14.6 | – | 23.3 | 38.0 | – | – | 306 | 0.0 | 48 |
| 6 Mixed | 43.7 | 11.6 | 46.4 | 9.3 | 14.1 | 62.9 | 30.1 | 16.0 | 16.8 | 176 | 72.2 | 204 | 74.7 | 587 | 270 | 0.0 | 48 |
| 7 Cool-down | 42.9 | 4.8 | 7.4 | 2.2 | – | 20.1 | 4.3 | 0.9 | 14.9 | – | – | – | – | – | 215 | 0.0 | 48 |

After boot: Wi-Fi up 2.6 s, Clock set 6.3 s, Every stop loaded 17.6 s.

## fpsdma (1 run)

| Phase | Temp °C | Busy c0 % | Busy c1 % | Frames/s | FPS dragging | Frame ms | draw | copy | wait | Slowest ms | Tap ms | Tap max | Drag ms | List ms | Request ms | Late scans | Min int KB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 Idle | 42.7 | 4.4 | 6.8 | 2.2 | – | 18.2 | 3.4 | 0.1 | 14.7 | – | – | – | – | – | 232 | 0.0 | 50 |
| 2 Screen churn | 43.1 | 6.1 | 10.4 | 3.7 | – | 28.7 | 10.8 | 1.5 | 16.4 | – | 44.0 | 64.0 | – | 674 | 282 | 0.0 | 50 |
| 3 Map pan | 44.3 | 6.2 | 68.3 | 15.2 | 15.7 | 62.0 | 42.6 | 1.5 | 18.0 | 175 | 63.8 | 147 | 63.1 | – | 182 | 0.0 | 50 |
| 4 Picker scroll | 45.0 | 5.9 | 60.8 | 30.1 | 34.1 | 28.3 | 17.1 | 1.9 | 9.3 | 59.0 | 28.9 | 30.0 | 29.1 | – | 184 | 0.0 | 50 |
| 5 Network burst | 44.5 | 11.7 | 9.3 | 3.1 | – | 24.6 | 9.6 | 0.6 | 14.5 | – | 28.4 | 30.0 | – | – | 343 | 0.0 | 50 |
| 6 Mixed | 45.3 | 12.8 | 41.3 | 14.4 | 25.1 | 37.9 | 24.1 | 1.7 | 12.1 | 176 | 39.9 | 67.0 | 45.6 | 656 | 300 | 0.0 | 50 |
| 7 Cool-down | 44.1 | 4.9 | 7.3 | 2.2 | – | 19.8 | 5.0 | 0.2 | 14.7 | – | – | – | – | – | 201 | 0.0 | 50 |

After boot: Wi-Fi up 2.6 s, Clock set 4.9 s, Every stop loaded 28.9 s.

## fpsdma against fpsref

| Phase | Metric | fpsref | fpsdma | Change | Counts beyond | Verdict |
|---|---|---|---|---|---|---|
| 1 Idle | Chip temp | 39.0 | 42.7 | +3.7 (+10%) | 1.0 °C | **worse** |
| 1 Idle | Frame time | 20.5 | 18.2 | -2.3 (-11%) | 2.0 ms | **better** |
| 2 Screen churn | Chip temp | 40.3 | 43.1 | +2.8 (+7%) | 1.0 °C | **worse** |
| 2 Screen churn | Core 1 busy | 13.7 | 10.4 | -3.3 (-24%) | 3.0 % | **better** |
| 2 Screen churn | Frame time | 36.4 | 28.7 | -7.7 (-21%) | 2.0 ms | **better** |
| 2 Screen churn | draw | 13.4 | 10.8 | -2.6 (-19%) | 2.0 ms | **better** |
| 2 Screen churn | copy | 8.2 | 1.5 | -6.7 (-82%) | 1.0 ms | **better** |
| 2 Screen churn | Tap to screen | 57.8 | 44.0 | -13.8 (-24%) | 5.0 ms | **better** |
| 2 Screen churn | Tap to screen (max) | 92.0 | 64.0 | -28.0 (-30%) | 20.0 ms | **better** |
| 2 Screen churn | Picker list load | 786 | 674 | -112.7 (-14%) | 100 ms | **better** |
| 2 Screen churn | Lowest free internal RAM | 48 | 50 | +2.9 (+6%) | 2.0 KB | **better** |
| 3 Map pan | Chip temp | 42.3 | 44.3 | +1.9 (+5%) | 1.0 °C | **worse** |
| 3 Map pan | Core 1 busy | 76.0 | 68.3 | -7.7 (-10%) | 3.0 % | **better** |
| 3 Map pan | FPS while dragging | 11.0 | 15.7 | +4.7 (+43%) | 1.0  | **better** |
| 3 Map pan | Frame time | 89.3 | 62.0 | -27.2 (-31%) | 2.0 ms | **better** |
| 3 Map pan | draw | 51.9 | 42.6 | -9.4 (-18%) | 2.0 ms | **better** |
| 3 Map pan | copy | 17.3 | 1.5 | -15.8 (-91%) | 1.0 ms | **better** |
| 3 Map pan | wait for panel | 20.1 | 18.0 | -2.1 (-10%) | 2.0 ms | **better** |
| 3 Map pan | Slowest frame (avg/s) | 137 | 127 | -9.9 (-7%) | 5.0 ms | **better** |
| 3 Map pan | Tap to screen | 118 | 63.8 | -54.7 (-46%) | 5.0 ms | **better** |
| 3 Map pan | Tap to screen (max) | 176 | 147 | -29.0 (-16%) | 20.0 ms | **better** |
| 3 Map pan | Drag to screen | 91.5 | 63.1 | -28.4 (-31%) | 5.0 ms | **better** |
| 3 Map pan | Lowest free internal RAM | 48 | 50 | +2.8 (+6%) | 2.0 KB | **better** |
| 4 Picker scroll | Chip temp | 43.0 | 45.0 | +1.9 (+4%) | 1.0 °C | **worse** |
| 4 Picker scroll | Core 1 busy | 67.2 | 60.8 | -6.4 (-10%) | 3.0 % | **better** |
| 4 Picker scroll | FPS while dragging | 17.1 | 34.1 | +17.0 (+100%) | 1.0  | **better** |
| 4 Picker scroll | Frame time | 55.7 | 28.3 | -27.4 (-49%) | 2.0 ms | **better** |
| 4 Picker scroll | draw | 22.1 | 17.1 | -4.9 (-22%) | 2.0 ms | **better** |
| 4 Picker scroll | copy | 17.7 | 1.9 | -15.8 (-89%) | 1.0 ms | **better** |
| 4 Picker scroll | wait for panel | 15.9 | 9.3 | -6.6 (-42%) | 2.0 ms | **better** |
| 4 Picker scroll | Slowest frame (avg/s) | 59.0 | 30.8 | -28.2 (-48%) | 5.0 ms | **better** |
| 4 Picker scroll | Tap to screen | 58.0 | 28.9 | -29.1 (-50%) | 5.0 ms | **better** |
| 4 Picker scroll | Tap to screen (max) | 59.0 | 30.0 | -29.0 (-49%) | 20.0 ms | **better** |
| 4 Picker scroll | Drag to screen | 57.9 | 29.1 | -28.8 (-50%) | 5.0 ms | **better** |
| 4 Picker scroll | Lowest free internal RAM | 48 | 50 | +2.8 (+6%) | 2.0 KB | **better** |
| 5 Network burst | Chip temp | 43.0 | 44.5 | +1.5 (+3%) | 1.0 °C | **worse** |
| 5 Network burst | draw | 7.4 | 9.6 | +2.1 (+29%) | 2.0 ms | **worse** |
| 5 Network burst | copy | 2.5 | 0.6 | -1.9 (-77%) | 1.0 ms | **better** |
| 5 Network burst | Tap to screen | 23.3 | 28.4 | +5.0 (+22%) | 5.0 ms | **worse** |
| 5 Network burst | Lowest free internal RAM | 48 | 50 | +2.8 (+6%) | 2.0 KB | **better** |
| 6 Mixed | Chip temp | 43.7 | 45.3 | +1.6 (+4%) | 1.0 °C | **worse** |
| 6 Mixed | Core 1 busy | 46.4 | 41.3 | -5.2 (-11%) | 3.0 % | **better** |
| 6 Mixed | FPS while dragging | 14.1 | 25.1 | +11.0 (+78%) | 1.0  | **better** |
| 6 Mixed | Frame time | 62.9 | 37.9 | -25.0 (-40%) | 2.0 ms | **better** |
| 6 Mixed | draw | 30.1 | 24.1 | -6.0 (-20%) | 2.0 ms | **better** |
| 6 Mixed | copy | 16.0 | 1.7 | -14.3 (-89%) | 1.0 ms | **better** |
| 6 Mixed | wait for panel | 16.8 | 12.1 | -4.7 (-28%) | 2.0 ms | **better** |
| 6 Mixed | Slowest frame (avg/s) | 96.8 | 80.2 | -16.6 (-17%) | 5.0 ms | **better** |
| 6 Mixed | Tap to screen | 72.2 | 39.9 | -32.3 (-45%) | 5.0 ms | **better** |
| 6 Mixed | Tap to screen (max) | 204 | 67.0 | -137.0 (-67%) | 20.0 ms | **better** |
| 6 Mixed | Drag to screen | 74.7 | 45.6 | -29.1 (-39%) | 5.0 ms | **better** |
| 6 Mixed | Drag to screen (max) | 204 | 176 | -28.0 (-14%) | 20.0 ms | **better** |
| 6 Mixed | Lowest free internal RAM | 48 | 50 | +2.8 (+6%) | 2.0 KB | **better** |
| 7 Cool-down | Chip temp | 42.9 | 44.1 | +1.2 (+3%) | 1.0 °C | **worse** |
| 7 Cool-down | Lowest free internal RAM | 48 | 50 | +2.8 (+6%) | 2.0 KB | **better** |
| boot | Wi-Fi up (s) | 2.6 | 2.6 | +0.0 | – | (info) |
| boot | Clock set (s) | 6.3 | 4.9 | -1.4 | – | (info) |
| boot | Every stop loaded (s) | 17.6 | 28.9 | +11.2 | – | (info) |

46 better and 9 worse beyond noise; everything else within noise.
