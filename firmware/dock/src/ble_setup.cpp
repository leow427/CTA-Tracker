#include "ble_setup.h"
#include "serial_out.h"

#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <stdio.h>
#include <string.h>

#include "ble_ids.h"
#include "link.h"
#include "serial_out.h"

namespace ble_setup {

namespace {

ble_uuid_any_t svcUuid, infoUuid;
volatile bool on = false;
uint8_t ownAddrType = 0;

int infoAccess(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    char buf[384];
    const size_t n = lan::infoJson(buf, sizeof(buf));
    if (!n) return BLE_ATT_ERR_INSUFFICIENT_RES;
    return os_mbuf_append(ctxt->om, buf, n) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

ble_gatt_chr_def chrs[2];
ble_gatt_svc_def svcs[2];

void advertise();

int onGap(ble_gap_event* e, void*) {
    if (e->type == BLE_GAP_EVENT_DISCONNECT || e->type == BLE_GAP_EVENT_ADV_COMPLETE) advertise();
    else if (e->type == BLE_GAP_EVENT_CONNECT && e->connect.status != 0) advertise();
    return 0;
}

// Flags and the service in the advertisement; the name (too long to fit beside a 128-bit UUID) in the scan response.
void advertise() {
    ble_hs_adv_fields f = {};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128 = &svcUuid.u128;
    f.num_uuids128 = 1;
    f.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&f) != 0) return;
    ble_hs_adv_fields r = {};
    const char* name = ble_svc_gap_device_name();
    r.name = reinterpret_cast<const uint8_t*>(name);
    r.name_len = strlen(name);
    r.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&r);
    ble_gap_adv_params p = {};
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(ownAddrType, nullptr, BLE_HS_FOREVER, &p, onGap, nullptr);
}

void onSync() {
    ble_hs_id_infer_auto(0, &ownAddrType);
    advertise();
}

void hostTask(void*) {
    nimble_port_run();   // returns after nimble_port_stop()
    nimble_port_freertos_deinit();
}

}  // namespace

bool start(const char* name) {
    if (on) return true;
    ble_uuid_from_str(&svcUuid, cta::kServiceUuid);
    ble_uuid_from_str(&infoUuid, cta::kInfoUuid);
    chrs[0] = {};
    chrs[0].uuid = &infoUuid.u;
    chrs[0].access_cb = infoAccess;
    chrs[0].flags = BLE_GATT_CHR_F_READ;
    chrs[1] = {};
    svcs[0] = {};
    svcs[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    svcs[0].uuid = &svcUuid.u;
    svcs[0].characteristics = chrs;
    svcs[1] = {};
    if (nimble_port_init() != ESP_OK) {
        uprintf("ble: didn't start\n");
        return false;
    }
    ble_hs_cfg.sync_cb = onSync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_gatts_count_cfg(svcs) != 0 || ble_gatts_add_svcs(svcs) != 0 || ble_svc_gap_device_name_set(name) != 0) {
        uprintf("ble: service setup failed\n");
        nimble_port_deinit();
        return false;
    }
    nimble_port_freertos_init(hostTask);
    on = true;
    return true;
}

void stop() {
    if (!on) return;
    nimble_port_stop();     // the host task leaves nimble_port_run and ends
    nimble_port_deinit();   // the host and the controller, their memory with them
    on = false;
}

bool running() { return on; }

}  // namespace ble_setup
