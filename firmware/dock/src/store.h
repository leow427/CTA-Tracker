#pragma once
// The dock's flash (NVS): its picked stops (spec 7.2), its cached weather location (spec 7.5), and the link's
// settings (ecosystem spec 4.3, 5.2): its one Wi-Fi network, the household token, Trackie's colors, device settings.
// Saves skip the write when nothing changed: a flash write upsets the panel (display.h).
#include <stdint.h>

#include "link_msg.h"
#include "picks.h"
#include "wifi_list.h"

namespace store {

// The saved picks, or the defaults (cta::defaultPicks) when none are saved or they don't parse.
void loadPicks(cta::PickedStop out[cta::kDockStops]);
// Writes the picks unless the saved ones are the same (wrote says whether it wrote flash).
bool savePicks(const cta::PickedStop picks[cta::kDockStops], bool* wrote = nullptr);
// The last ipapi.co location and when it was looked up (Unix time); false if none.
bool loadGeo(float& lat, float& lon, uint32_t& when);
bool saveGeo(float lat, float lon, uint32_t when);
// Each load is false when nothing is saved.
bool loadWifi(cta::SavedNet& out);
bool saveWifi(const cta::SavedNet& n);
bool loadToken(char out[cta::kTokenLen + 1]);
bool saveToken(const char* t);
bool loadPet(cta::PetConfig& out);
bool savePet(const cta::PetConfig& p);
bool loadSettings(cta::DeviceSettings& out);
bool saveSettings(const cta::DeviceSettings& s);
// Forgets the Wi-Fi network and the token (and, once BLE setup exists, its bonds); picks, pet and settings stay.
void wipeLink();

}  // namespace store
