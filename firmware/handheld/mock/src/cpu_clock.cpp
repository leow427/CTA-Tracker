#include "cpu_clock.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <soc/rtc.h>
#ifdef POWER_LOG
#include <esp_timer.h>
#include <freertos/task.h>
#endif

namespace cpuclk {

namespace {

// The hold count and the clock change together under this, so a release on one core can't undo a hold just taken on
// the other. Static: no heap, and it can't fail.
StaticSemaphore_t muBuf;
SemaphoreHandle_t mu = nullptr;
int holds = 0;

#ifdef POWER_LOG
// Under mu: the time and busy time at each clock since the last take().
uint32_t nowMhz = 0;
int64_t sinceUs = 0;
uint32_t idleSince[2] = {0, 0};
uint64_t usAt[3] = {}, busyUsAt[3] = {};

int slot(uint32_t mhz) { return mhz >= 240 ? 2 : mhz >= 160 ? 1 : 0; }

// Books the time since the last switch or take() to the clock it ran at. Busy time is the wall time less each core's
// idle task run time (FreeRTOS run-time stats, esp_timer µs), as the power log measures it.
void book() {
    const int64_t now = esp_timer_get_time();
    const uint64_t wall = static_cast<uint64_t>(now - sinceUs);
    sinceUs = now;
    uint64_t busy = 0;
    for (int c = 0; c < 2; ++c) {
        const uint32_t idle = ulTaskGetIdleRunTimeCounterForCore(c);
        const uint32_t d = idle - idleSince[c];   // unsigned: survives the 71-minute wrap
        idleSince[c] = idle;
        if (d < wall) busy += wall - d;
    }
    const int k = slot(nowMhz);
    usAt[k] += wall;
    busyUsAt[k] += busy;
}
#endif

// What Arduino's setCpuFrequencyMhz did here: 80, 160 and 240 MHz all keep the APB at 80 MHz, so only the CPU's
// divider changes.
void set(uint32_t to) {   // under mu
#ifdef POWER_LOG
    book();
#endif
    rtc_cpu_freq_config_t conf;
    if (rtc_clk_cpu_freq_mhz_to_config(to, &conf)) rtc_clk_cpu_freq_set_config_fast(&conf);
#ifdef POWER_LOG
    nowMhz = mhz();
#endif
}

}  // namespace

uint32_t mhz() {
    rtc_cpu_freq_config_t conf;
    rtc_clk_cpu_freq_get_config(&conf);
    return conf.freq_mhz;
}

void begin() {
    mu = xSemaphoreCreateMutexStatic(&muBuf);
#ifdef POWER_LOG
    nowMhz = mhz();
    sinceUs = esp_timer_get_time();
    for (int c = 0; c < 2; ++c) idleSince[c] = ulTaskGetIdleRunTimeCounterForCore(c);
#endif
}

void hold() {
    xSemaphoreTake(mu, portMAX_DELAY);
    if (holds++ == 0) set(kFastMhz);
    xSemaphoreGive(mu);
}

void release() {
    xSemaphoreTake(mu, portMAX_DELAY);
    if (holds > 0 && --holds == 0) set(kSlowMhz);
    xSemaphoreGive(mu);
}

#ifdef POWER_LOG
void take(Stats& out) {
    xSemaphoreTake(mu, portMAX_DELAY);
    book();
    for (int k = 0; k < 3; ++k) {
        out.ms[k] = static_cast<uint32_t>(usAt[k] / 1000);
        out.busyMs[k] = static_cast<uint32_t>(busyUsAt[k] / 1000);
        usAt[k] %= 1000;   // the remainders carry into the next sample, so rounding loses no time
        busyUsAt[k] %= 1000;
    }
    xSemaphoreGive(mu);
}
#endif

}  // namespace cpuclk
