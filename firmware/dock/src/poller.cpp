#include "poller.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform_util.h>
#include <string.h>

#include "alerts_weather.h"
#include "approach.h"
#include "cta_fetch.h"
#include "keys.h"
#include "local_time.h"
#include "net.h"
#include "scheduler.h"
#include "state.h"
#include "store.h"
#include "text.h"
#include "train_parse.h"
#include "urls.h"

namespace poller {

static constexpr size_t kBodyCap = 128 * 1024;       // arrivals, positions, vehicles, alerts
static constexpr size_t kPatternCap = 512 * 1024;    // a long route's getpatterns
static constexpr int kMaxBusRows = 4;                // patterns per picked bus stop
static constexpr size_t kMaxTrains = 160, kMaxBuses = 240;

static char* sBody;
static char* sPatterns;
static char sUrl[512];

// Approach rows per section, rebuilt when its stop changes (spec 6.4: bus patterns cached until then).
static cta::ApproachRow sRows[3][kMaxBusRows];
static int sRowCount[3];
static cta::PickedStop sBuilt[3];

static char sAlerted[16][cta::kIdLen + 1];
static int sAlertedCount = 0;
static bool sHaveGeo = false;
static uint32_t sGeoFailedAt = 0;
static double sLat, sLon;

static cta::Job sArrivals{cta::kDockPollSec}, sPositions{cta::kDockPollSec}, sAlerts{cta::kAlertsPollSec},
    sWeather{cta::kWeatherPollSec}, sGeo{cta::kGeoIpPollSec}, sRowsRetry{cta::kSlowPollSec};

static SemaphoreHandle_t sPickMutex;
static PickerLists* sPick;   // in PSRAM
static Ask sPickAsk = Ask::None;
static char sPickRoute[cta::kIdLen + 1], sPickDir[cta::kNameLen + 1];
static bool sPickPending = false;

static uint32_t uptime() { return millis() / 1000; }
static bool haveKeys() { return CTA_TRAIN_KEY[0] && CTA_BUS_KEY[0]; }

static bool alerted(const char* route) {
    for (int i = 0; i < sAlertedCount; i++)
        if (strcasecmp(sAlerted[i], route) == 0) return true;
    return false;
}

static bool samePick(const cta::PickedStop& a, const cta::PickedStop& b) {
    return a.type == b.type && !strcmp(a.id, b.id) && !strcmp(a.route, b.route);
}

// Rebuilds the rows of every section whose pick changed. Bus rows need a getpatterns fetch.
static void rebuildRows(const cta::DeviceConfig& cfg) {
    for (int s = 0; s < 3; s++) {
        const cta::PickedStop& p = cfg.stops[s];
        if (samePick(p, sBuilt[s]) && (sRowCount[s] > 0 || !p.id[0])) continue;
        sRowCount[s] = 0;
        sBuilt[s] = p;   // the old data was cleared when the pick changed (pickChanged)
        if (!p.id[0]) continue;
        if (p.type == cta::Kind::Train) {
            const cta::TrainLine* line = cta::trainLineFor(p.route, p.id);
            if (line && cta::buildTrainRow(line->stopIds, line->count, p.id, sRows[s][0])) sRowCount[s] = 1;
        } else if (cta::busPatternsUrl(sUrl, sizeof sUrl, CTA_BUS_KEY, p.route)) {
            int n = cta::ctaGet(sUrl, sPatterns, kPatternCap, 15000);
            if (n > 0) {
                int rows = cta::buildBusRows(sPatterns, n, p.id, sRows[s], kMaxBusRows);
                sRowCount[s] = rows > 0 ? rows : 0;
            }
        }
    }
}

static void fetchArrivals(const cta::DeviceConfig& cfg, uint32_t now) {
    bool anyOk = false, anyFail = false;
    cta::StopRecord fresh[3];
    bool got[3] = {};
    for (int s = 0; s < 3; s++) {
        const cta::PickedStop& p = cfg.stops[s];
        if (!p.id[0] || p.type != cta::Kind::Train) continue;
        fresh[s] = cta::StopRecord{};
        int n = cta::trainArrivalsUrl(sUrl, sizeof sUrl, CTA_TRAIN_KEY, p.id) ? cta::ctaGet(sUrl, sBody, kBodyCap) : -1;
        if (n > 0 && cta::parseTrainArrivals(sBody, n, now, p.route, fresh[s])) got[s] = anyOk = true;
        else anyFail = true;
    }
    // Every picked bus stop in one getpredictions call, then one record per stop for its own route.
    const char* ids[3];
    size_t nIds = 0;
    for (int s = 0; s < 3; s++) {
        const cta::PickedStop& p = cfg.stops[s];
        if (!p.id[0] || p.type != cta::Kind::Bus) continue;
        bool dup = false;
        for (size_t i = 0; i < nIds; i++) dup |= !strcmp(ids[i], p.id);
        if (!dup) ids[nIds++] = p.id;
    }
    if (nIds) {
        int n = cta::busPredictionsUrl(sUrl, sizeof sUrl, CTA_BUS_KEY, ids, nIds) ? cta::ctaGet(sUrl, sBody, kBodyCap) : -1;
        for (int s = 0; s < 3; s++) {
            const cta::PickedStop& p = cfg.stops[s];
            if (!p.id[0] || p.type != cta::Kind::Bus) continue;
            cta::StopRecord recs[3];
            int count = n > 0 ? cta::parseBusPredictions(sBody, n, now, p.route, recs, 3) : -1;
            if (count < 0) {
                anyFail = true;
                continue;
            }
            fresh[s] = cta::StopRecord{};
            fresh[s].type = cta::Kind::Bus;
            fresh[s].ts = now;
            for (int i = 0; i < count; i++)
                if (!strcmp(recs[i].id, p.id)) fresh[s] = recs[i];
            got[s] = anyOk = true;
        }
    }
    Lock l;
    for (int s = 0; s < 3; s++) {
        if (!got[s]) continue;
        const cta::PickedStop& p = gState.cfg.stops[s];
        if (!samePick(p, cfg.stops[s])) continue;   // the pick changed mid-fetch
        cta::StopRecord& r = fresh[s];
        strlcpy(r.id, p.id, sizeof r.id);
        strlcpy(r.route, p.route, sizeof r.route);
        strlcpy(r.dir, p.dir, sizeof r.dir);
        if (p.name[0]) strlcpy(r.name, p.name, sizeof r.name);
        r.alert = alerted(p.route);
        gState.recs[s] = r;
    }
    if (anyOk) gState.lastUpdate = now;
    gState.ctaError = anyFail && !anyOk;
}

static void fetchPositions(const cta::DeviceConfig& cfg, uint32_t now) {
    const char* trainRoutes[3];
    const char* busRoutes[3];
    size_t nT = 0, nB = 0;
    for (int s = 0; s < 3; s++) {
        const cta::PickedStop& p = cfg.stops[s];
        if (!p.id[0] || !sRowCount[s]) continue;
        const char** list = p.type == cta::Kind::Train ? trainRoutes : busRoutes;
        size_t& n = p.type == cta::Kind::Train ? nT : nB;
        bool dup = false;
        for (size_t i = 0; i < n; i++) dup |= !strcasecmp(list[i], p.route);
        if (!dup) list[n++] = p.route;
    }
    uint8_t masks[3] = {};
    bool ok[3] = {};
    if (nT) {
        static cta::TrainPos trains[kMaxTrains];
        int n = cta::trainPositionsUrl(sUrl, sizeof sUrl, CTA_TRAIN_KEY, trainRoutes, nT) ? cta::ctaGet(sUrl, sBody, kBodyCap) : -1;
        int count = n > 0 ? cta::parseTrainPositions(sBody, n, trains, kMaxTrains) : -1;
        for (int s = 0; s < 3 && count >= 0; s++)
            if (cfg.stops[s].id[0] && cfg.stops[s].type == cta::Kind::Train && sRowCount[s]) {
                masks[s] = cta::litMaskTrains(sRows[s][0], cfg.stops[s].route, trains, count);
                ok[s] = true;
            }
    }
    if (nB) {
        static cta::BusVehicle buses[kMaxBuses];
        int n = cta::busVehiclesUrl(sUrl, sizeof sUrl, CTA_BUS_KEY, busRoutes, nB) ? cta::ctaGet(sUrl, sBody, kBodyCap) : -1;
        int count = n > 0 ? cta::parseBusVehicles(sBody, n, buses, kMaxBuses) : -1;
        for (int s = 0; s < 3 && count >= 0; s++)
            if (cfg.stops[s].id[0] && cfg.stops[s].type == cta::Kind::Bus && sRowCount[s]) {
                masks[s] = cta::litMaskBuses(sRows[s], sRowCount[s], buses, count);
                ok[s] = true;
            }
    }
    Lock l;
    for (int s = 0; s < 3; s++) {
        if (!ok[s] || !samePick(gState.cfg.stops[s], cfg.stops[s])) continue;
        gState.masks[s] = masks[s];
        gState.maskTs[s] = now;
    }
}

static void fetchAlerts(const cta::DeviceConfig& cfg) {
    const char* routes[3];
    size_t n = 0;
    for (const auto& p : cfg.stops)
        if (p.id[0]) routes[n++] = p.route;
    if (!n) return;
    int len = cta::alertsUrl(sUrl, sizeof sUrl, routes, n) ? cta::ctaGet(sUrl, sBody, kBodyCap) : -1;
    if (len <= 0) return;
    int count = cta::parseAlertRoutes(sBody, len, sAlerted, 16);
    if (count < 0) return;
    sAlertedCount = count;
    Lock l;
    for (int s = 0; s < 3; s++)
        if (gState.recs[s].ts) gState.recs[s].alert = alerted(gState.cfg.stops[s].route);
}

static bool fetchGeo() {
    if (!cta::geoIpUrl(sUrl, sizeof sUrl)) return false;
    int n = cta::ctaGet(sUrl, sBody, kBodyCap);
    double lat, lon;
    if (n <= 0 || !cta::parseGeoIp(sBody, n, lat, lon)) return false;
    sLat = lat;
    sLon = lon;
    sHaveGeo = true;
    store::saveGeo(lat, lon);
    return true;
}

static void fetchWeather() {
    if (!sHaveGeo || !cta::weatherUrl(sUrl, sizeof sUrl, sLat, sLon)) return;
    int n = cta::ctaGet(sUrl, sBody, kBodyCap);
    int wmo;
    float t;
    if (n > 0 && cta::parseWeather(sBody, n, wmo, t)) {
        Lock l;
        gState.wmo = wmo;
        gState.tempC = t;
        gState.hasWeather = true;
    }
}

static void servePicker() {
    xSemaphoreTake(sPickMutex, portMAX_DELAY);
    bool pending = sPickPending;
    Ask ask = sPickAsk;
    char route[sizeof sPickRoute], dir[sizeof sPickDir];
    strcpy(route, sPickRoute);
    strcpy(dir, sPickDir);
    sPickPending = false;
    xSemaphoreGive(sPickMutex);
    if (!pending) return;
    bool built = ask == Ask::Routes ? cta::busRoutesUrl(sUrl, sizeof sUrl, CTA_BUS_KEY)
                 : ask == Ask::Directions ? cta::busDirectionsUrl(sUrl, sizeof sUrl, CTA_BUS_KEY, route)
                                          : cta::busStopsUrl(sUrl, sizeof sUrl, CTA_BUS_KEY, route, dir);
    int n = built && net::up() ? cta::ctaGet(sUrl, sBody, kBodyCap) : -1;
    int count = -1;
    xSemaphoreTake(sPickMutex, portMAX_DELAY);
    if (n > 0 && ask == sPickAsk && !sPickPending) {   // not superseded by a newer request
        if (ask == Ask::Routes) count = cta::parseBusRoutes(sBody, n, sPick->routes, kMaxRoutes);
        else if (ask == Ask::Directions) count = cta::parseBusDirections(sBody, n, sPick->dirs, kMaxDirs);
        else count = cta::parseBusStops(sBody, n, sPick->stops, kMaxStops);
    }
    if (ask == sPickAsk && !sPickPending) {
        sPick->ask = ask;
        sPick->count = count < 0 ? 0 : count;
        sPick->ok = count >= 0;
        sPick->done = true;
    }
    xSemaphoreGive(sPickMutex);
}

void askBus(Ask ask, const char* route, const char* dir) {
    xSemaphoreTake(sPickMutex, portMAX_DELAY);
    sPickAsk = ask;
    strlcpy(sPickRoute, route, sizeof sPickRoute);
    strlcpy(sPickDir, dir, sizeof sPickDir);
    sPickPending = true;
    sPick->done = false;
    xSemaphoreGive(sPickMutex);
}

bool busLists(PickerLists* out) {
    xSemaphoreTake(sPickMutex, portMAX_DELAY);
    bool ready = sPick->done && sPick->ask == sPickAsk && !sPickPending;
    if (ready) memcpy(out, sPick, sizeof *sPick);
    xSemaphoreGive(sPickMutex);
    return ready;
}

static void task(void*) {
    char ssid[33] = "", pass[64] = "";
    bool haveWifi = store::loadWifi(ssid, sizeof ssid, pass, sizeof pass);
    sHaveGeo = store::loadGeo(sLat, sLon);
    net::begin(haveWifi ? ssid : "", pass);
    mbedtls_platform_zeroize(pass, sizeof pass);
    uint32_t seenVersion = UINT32_MAX;
    for (;;) {
        net::poll();
        cta::DeviceConfig cfg;
        uint32_t version;
        uint8_t urgent;
        bool newWifi = false;
        char newSsid[33], newPass[64];
        {
            Lock l;
            gState.wifiUp = net::up();
            gState.timeValid = net::timeValid();
            gState.keyMissing = !haveKeys();
            if (gState.wifiChanged) {
                gState.wifiChanged = false;
                newWifi = true;
                strlcpy(newSsid, gState.ssid, sizeof newSsid);
                strlcpy(newPass, gState.pendingPass, sizeof newPass);
                mbedtls_platform_zeroize(gState.pendingPass, sizeof gState.pendingPass);
            }
            cfg = gState.cfg;
            version = gState.cfgVersion;
            urgent = gState.stopsChanged;
            gState.stopsChanged = 0;
        }
        if (newWifi) {   // outside the lock: flash writes and WiFi.disconnect() block
            store::saveWifi(newSsid, newPass);
            net::setCredentials(newSsid, newPass);
            mbedtls_platform_zeroize(newPass, sizeof newPass);
        }
        servePicker();   // answers at once with an error while offline, so the picker never hangs
        if (!net::up() || !net::timeValid()) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        uint32_t up = uptime(), now = net::now();
        bool slow = cta::inNightHours(net::minuteOfDay(), cfg.nightStart, cfg.nightEnd);
        // Daily, but with no location yet a failed lookup retries every 5 min, so one ipapi.co hiccup on a
        // fresh dock doesn't hide the weather for a day (spec 7.5). A cached location rides out failures.
        if (sGeo.isDue(up, false) && (!sGeoFailedAt || up - sGeoFailedAt >= cta::kSlowPollSec)) {
            if (fetchGeo() || sHaveGeo) {
                sGeo.markRun(up);
                sGeoFailedAt = 0;
            } else {
                sGeoFailedAt = up ? up : 1;
            }
        }
        if (sWeather.isDue(up, slow)) {
            fetchWeather();
            sWeather.markRun(up);
        }
        if (haveKeys()) {
            if (version != seenVersion || sRowsRetry.isDue(up, false)) {   // retry a failed getpatterns
                rebuildRows(cfg);
                seenVersion = version;
                sRowsRetry.markRun(up);
            }
            if (urgent) {
                sArrivals.reset();
                sPositions.reset();
                sAlerts.reset();
            }
            if (sAlerts.isDue(up, slow)) {
                fetchAlerts(cfg);
                sAlerts.markRun(up);
            }
            if (sArrivals.isDue(up, slow)) {
                fetchArrivals(cfg, now);
                sArrivals.markRun(up);
            }
            servePicker();
            if (!slow && sPositions.isDue(up, slow)) {   // LEDs are off at night
                fetchPositions(cfg, net::now());
                sPositions.markRun(up);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void begin() {
    sBody = (char*)heap_caps_malloc(kBodyCap, MALLOC_CAP_SPIRAM);
    sPatterns = (char*)heap_caps_malloc(kPatternCap, MALLOC_CAP_SPIRAM);
    sPick = (PickerLists*)heap_caps_calloc(1, sizeof(PickerLists), MALLOC_CAP_SPIRAM);
    sPickMutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(task, "net", 12288, nullptr, 1, nullptr, 0);
}

}  // namespace poller
