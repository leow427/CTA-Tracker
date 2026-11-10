#include "stress.h"

#ifdef POWER_LOG

#include <math.h>

#include "live.h"
#include "power_log.h"

namespace stress {

namespace {

using ui::Screen;
using ui::Tab;

struct Phase {
    uint8_t id;
    uint32_t ms;
};
constexpr uint32_t kWarmMaxMs = 180000;   // phase 0 ends when every stop has been fetched once, or after this
constexpr Phase kPhases[] = {
    {1, 180000},   // dim idle
    {2, 120000},   // awake, still
    {3, 120000},   // scroll
    {4, 120000},   // screen churn
    {5, 180000},   // network burst, dim
    {6, 180000},   // everything
    {7, 120000},   // cool-down, dim
};
constexpr int kNPhases = sizeof(kPhases) / sizeof(kPhases[0]);
constexpr uint32_t kStillTapMs = 10000, kChurnStepMs = 1500, kMixScrollMs = 10000;

bool on = false;
bool startReset = false;   // reset the screens on the next tick (start() has no App)
int idx = -1;          // -1: warm-up
uint32_t phaseT0 = 0, nextAct = 0;
int churnStep = 0;
bool mixScrolling = true;   // phase 6: scrolling, or one churn loop
uint32_t mixT0 = 0;

// Synthetic touches. Stamps run up to `ms`, never past it: a later lastInput would read as a huge idle time.
void tapAt(ui::App& a, mock::Model& m, float x, float y, uint32_t ms) {
    ui::pointerDown(a, m, x, y, ms - 40);
    ui::pointerUp(a, m, x, y, ms);
}

void swipeHome(ui::App& a, mock::Model& m, uint32_t ms) {   // down from the top of Stops
    ui::pointerDown(a, m, 233, 40, ms - 64);
    ui::pointerMove(a, m, 233, 80, ms - 48);
    ui::pointerMove(a, m, 233, 120, ms - 32);
    ui::pointerMove(a, m, 233, 160, ms - 16);
    ui::pointerUp(a, m, 233, 160, ms);
}

void goDim(ui::App& a, uint32_t ms) {   // Home, and dim from the next tick (as if left alone 15 s)
    a.view.screen = Screen::Home;
    a.trans.on = false;
    a.lastInput = ms - 16000;
}

void wakeUp(ui::App& a, mock::Model& m, uint32_t ms) {
    if (!a.dim) return;
    plog::tap();
    tapAt(a, m, 233, 30, ms);   // a tap on the dim screen only wakes it
}

void scrollStep(ui::App& a, mock::Model& m, uint32_t ms) {
    if (a.view.screen == Screen::Home && !a.trans.on) { plog::tap(); tapAt(a, m, 233, 440, ms); return; }
    if (a.view.screen != Screen::Stops || a.trans.on) return;
    const int t = static_cast<int>(a.view.tab);
    a.scrollTarget[t] = 560 + 400 * sinf(ms / 700.f);   // as the serial "a" auto-scroll
    a.lastInput = ms;
}

void endScroll(ui::App& a) {   // come to rest on a card, as a release does
    const int t = static_cast<int>(a.view.tab);
    a.scrollTarget[t] = ui::kRow * roundf(a.scrollTarget[t] / ui::kRow);
}

// One step of Home -> Stops -> other tab -> a Detail -> back -> Home. Off-script screens are walked back first.
// Returns true when the step finished a loop.
bool churn(ui::App& a, mock::Model& m, uint32_t ms) {
    const Screen sc = a.view.screen;
    if (a.trans.on) return false;
    switch (churnStep) {
        case 0:
            if (sc == Screen::Home) { plog::tap(); tapAt(a, m, 233, 440, ms); churnStep = 1; return false; }
            break;
        case 1:
            if (sc == Screen::Stops) {
                plog::tap();
                tapAt(a, m, a.view.tab == Tab::Trains ? 350 : 116, 50, ms);
                churnStep = 2;
                return false;
            }
            break;
        case 2:
            if (sc == Screen::Stops) {
                endScroll(a);
                const int t = static_cast<int>(a.view.tab);
                a.scroll[t] = a.scrollTarget[t] = 0;   // the first card sits under the tap
                plog::tap();
                tapAt(a, m, 233, 206, ms);
                churnStep = 3;
                return false;
            }
            break;
        case 3:
            churnStep = 4;
            if (sc == Screen::Detail) { plog::tap(); tapAt(a, m, 40, 233, ms); return false; }
            if (sc == Screen::Stops) break;   // no card to open (an empty tab): straight on to Home
            break;
        case 4:
            if (sc == Screen::Stops) { plog::tap(); swipeHome(a, m, ms); churnStep = 0; return true; }
            break;
    }
    // Off script: walk back toward Home.
    if (sc == Screen::Detail) tapAt(a, m, 40, 233, ms);
    else if (sc == Screen::Stops) swipeHome(a, m, ms);
    else { a.view.screen = Screen::Home; a.trans.on = false; a.lastInput = ms; }
    const bool loopDone = churnStep == 4;
    churnStep = 0;
    return loopDone;
}

void enter(int i, ui::App& a, mock::Model& m, uint32_t ms) {
    idx = i;
    phaseT0 = nextAct = ms;
    churnStep = 0;
    mixScrolling = true;
    mixT0 = ms;
    const uint8_t id = kPhases[i].id;
    plog::event("phase,%u", static_cast<unsigned>(id));
    live::setBurst(id == 5 || id == 6);
    if (id == 1 || id == 5 || id == 7) goDim(a, ms);
    if (id == 2 || id == 3 || id == 4 || id == 6) {
        wakeUp(a, m, ms);
        a.view.screen = Screen::Home;
        a.trans.on = false;
        a.lastInput = ms;
    }
}

}  // namespace

void start(uint32_t ms) {
    on = true;
    idx = -1;
    phaseT0 = ms;
    // Every run starts from the screen a fresh boot shows (Home, the Trains tab, both lists at the top), so a run
    // without a reflash scrolls the same list as one with (run 3 of the baseline scrolled Buses, left over from the
    // run before).
    startReset = true;
    live::resetBurstCount();
    live::setBurst(false);
    plog::header();
    plog::event("start,%s", kVersion);
    plog::event("phase,0");
}

void stop(uint32_t ms) {
    (void)ms;
    if (!on) return;
    on = false;
    live::setBurst(false);
    plog::event("stopped");
}

bool running() { return on; }

uint8_t phase() { return on && idx >= 0 ? kPhases[idx].id : 0; }

void tick(ui::App& a, mock::Model& m, uint32_t ms, bool loaded) {
    if (!on) return;
    if (startReset) {
        startReset = false;
        a.view.screen = Screen::Home;
        a.view.tab = Tab::Trains;
        a.trans.on = false;
        for (int t = 0; t < 2; ++t) a.scroll[t] = a.scrollTarget[t] = 0;
        a.detailScroll = a.detailTarget = 0;
        a.lastInput = ms;
    }
    if (idx < 0) {   // warm-up: sit on Home until every stop is in
        if ((loaded && ms - phaseT0 > 5000) || ms - phaseT0 > kWarmMaxMs) {
            if (!loaded) plog::event("warmup_timeout");
            enter(0, a, m, ms);
        }
        return;
    }
    if (ms - phaseT0 >= kPhases[idx].ms) {
        if (kPhases[idx].id == 3 || kPhases[idx].id == 6) endScroll(a);
        if (idx + 1 >= kNPhases) {
            on = false;
            live::setBurst(false);
            plog::event("done");
            return;
        }
        enter(idx + 1, a, m, ms);
        return;
    }
    switch (kPhases[idx].id) {
        case 1: case 5: case 7:   // dim; if anything woke it, dim again
            if (!a.dim) goDim(a, ms);
            break;
        case 2:
            if (ms >= nextAct) { nextAct = ms + kStillTapMs; tapAt(a, m, 233, 30, ms); }   // empty space on Home
            break;
        case 3:
            scrollStep(a, m, ms);
            break;
        case 4:
            if (ms >= nextAct) { nextAct = ms + kChurnStepMs; churn(a, m, ms); }
            break;
        case 6:
            if (mixScrolling) {
                scrollStep(a, m, ms);
                if (ms - mixT0 >= kMixScrollMs) {
                    endScroll(a);
                    mixScrolling = false;
                    churnStep = 1;   // already on Stops
                    nextAct = ms + kChurnStepMs;
                }
            } else if (ms >= nextAct) {
                nextAct = ms + kChurnStepMs;
                if (churn(a, m, ms)) { mixScrolling = true; mixT0 = ms; }
            }
            break;
    }
}

}  // namespace stress

#endif  // POWER_LOG
