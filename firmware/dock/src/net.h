#pragma once
// Wi-Fi join with backoff, and Chicago time over NTP (spec 6.6).
#include <stdint.h>

namespace net {
void begin(const char* ssid, const char* pass);   // ssid "" = not set up yet
void setCredentials(const char* ssid, const char* pass);
void poll();                                       // call often from the network task
bool up();
bool timeValid();                                  // NTP has set the clock
uint32_t now();                                    // Unix time (0 until timeValid)
int minuteOfDay();                                 // Chicago local time
}  // namespace net
