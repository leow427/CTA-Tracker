#pragma once
// What the Settings page asks of the device (ui.cpp, live_model.cpp). mock_data.cpp has weak defaults, so the UI
// builds and the laptop tools run alone; the Bluetooth link (Track W: ble_setup.*, link.*) supplies the real ones.

namespace device {

// Settings → Pair again, confirmed: wipe the bond and the token, pick a new BLE address, and enter pairing mode.
// Called on the loop's task. The weak default does nothing.
void pairAgain();
// A bonded phone is connected over Bluetooth. The weak default says no.
bool bleConnected();
// Paired (a bond or a token exists). The Pair screen shown after Pair again goes Home once this is true; an unpaired
// device stays there until it's paired. The weak default says yes, since its pairAgain() unpairs nothing.
bool paired();

}  // namespace device
