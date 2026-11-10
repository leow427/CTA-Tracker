#pragma once
// The CPU clock: 240 MHz whenever anything the user could notice is running, 80 MHz otherwise. At 240 MHz the chip
// draws about 10 mA more than at 80 with both cores idle (power analysis, docs/power/log.md), and the dim always-on
// screen, the handheld's usual state, keeps the cores only a few percent busy.
//
// Each reason to run at full speed takes a hold, from any task; the clock drops when the last one is released. The
// loop holds it while it draws and sends a frame, while anything moves, and while a finger is down, and the touch
// task through every press (main.cpp); the network task holds it from each TLS handshake (or each request on a
// kept-open connection) to the end of the poll, and through every join and scan (live.cpp), so a handshake takes as
// long as before.
//
// Switching between 80, 160, and 240 MHz on the ESP32-S3 only changes the CPU's divider of the PLL, which stays on:
// the APB bus (and so SPI, the panel's QSPI) stays at 80 MHz, the FreeRTOS tick and esp_timer (millis, micros) run
// from the system timer on the crystal, I2C from the crystal, and USB serial from the PLL.
#include <stdint.h>

namespace cpuclk {

constexpr uint32_t kFastMhz = 240, kSlowMhz = 80;

void begin();     // before any task calls hold(); the clock stays as it is until the first hold or release
void hold();      // full speed from now until the matching release()
void release();

// Holds full speed for its scope.
class Fast {
public:
    Fast() { hold(); }
    ~Fast() { release(); }
    Fast(const Fast&) = delete;
    Fast& operator=(const Fast&) = delete;
};

// Holds full speed from its first on() to the end of its scope: for a request, whose DNS lookup and TCP connection
// only wait on the network, while the TLS handshake and the parsing after it are CPU work.
class Lazy {
public:
    Lazy() = default;
    ~Lazy() { if (held_) release(); }
    void on() {
        if (held_) return;
        held_ = true;
        hold();
    }
    Lazy(const Lazy&) = delete;
    Lazy& operator=(const Lazy&) = delete;

private:
    bool held_ = false;
};

#ifdef POWER_LOG
// For the power log, since the last take(): the ms spent at 80, 160, and 240 MHz, and the busy core-ms at each (both
// cores together, so up to twice the time). The clock can change several times a second, which a once-a-second
// snapshot of it would misreport.
struct Stats {
    uint32_t ms[3];
    uint32_t busyMs[3];
};
void take(Stats& out);
#endif

}  // namespace cpuclk
