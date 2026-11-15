#ifdef HANDHELD_LIVE   // the mock build has no radio
#include "ble_setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ble_ids.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

namespace ble_setup {

namespace {

bool on = false;
uint8_t ownAddr = 0;
char info[64] = "";   // what the `info` characteristic reads
ble_uuid128_t svcUuid, infoUuid;

// "d8d4388c-c8e3-..." into NimBLE's form: the 16 bytes, least significant first.
void parseUuid(const char* s, ble_uuid128_t& out) {
    out.u.type = BLE_UUID_TYPE_128;
    int i = 15;
    for (const char* p = s; *p && i >= 0; p += 2) {
        if (*p == '-') ++p;
        const char hex[3] = {p[0], p[1], 0};
        out.value[i--] = static_cast<uint8_t>(strtoul(hex, nullptr, 16));
    }
}

int readInfo(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, info, strlen(info)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

ble_gatt_chr_def chrs[2];
ble_gatt_svc_def svcs[2];

int onGap(ble_gap_event* e, void*);

void advertise() {
    ble_hs_adv_fields adv = {};   // flags and the service; the name goes in the scan response (31 bytes each)
    adv.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv.uuids128 = &svcUuid;
    adv.num_uuids128 = 1;
    adv.uuids128_is_complete = 1;
    ble_hs_adv_fields rsp = {};
    const char* name = ble_svc_gap_device_name();
    rsp.name = reinterpret_cast<const uint8_t*>(name);
    rsp.name_len = strlen(name);
    rsp.name_is_complete = 1;
    if (ble_gap_adv_set_fields(&adv) != 0 || ble_gap_adv_rsp_set_fields(&rsp) != 0) return;
    ble_gap_adv_params params = {};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(ownAddr, nullptr, BLE_HS_FOREVER, &params, onGap, nullptr);
}

int onGap(ble_gap_event* e, void*) {   // the host task
    switch (e->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (e->connect.status != 0) advertise();
            break;
        case BLE_GAP_EVENT_DISCONNECT:
        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();
            break;
        default:
            break;
    }
    return 0;
}

void onSync() {
    ble_hs_id_infer_auto(0, &ownAddr);
    advertise();
}

void hostTask(void*) {
    nimble_port_run();   // until stop()
    nimble_port_freertos_deinit();
}

}  // namespace

bool start(const char* name) {
    if (on) return true;
    parseUuid(cta::kServiceUuid, svcUuid);
    parseUuid(cta::kInfoUuid, infoUuid);
    snprintf(info, sizeof(info), "{\"type\":\"handheld\",\"name\":\"%s\"}", name);
    chrs[0] = {};
    chrs[0].uuid = &infoUuid.u;
    chrs[0].access_cb = readInfo;
    chrs[0].flags = BLE_GATT_CHR_F_READ;
    chrs[1] = {};
    svcs[0] = {};
    svcs[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    svcs[0].uuid = &svcUuid.u;
    svcs[0].characteristics = chrs;
    svcs[1] = {};
    if (nimble_port_init() != ESP_OK) return false;
    ble_hs_cfg.sync_cb = onSync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_gatts_count_cfg(svcs) != 0 || ble_gatts_add_svcs(svcs) != 0 || ble_svc_gap_device_name_set(name) != 0) {
        nimble_port_deinit();
        return false;
    }
    nimble_port_freertos_init(hostTask);
    on = true;
    return true;
}

void stop() {
    if (!on) return;
    // Ends the host task's loop (it deletes itself). Refused while the host is still starting: deinit would then free
    // the queue that task waits on, so leave it running and let a later stop() try again.
    if (nimble_port_stop() != 0) return;
    nimble_port_deinit();   // frees the host and the controller
    on = false;
}

bool running() { return on; }

}  // namespace ble_setup
#endif
