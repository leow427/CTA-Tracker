#include "picker.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <string.h>

#include "display.h"
#include "line_colors.h"
#include "poller.h"
#include "state.h"
#include "text.h"
#include "approach.h"
#include "train_sequences.h"

// Spec 7.2: Train or Bus → line or route → direction → stop, from a scrolling list. Trains come from the
// bundled tables; buses come live from Bus Tracker.
namespace picker {

static constexpr int W = display::kWidth, H = display::kHeight;
static constexpr int kHeaderH = 96, kRowH = 72;

enum class Step : uint8_t { Kind, TrainLine, TrainDir, TrainStop, BusRoute, BusDir, BusStop };

static lv_obj_t *sRoot, *sTitle, *sBack, *sList, *sStatus;
static int sSection = 0;
static Step sStep = Step::Kind;
static char sRoute[cta::kIdLen + 1], sDir[cta::kNameLen + 1];
static poller::PickerLists* sLists;   // PSRAM copy of the last Bus Tracker answer
static bool sWaiting = false;

static void show(Step step);

static lv_obj_t* text(lv_obj_t* parent, const lv_font_t* font, uint32_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

// A row in the list: a raised navy button with a colored bar, a title and an optional subtitle.
static lv_obj_t* row(const char* title, const char* sub, uint32_t bar, lv_event_cb_t cb, void* data) {
    lv_obj_t* b = lv_obj_create(sList);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, W - 32, kRowH);
    lv_obj_set_style_bg_color(b, lv_color_hex(cta::kRaised), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x22386A), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(b, lv_color_white(), 0);
    lv_obj_set_style_border_opa(b, 40, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
    lv_obj_t* stripe = lv_obj_create(b);
    lv_obj_remove_style_all(stripe);
    lv_obj_set_size(stripe, 12, kRowH);
    lv_obj_set_style_bg_color(stripe, lv_color_hex(bar), 0);
    lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
    lv_obj_remove_flag(stripe, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* t = text(b, &lv_font_montserrat_24, 0xFFFFFF);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, W - 80);
    lv_label_set_text(t, title);
    lv_obj_set_pos(t, 28, sub && *sub ? 10 : 22);
    if (sub && *sub) {
        lv_obj_t* s = text(b, &lv_font_montserrat_16, cta::kMist);
        lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
        lv_obj_set_width(s, W - 80);
        lv_label_set_text(s, sub);
        lv_obj_set_pos(s, 28, 42);
    }
    return b;
}

static void status(const char* msg) {
    if (msg) {
        lv_label_set_text(sStatus, msg);
        lv_obj_remove_flag(sStatus, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(sStatus, LV_OBJ_FLAG_HIDDEN);
    }
}

static void save(cta::Kind kind, const char* route, const char* dir, const char* id, const char* name) {
    {
        Lock l;
        cta::PickedStop& p = gState.cfg.stops[sSection];
        p = cta::PickedStop{};
        p.type = kind;
        cta::copyTruncated(p.route, route, cta::kIdLen);
        cta::copyTruncated(p.dir, dir, cta::kNameLen);
        cta::copyTruncated(p.id, id, cta::kIdLen);
        cta::copyTruncated(p.name, name, cta::kNameLen);
        gState.cfgVersion++;
        pickChanged(sSection);
        gState.cfgDirty = true;   // the most recent pick wins, from here or the app (spec 7.2)
    }
    lv_obj_add_flag(sRoot, LV_OBJ_FLAG_HIDDEN);
}

// ---------- trains, from the bundled tables ----------

static constexpr size_t kMaxStations = 128;
static cta::StationRef sStations[kMaxStations];
static const char* sTrainDir = "";   // the full label from the table: some are over 24 bytes

static void onTrainStop(lv_event_t* e) {
    const cta::StationRef& s = sStations[(size_t)lv_event_get_user_data(e)];
    save(cta::Kind::Train, s.line->route, s.line->dirLabel, s.line->stopIds[s.index], s.line->names[s.index]);
}

static void onTrainDir(lv_event_t* e) {
    sTrainDir = cta::kTrainLines[(size_t)lv_event_get_user_data(e)].dirLabel;
    show(Step::TrainStop);
}

static void onTrainLine(lv_event_t* e) {
    strlcpy(sRoute, cta::kTrainLineInfo[(size_t)lv_event_get_user_data(e)].route, sizeof sRoute);
    show(Step::TrainDir);
}

static void listTrainLines() {
    for (size_t i = 0; i < 8; i++) {
        const cta::TrainLineInfo& l = cta::kTrainLineInfo[i];
        row(l.name, nullptr, l.rgb, onTrainLine, (void*)i);
    }
}

static void listTrainDirs() {
    // One row per direction label; a (line, direction) can span several sequences (plan 1 task 10).
    for (size_t i = 0; i < cta::kTrainLineCount; i++) {
        const cta::TrainLine& l = cta::kTrainLines[i];
        if (strcmp(l.route, sRoute)) continue;
        bool seen = false;
        for (size_t j = 0; j < i; j++)
            seen |= !strcmp(cta::kTrainLines[j].route, sRoute) && !strcmp(cta::kTrainLines[j].dirLabel, l.dirLabel);
        if (!seen) row(l.dirLabel, nullptr, cta::lineColor(sRoute), onTrainDir, (void*)i);
    }
}

static void listTrainStops() {
    size_t n = cta::trainStations(sRoute, sTrainDir, sStations, kMaxStations);
    for (size_t i = 0; i < n; i++)
        row(sStations[i].line->names[sStations[i].index], nullptr, cta::lineColor(sRoute), onTrainStop, (void*)i);
}

// ---------- buses, live from Bus Tracker ----------

static void onBusStop(lv_event_t* e) {
    const cta::BusStop& s = sLists->stops[(size_t)lv_event_get_user_data(e)];
    save(cta::Kind::Bus, sRoute, sDir, s.id, s.name);
}

static void onBusDir(lv_event_t* e) {
    strlcpy(sDir, sLists->dirs[(size_t)lv_event_get_user_data(e)], sizeof sDir);
    show(Step::BusStop);
}

static void onBusRoute(lv_event_t* e) {
    strlcpy(sRoute, sLists->routes[(size_t)lv_event_get_user_data(e)].rt, sizeof sRoute);
    show(Step::BusDir);
}

static void onKind(lv_event_t* e) { show((size_t)lv_event_get_user_data(e) ? Step::BusRoute : Step::TrainLine); }

static void onBack(lv_event_t*) {
    switch (sStep) {
        case Step::Kind: lv_obj_add_flag(sRoot, LV_OBJ_FLAG_HIDDEN); break;
        case Step::TrainLine:
        case Step::BusRoute: show(Step::Kind); break;
        case Step::TrainDir: show(Step::TrainLine); break;
        case Step::TrainStop: show(Step::TrainDir); break;
        case Step::BusDir: show(Step::BusRoute); break;
        case Step::BusStop: show(Step::BusDir); break;
    }
}

static void onRetry(lv_event_t*) { show(sStep); }

static void fillBusList() {
    if (!sLists->ok) {
        status(nullptr);
        row("Couldn't reach Bus Tracker", "Tap to try again", cta::kAmber, onRetry, nullptr);
        return;
    }
    status(sLists->count ? nullptr : "Nothing here right now");
    uint32_t bus = cta::kBusAccent;
    for (int i = 0; i < sLists->count; i++) {
        if (sStep == Step::BusRoute) row(sLists->routes[i].rt, sLists->routes[i].name, bus, onBusRoute, (void*)(size_t)i);
        else if (sStep == Step::BusDir) row(sLists->dirs[i], nullptr, bus, onBusDir, (void*)(size_t)i);
        else row(sLists->stops[i].name, sLists->stops[i].id, bus, onBusStop, (void*)(size_t)i);
    }
}

static void show(Step step) {
    sStep = step;
    lv_obj_clean(sList);
    lv_obj_scroll_to_y(sList, 0, LV_ANIM_OFF);
    status(nullptr);
    sWaiting = false;
    char title[48];
    switch (step) {
        case Step::Kind:
            snprintf(title, sizeof title, "Stop %d", sSection + 1);
            row("Train", "L lines and stations", cta::lineColor("Red"), onKind, (void*)0);
            row("Bus", "Routes from Bus Tracker", cta::kBusAccent, onKind, (void*)1);
            break;
        case Step::TrainLine: strcpy(title, "Train line"); listTrainLines(); break;
        case Step::TrainDir: strcpy(title, "Direction"); listTrainDirs(); break;
        case Step::TrainStop: strlcpy(title, sTrainDir, sizeof title); listTrainStops(); break;
        case Step::BusRoute:
        case Step::BusDir:
        case Step::BusStop:
            strcpy(title, step == Step::BusRoute ? "Bus route" : step == Step::BusDir ? "Direction" : "Stop");
            if (step != Step::BusRoute) snprintf(title, sizeof title, "%s %s", sRoute, step == Step::BusDir ? "direction" : sDir);
            status("Loading...");
            sWaiting = true;
            poller::askBus(step == Step::BusRoute ? poller::Ask::Routes
                           : step == Step::BusDir ? poller::Ask::Directions
                                                  : poller::Ask::Stops,
                           sRoute, sDir);
            break;
    }
    lv_label_set_text(sTitle, title);
}

static void poll(lv_timer_t*) {
    if (!sWaiting || lv_obj_has_flag(sRoot, LV_OBJ_FLAG_HIDDEN)) return;
    if (poller::busLists(sLists)) {
        sWaiting = false;
        fillBusList();
    }
}

void open(int section) {
    sSection = section;
    lv_obj_remove_flag(sRoot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(sRoot);
    show(Step::Kind);
}

void begin() {
    sLists = (poller::PickerLists*)heap_caps_calloc(1, sizeof(poller::PickerLists), MALLOC_CAP_SPIRAM);
    sRoot = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(sRoot);
    lv_obj_set_size(sRoot, W, H);
    lv_obj_set_style_bg_color(sRoot, lv_color_hex(cta::kNavy), 0);
    lv_obj_set_style_bg_opa(sRoot, LV_OPA_COVER, 0);
    lv_obj_add_flag(sRoot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(sRoot, LV_OBJ_FLAG_SCROLLABLE);

    sBack = lv_obj_create(sRoot);
    lv_obj_remove_style_all(sBack);
    lv_obj_set_pos(sBack, 16, 24);
    lv_obj_set_size(sBack, 112, 52);
    lv_obj_set_style_bg_color(sBack, lv_color_hex(cta::kHeadlight), 0);
    lv_obj_set_style_bg_opa(sBack, LV_OPA_COVER, 0);
    lv_obj_add_flag(sBack, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sBack, onBack, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* back = text(sBack, &lv_font_montserrat_20, cta::kNavy);
    lv_label_set_text(back, LV_SYMBOL_LEFT " Back");
    lv_obj_center(back);

    sTitle = text(sRoot, &lv_font_montserrat_28, 0xFFFFFF);
    lv_obj_set_pos(sTitle, 144, 34);
    lv_obj_set_width(sTitle, W - 160);
    lv_label_set_long_mode(sTitle, LV_LABEL_LONG_DOT);

    sList = lv_obj_create(sRoot);
    lv_obj_remove_style_all(sList);
    lv_obj_set_pos(sList, 16, kHeaderH);
    lv_obj_set_size(sList, W - 32, H - kHeaderH);
    lv_obj_set_flex_flow(sList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sList, 10, 0);
    lv_obj_set_style_pad_bottom(sList, 24, 0);
    lv_obj_set_scroll_dir(sList, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(sList, LV_SCROLLBAR_MODE_AUTO);

    sStatus = text(sRoot, &lv_font_montserrat_24, cta::kMist);
    lv_obj_set_width(sStatus, W);
    lv_obj_set_style_text_align(sStatus, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(sStatus, 0, H / 2 - 12);
    lv_obj_add_flag(sStatus, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(sRoot, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(poll, 100, nullptr);
}

bool isOpen() { return sRoot && !lv_obj_has_flag(sRoot, LV_OBJ_FLAG_HIDDEN); }

}  // namespace picker
