#pragma once
// The power analysis stress test, stress-v1 (docs/superpowers/specs/2026-11-09-handheld-power-analysis-design.md,
// section 3). POWER_LOG builds only. Serial "S" starts it; it then drives every input itself on exact timing, from
// the frame loop, and marks each phase with an `E,phase,...` line and the end with `E,done`.
#include <stdint.h>

#include "mock_data.h"
#include "ui.h"

namespace stress {

#ifdef POWER_LOG

constexpr const char* kVersion = "stress-v1";

void start(uint32_t ms);
void stop(uint32_t ms);   // serial "X": abandon a run
bool running();
uint8_t phase();          // 0 = warm-up or not running; 1-7 as in the spec
// Call every loop, before ui::tick. loaded: every stop has been fetched once.
void tick(ui::App& a, mock::Model& m, uint32_t ms, bool loaded);

#endif

}  // namespace stress
