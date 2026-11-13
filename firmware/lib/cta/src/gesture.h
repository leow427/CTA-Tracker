#pragma once
// The dock's touch gestures (mockup docs/ideas/2026-11-11-dock-ui-mockup.html): a press is a tap until it moves more
// than kMovePx; held kLongPressMs without moving it's a long press; lifted after a sideways move of more than
// kSwipePx, and more sideways than 1.5 times its height, it's a swipe.
#include <math.h>
#include <stdint.h>

namespace cta {

constexpr float kMovePx = 8, kSwipePx = 60;
constexpr uint32_t kLongPressMs = 600;

enum class Gesture : uint8_t { None, Tap, SwipeLeft, SwipeRight };

// Whether a press has moved off its spot (dx, dy from where it went down).
inline bool pressMoved(float dx, float dy) { return hypotf(dx, dy) > kMovePx; }

// What a press was when lifted: moved and held say what happened while it was down. A drag that isn't a swipe (or a
// long press) is None: what it did, it did while moving.
inline Gesture classifyRelease(float dx, float dy, bool moved, bool held) {
    if (!moved) return held ? Gesture::None : Gesture::Tap;
    if (fabsf(dx) > kSwipePx && fabsf(dx) > 1.5f * fabsf(dy)) return dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight;
    return Gesture::None;
}

// A press that went down at t0 has been held long enough by now. Signed, so a press stamped a little after `now`
// (the touch task's clock ran ahead of the frame's) is young, not 4 billion ms old.
inline bool longPressDue(uint32_t t0, uint32_t now) { return static_cast<int32_t>(now - t0) >= static_cast<int32_t>(kLongPressMs); }

}  // namespace cta
