#pragma once
// Turns raw touch-controller reads into presses: down, moves, up, plus the two-finger debug hold.
//
// The CST9217 driver returns "no points" whenever it's read before the controller has a new report, not only when
// the finger is gone. Read every 8 ms, about one read in three comes back empty mid-swipe (gaps of 8–24 ms). So a
// lift only counts after liftMs with no point, and the up carries the last point's time, so release speed comes from
// real samples. Plain C++, so tools/touch_test.cpp can check it on a laptop.
#include <stdint.h>

namespace touchf {

enum class Kind : uint8_t { Down, Move, Up, Cancel, Cycle };
struct Event { Kind kind; int16_t x, y; uint32_t ms; };

class Filter {
public:
    static constexpr uint32_t kHoldMs = 650;   // two fingers held this long: the debug gesture

    explicit Filter(uint32_t liftMs = 40) : liftMs_(liftMs) {}

    // One read at ms: n points, (x, y) the first one in screen coordinates. Writes up to two events; returns how many.
    int feed(uint32_t ms, uint8_t n, int16_t x, int16_t y, Event* out) {
        int k = 0;
        if (n == 0) {
            if (ms - lastSeen_ < liftMs_) return 0;   // a gap between reports, not a lift
            if (down_) { out[k++] = {Kind::Up, x_, y_, lastSeen_}; down_ = false; }
            twoSince_ = 0;
            twoFired_ = false;
            return k;
        }
        lastSeen_ = ms;
        if (n >= 2) {
            if (!twoSince_) twoSince_ = ms;
            if (!twoFired_ && ms - twoSince_ > kHoldMs) {
                twoFired_ = true;
                if (down_) { out[k++] = {Kind::Cancel, x_, y_, ms}; down_ = false; }
                out[k++] = {Kind::Cycle, 0, 0, ms};
            }
            return k;
        }
        if (twoFired_) return 0;   // the rest of a debug hold, until every finger lifts
        x_ = x;
        y_ = y;
        out[k++] = {down_ ? Kind::Move : Kind::Down, x, y, ms};
        down_ = true;
        return k;
    }

private:
    uint32_t liftMs_;
    bool down_ = false, twoFired_ = false;
    uint32_t lastSeen_ = 0, twoSince_ = 0;
    int16_t x_ = 0, y_ = 0;
};

}  // namespace touchf
