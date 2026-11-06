#pragma once
// The dock's GATT service (spec 10): info (read), wifi (encrypted, authenticated write; slot 0 only),
// config (read, write: stops, pet, settings). Advertises as CTA-Dock-xxxx.
namespace ble {
void begin(const char* fw);
}
