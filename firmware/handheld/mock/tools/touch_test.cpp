// Checks src/touch_filter.h on a laptop: presses survive the CST9217's empty reads, real lifts still end them, and
// the two-finger hold still fires. Optionally replays a touch log recorded with the device's serial "t" command
// ("T d|m|u x y ms" lines) and counts the presses with and without the lift delay.
// Build and run (from firmware/handheld/mock):
//   c++ -std=c++17 -Isrc tools/touch_test.cpp -o /tmp/touch_test && /tmp/touch_test [touch log]
#include <stdio.h>

#include <vector>

#include "touch_filter.h"

namespace {

using touchf::Event;
using touchf::Filter;
using touchf::Kind;

int failures = 0;
void check(bool ok, const char* what) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

struct Read { uint32_t ms; uint8_t n; int16_t x, y; };

std::vector<Event> run(Filter f, const std::vector<Read>& reads) {
    std::vector<Event> out;
    Event ev[2];
    for (const Read& r : reads) {
        const int k = f.feed(r.ms, r.n, r.x, r.y, ev);
        out.insert(out.end(), ev, ev + k);
    }
    return out;
}
int count(const std::vector<Event>& evs, Kind k) {
    int n = 0;
    for (const Event& e : evs) n += e.kind == k;
    return n;
}

// Reads every 8 ms from t0 for ms; points where `touching`, except the controller's pattern of 3 good reads then 2
// empty ones while it's between reports.
std::vector<Read> reads(uint32_t t0, uint32_t ms, uint8_t fingers, bool dropouts, int16_t x0, int16_t y0, int16_t dy) {
    std::vector<Read> r;
    for (uint32_t t = 0, i = 0; t < ms; t += 8, ++i) {
        const bool empty = dropouts && i % 5 >= 3;
        r.push_back({t0 + t, empty ? uint8_t{0} : fingers, x0, static_cast<int16_t>(y0 + dy * static_cast<int>(i))});
    }
    return r;
}
std::vector<Read> idle(uint32_t t0, uint32_t ms) { return reads(t0, ms, 0, false, 0, 0, 0); }
std::vector<Read> join(std::initializer_list<std::vector<Read>> parts) {
    std::vector<Read> all;
    for (const auto& p : parts) all.insert(all.end(), p.begin(), p.end());
    return all;
}

}  // namespace

int main(int argc, char** argv) {
    {   // one swipe up, with the controller's empty reads all through it
        const auto evs = run(Filter(), join({reads(1000, 400, 1, true, 233, 400, -6), idle(1400, 200)}));
        check(count(evs, Kind::Down) == 1 && count(evs, Kind::Up) == 1, "a swipe with empty reads is one press");
        const Event& up = evs.back();
        check(up.kind == Kind::Up && up.ms == 1000 + 392 - 8 * 2 && up.y == 400 - 6 * 47,
              "the up carries the last point and its time");
    }
    {   // the old behaviour: any empty read is a lift
        const auto evs = run(Filter(0), join({reads(1000, 400, 1, true, 233, 400, -6), idle(1400, 200)}));
        check(count(evs, Kind::Down) == 10, "without the lift delay the same swipe is ten presses");
    }
    {   // two quick taps 60 ms apart stay two taps
        const auto evs = run(Filter(), join({reads(1000, 24, 1, false, 100, 100, 0), idle(1024, 60),
                                             reads(1084, 24, 1, false, 100, 100, 0), idle(1108, 100)}));
        check(count(evs, Kind::Down) == 2 && count(evs, Kind::Up) == 2, "two taps 60 ms apart are two presses");
    }
    {   // a two-finger hold with empty reads still fires once, after 650 ms
        const auto evs = run(Filter(), join({reads(1000, 900, 2, true, 0, 0, 0), idle(1900, 100)}));
        check(count(evs, Kind::Cycle) == 1, "a two-finger hold with empty reads fires the debug gesture once");
    }
    {   // one finger, then a second: the first press is cancelled, then the hold fires
        const auto evs = run(Filter(), join({reads(1000, 100, 1, true, 50, 50, 0), reads(1100, 800, 2, true, 0, 0, 0),
                                             idle(1900, 100)}));
        check(count(evs, Kind::Cancel) == 1 && count(evs, Kind::Cycle) == 1 && count(evs, Kind::Up) == 0,
              "a second finger cancels the first press, then the hold fires");
    }
    if (argc > 1) {   // replay a recorded log: rebuild the 8 ms reads, empty between an up and the next down
        std::vector<Read> log;
        FILE* f = fopen(argv[1], "r");
        if (!f) { printf("can't open %s\n", argv[1]); return 1; }
        char line[128], k;
        int x, y;
        unsigned ms;
        uint32_t emptyFrom = 0;
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "T %c %d %d %u", &k, &x, &y, &ms) != 4) continue;
            if (emptyFrom)
                for (uint32_t t = emptyFrom; t < ms; t += 8) log.push_back({t, 0, 0, 0});
            emptyFrom = 0;
            if (k == 'u') { log.push_back({ms, 0, 0, 0}); emptyFrom = ms + 8; }
            else log.push_back({ms, 1, static_cast<int16_t>(x), static_cast<int16_t>(y)});
        }
        fclose(f);
        for (uint32_t t = 0; t < 200; t += 8) log.push_back({log.back().ms + 8, 0, 0, 0});
        printf("replay of %s: %d presses lifting on any empty read, %d with the lift delay\n", argv[1],
               count(run(Filter(0), log), Kind::Down), count(run(Filter(), log), Kind::Down));
    }
    printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
