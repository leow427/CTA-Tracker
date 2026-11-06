// Renders the dock's real UI code (src/ui.cpp, src/picker.cpp) headless on the laptop and writes PNG
// screenshots to preview/out/, for checking the look without the board:
//   cd firmware/dock && pio run -e preview -t exec
// Hardware, network and BLE are stand-ins below; the data is a fixed sample.
#include <Arduino.h>
#include <lvgl.h>
#include <sys/stat.h>
#include <zlib.h>

#include <string>
#include <vector>

#include "display.h"
#include "net.h"
#include "picker.h"
#include "poller.h"
#include "state.h"
#include "store.h"
#include "ui.h"

PreviewSerial Serial;
static uint32_t sMs = 0;
uint32_t millis() { return sMs; }

static const uint32_t kNow = 1762351200;   // 2026-11-05 08:00 CST
namespace net {
uint32_t now() { return kNow; }
bool timeValid() { return true; }
bool up() { return gState.wifiUp; }
int minuteOfDay() { return 8 * 60; }
}  // namespace net
namespace display {
int touchCount() { return 0; }
}
namespace store {
void saveConfig(const cta::DeviceConfig&) {}
}
namespace poller {
void askBus(Ask, const char*, const char*) {}
bool busLists(PickerLists*) { return false; }
}  // namespace poller

static std::vector<uint16_t> sFb(display::kWidth* display::kHeight);

static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    const uint16_t* src = (const uint16_t*)px;
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) sFb[y * display::kWidth + x] = *src++;
    lv_display_flush_ready(d);
}

static void writePng(const std::string& path) {
    const int W = display::kWidth, H = display::kHeight;
    std::vector<uint8_t> raw;
    for (int y = 0; y < H; y++) {
        raw.push_back(0);
        for (int x = 0; x < W; x++) {
            uint16_t c = sFb[y * W + x];
            raw.push_back(((c >> 11) & 0x1F) * 255 / 31);
            raw.push_back(((c >> 5) & 0x3F) * 255 / 63);
            raw.push_back((c & 0x1F) * 255 / 31);
        }
    }
    uLongf zn = compressBound(raw.size());
    std::vector<uint8_t> z(zn);
    compress2(z.data(), &zn, raw.data(), raw.size(), 9);
    FILE* f = fopen(path.c_str(), "wb");
    auto be = [&](uint32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; fwrite(b, 1, 4, f); };
    auto chunk = [&](const char* tag, const uint8_t* d, uint32_t n) {
        be(n);
        std::vector<uint8_t> c(tag, tag + 4);
        c.insert(c.end(), d, d + n);
        fwrite(c.data(), 1, c.size(), f);
        be(crc32(0, c.data(), c.size()));
    };
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13] = {0, 0, (uint8_t)(W >> 8), (uint8_t)W, 0, 0, (uint8_t)(H >> 8), (uint8_t)H, 8, 2, 0, 0, 0};
    chunk("IHDR", ihdr, 13);
    chunk("IDAT", z.data(), zn);
    chunk("IEND", nullptr, 0);
    fclose(f);
    printf("wrote %s\n", path.c_str());
}

static void frames(int n) {
    for (int i = 0; i < n; i++) {
        sMs += 250;
        ui::tick();
        lv_timer_handler();
    }
}

static cta::PickedStop pick(cta::Kind k, const char* route, const char* dir, const char* id, const char* name) {
    cta::PickedStop p{};
    p.type = k;
    strcpy(p.route, route);
    strcpy(p.dir, dir);
    strcpy(p.id, id);
    strcpy(p.name, name);
    return p;
}

static cta::StopRecord rec(const cta::PickedStop& p, std::initializer_list<cta::Arrival> a, bool alert, uint32_t ts) {
    cta::StopRecord r{};
    strcpy(r.id, p.id);
    r.type = p.type;
    strcpy(r.route, p.route);
    strcpy(r.dir, p.dir);
    strcpy(r.name, p.name);
    for (const auto& x : a) r.arrivals[r.count++] = x;
    r.alert = alert;
    r.ts = ts;
    return r;
}

static void sample(bool hyped) {
    gState = DockState{};
    gState.cfg = cta::defaultConfig();
    gState.cfg.stops[0] = pick(cta::Kind::Train, "Red", "95th/Dan Ryan", "30255", "Belmont");
    gState.cfg.stops[1] = pick(cta::Kind::Train, "Brn", "Loop", "30257", "Belmont");
    gState.cfg.stops[2] = pick(cta::Kind::Bus, "22", "Southbound", "1001", "Clark & Belmont");
    gState.recs[0] = rec(gState.cfg.stops[0], {{(int16_t)(hyped ? 2 : 6), "95th/Dan Ryan", true, false}, {11, "95th/Dan Ryan", true, false}, {19, "95th/Dan Ryan", false, false}}, false, kNow - 10);
    gState.recs[1] = rec(gState.cfg.stops[1], {{4, "Loop", true, false}, {12, "Loop", true, true}, {20, "Loop", true, false}}, true, kNow - 10);
    gState.recs[2] = rec(gState.cfg.stops[2], {{0, "Harrison", true, false}, {8, "Harrison", true, true}, {23, "Harrison", false, false}}, false, kNow - 10);
    gState.hasWifiConfig = gState.wifiUp = gState.timeValid = true;
    gState.hasWeather = true;
    gState.wmo = 61;
    gState.tempC = 8;
    gState.cfgVersion++;
}

int main() {
    mkdir("preview/out", 0755);
    stateBegin();
    lv_init();
    lv_tick_set_cb(millis);
    lv_display_t* d = lv_display_create(display::kWidth, display::kHeight);
    static uint8_t buf[display::kWidth * 80 * 2];
    lv_display_set_buffers(d, buf, nullptr, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);
    ui::begin();

    sample(false);
    frames(4);
    writePng("preview/out/1_live.png");

    sample(true);
    frames(4);
    writePng("preview/out/2_hyped.png");

    sample(false);
    gState.wifiUp = false;
    for (auto& r : gState.recs) r.ts = kNow - 7 * 60;
    gState.hasWeather = false;
    frames(4);
    writePng("preview/out/3_offline.png");

    sample(false);
    gState.cfg.stops[1] = cta::PickedStop{};
    gState.recs[1] = cta::StopRecord{};
    gState.recs[2].ts = 0;
    gState.recs[0].count = 0;
    gState.wmo = 73;
    gState.tempC = -3;
    frames(4);
    writePng("preview/out/4_empty_loading.png");

    picker::open(0);
    frames(2);
    writePng("preview/out/5_picker.png");
    lv_obj_t* top = lv_layer_top();
    (void)top;

    sample(false);
    gState.passkey = 482913;
    frames(4);
    writePng("preview/out/6_passkey.png");

    sample(false);
    gState.hasWifiConfig = false;
    frames(4);
    writePng("preview/out/7_setup.png");
    return 0;
}
