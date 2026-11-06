#include "ble_server.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <mbedtls/platform_util.h>

#include "ble_ids.h"
#include "cta_config.h"
#include "device_config.h"
#include "state.h"

namespace ble {

static const char* sFw = "";

// For bring-up (docs/notes/dock-firmware.md check 5): how close the writes come to NimBLE's stack limit.
static void logStack() {   // ESP-IDF's stack type is a byte, so this is already in bytes
    Serial.printf("ble: host stack free %u bytes\n", (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}
static char sBuf[cta::kBleMaxPayload + 1];

class ServerCallbacks : public NimBLEServerCallbacks {
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
        {
            Lock l;
            gState.passkey = 0;
        }
        NimBLEDevice::startAdvertising();
    }
    // NimBLE's display-passkey flow: the dock shows a fresh 6-digit code, the user types it on the iPhone.
    uint32_t onPassKeyDisplay() override {
        uint32_t key = 100000 + esp_random() % 900000;
        Lock l;
        gState.passkey = key;
        return key;
    }
    void onAuthenticationComplete(NimBLEConnInfo& info) override {
        Lock l;
        gState.passkey = 0;
        if (!info.isEncrypted()) Serial.println("ble: pairing failed");
    }
};

class InfoCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        size_t n;
        {
            Lock l;
            n = cta::encodeInfoJson("dock", sFw, -1, false, gState.wifiUp, gState.ssid, gState.lastUpdate, sBuf, sizeof sBuf);
        }
        c->setValue((const uint8_t*)sBuf, n);
    }
};

class ConfigCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        size_t n;
        {
            Lock l;
            n = cta::encodeConfigJson(gState.cfg, true, sBuf, sizeof sBuf);
        }
        c->setValue((const uint8_t*)sBuf, n);
    }
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        NimBLEAttValue v = c->getValue();
        bool ok;
        {
            Lock l;
            static cta::DeviceConfig cfg;   // off NimBLE's 5 KB stack; only touched under the lock
            cfg = gState.cfg;
            ok = cta::applyConfigJson((const char*)v.data(), v.size(), cfg);
            if (ok) {
                for (int i = 0; i < 3; i++)
                    if (memcmp(&cfg.stops[i], &gState.cfg.stops[i], sizeof cfg.stops[i]) != 0) pickChanged(i);
                gState.cfg = cfg;
                gState.cfgVersion++;   // the most recent pick wins, from the app or the dock (spec 7.2)
                gState.cfgDirty = true;   // saved by the loop task
            }
        }
        if (!ok) Serial.println("ble: config write rejected");
        logStack();
    }
};

class WifiCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        // The characteristic already requires an encrypted, authenticated link; check again before trusting it.
        if (!info.isEncrypted() || !info.isAuthenticated()) return;
        NimBLEAttValue v = c->getValue();
        uint8_t slot;
        char ssid[cta::kSsidMax + 1], pass[cta::kPassMax + 1];
        bool ok = cta::decodeWifiJson((const char*)v.data(), v.size(), slot, ssid, sizeof ssid, pass, sizeof pass);
        // Wipe the password from the attribute (setValue with length 0 alone keeps the old bytes) and the copy.
        static const uint8_t kZeros[cta::kBleMaxPayload] = {};
        c->setValue(kZeros, v.size() < sizeof kZeros ? v.size() : sizeof kZeros);
        c->setValue(kZeros, 0);
        mbedtls_platform_zeroize((void*)v.data(), v.size());
        if (!ok || slot != 0) {   // the dock has one network (spec 10)
            Serial.println("ble: wifi write rejected");
            mbedtls_platform_zeroize(pass, sizeof pass);
            return;
        }
        {
            Lock l;   // the network task saves them to flash and rejoins
            strlcpy(gState.ssid, ssid, sizeof gState.ssid);
            strlcpy(gState.pendingPass, pass, sizeof gState.pendingPass);
            gState.hasWifiConfig = true;
            gState.wifiChanged = true;
        }
        mbedtls_platform_zeroize(pass, sizeof pass);
        logStack();
    }
};

void begin(const char* fw) {
    sFw = fw;
    char name[16];
    uint64_t mac = ESP.getEfuseMac();
    snprintf(name, sizeof name, "CTA-Dock-%02X%02X", (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
    NimBLEDevice::init(name);
    NimBLEDevice::setMTU(cta::kBleMaxPayload + 5);
    NimBLEDevice::setSecurityAuth(true, true, true);   // bonded, MITM-protected, LE Secure Connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());
    NimBLEService* svc = server->createService(cta::kServiceUuid);
    svc->createCharacteristic(cta::kInfoUuid, NIMBLE_PROPERTY::READ)->setCallbacks(new InfoCallbacks());
    svc->createCharacteristic(cta::kWifiUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC |
                                                  NIMBLE_PROPERTY::WRITE_AUTHEN, cta::kBleMaxPayload)
        ->setCallbacks(new WifiCallbacks());
    svc->createCharacteristic(cta::kConfigUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE, cta::kBleMaxPayload)
        ->setCallbacks(new ConfigCallbacks());
    svc->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setName(name);
    adv->addServiceUUID(cta::kServiceUuid);
    adv->enableScanResponse(true);
    adv->start();
}

}  // namespace ble
