#ifdef HANDHELD_LIVE   // the mock build has no radio
#include "ble_setup.h"

#include <esp_heap_caps.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <nvs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include "ble_frame.h"
#include "ble_ids.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "link.h"
#include "link_msg.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

extern "C" void ble_store_config_init(void);   // NimBLE's NVS bond store; IDF ships no header for it

namespace ble_setup {

namespace {

// The `link` write's answers (the plan's envelope table); the stack itself refuses an unencrypted or unbonded write.
constexpr int kAttBadBody = 0x80, kAttTooLarge = 0x81, kAttNotFound = 0x82, kAttBadFrames = 0x83;

// In PSRAM, allocated on the first start and kept: the message being reassembled, the status JSON, one chunk.
struct Bufs {
    char msg[cta::kMaxBleMsg + 1];   // the reassembler adds a NUL
    char info[cta::kMaxInfo];
    uint8_t chunk[BLE_ATT_ATTR_MAX_LEN];
};
Bufs* bufs;
cta::Reassembler* reasm;
SemaphoreHandle_t infoMu;   // bufs->info and infoLen: the loop writes, the host task reads
size_t infoLen = 0;

volatile bool on = false;
volatile uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
volatile bool subscribed = false, subNew = false, hasBond = false;
volatile uint32_t shownKey = 0;
volatile uint32_t minStack = 0;
TaskHandle_t hostHandle = nullptr;   // onGap also runs on the loop (a notify's NOTIFY_TX): count the host task only
uint8_t addr[6];   // our static random address (NVS "ble_addr"), least significant byte first
uint16_t infoHandle = 0;
ble_uuid128_t svcUuid, infoUuid, linkUuid;

void noteStack() {
    if (xTaskGetCurrentTaskHandle() != hostHandle) return;
    const uint32_t left = uxTaskGetStackHighWaterMark(nullptr);
    if (!minStack || left < minStack) minStack = left;
}

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

bool secure(uint16_t c) {
    ble_gap_conn_desc d;
    return c != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_find(c, &d) == 0 && d.sec_state.encrypted &&
           d.sec_state.authenticated && d.sec_state.bonded;
}

int accessInfo(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*) {   // the host task
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    xSemaphoreTake(infoMu, portMAX_DELAY);
    const int rc = os_mbuf_append(ctxt->om, bufs->info, infoLen);
    xSemaphoreGive(infoMu);
    noteStack();
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// One chunk of a framed message; the whole message goes to the dispatcher, whose status is the write's answer.
int accessLink(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*) {   // the host task
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint16_t n = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, bufs->chunk, sizeof(bufs->chunk), &n) != 0) return kAttBadFrames;
    const cta::FrameResult fr = reasm->add(bufs->chunk, n);
    if (fr == cta::FrameResult::More) return 0;
    if (fr == cta::FrameResult::Error) {
        printf("ble: link chunk out of sequence (%u bytes, header 0x%02x)\n", n, n ? bufs->chunk[0] : 0);
        return kAttBadFrames;
    }
    cta::Method m;
    char path[cta::kPathLen + 1];
    const char* body = nullptr;
    size_t len = 0;
    int rc = kAttBadBody;
    if (cta::parseEnvelope(reasm->message(), reasm->length(), m, path, body, len)) {
        const int st = lan::apply(m == cta::Method::Delete, path, body, len, lan::Via::Ble);
        rc = st == 200 ? 0 : st == 413 ? kAttTooLarge : st == 404 ? kAttNotFound : kAttBadBody;
        // Path and status only: a body may hold a password or the token.
        printf("ble: %s %s: %d\n", m == cta::Method::Delete ? "DELETE" : "POST", path, st);
    } else {
        printf("ble: link message without an envelope (%u bytes)\n", static_cast<unsigned>(reasm->length()));
    }
    noteStack();
    return rc;
}

ble_gatt_chr_def chrs[3];
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
    const int rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, nullptr, BLE_HS_FOREVER, &params, onGap, nullptr);
    if (rc != 0 && rc != BLE_HS_EALREADY) printf("ble: advertising didn't start (%d)\n", rc);
}

void countBonds() {
    int n = 0;
    hasBond = ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &n) == 0 && n > 0;
}

int onGap(ble_gap_event* e, void*) {   // the host task
    switch (e->type) {
        case BLE_GAP_EVENT_CONNECT:
            printf("ble: connect, status %d\n", e->connect.status);
            if (e->connect.status == 0) conn = e->connect.conn_handle;
            else advertise();
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            printf("ble: disconnect, reason 0x%03x\n", e->disconnect.reason);
            conn = BLE_HS_CONN_HANDLE_NONE;
            subscribed = false;
            shownKey = 0;
            advertise();
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise();
            break;
        case BLE_GAP_EVENT_PASSKEY_ACTION:
            printf("ble: passkey action %d (%s)\n", e->passkey.params.action,
                   e->passkey.params.action == BLE_SM_IOACT_DISP ? "shown on screen" : "not handled");
            if (e->passkey.params.action == BLE_SM_IOACT_DISP) {   // the code goes on the screen only, never a log
                ble_sm_io io = {};
                io.action = BLE_SM_IOACT_DISP;
                io.passkey = esp_random() % 999999 + 1;
                shownKey = io.passkey;
                ble_sm_inject_io(e->passkey.conn_handle, &io);
            }
            break;
        case BLE_GAP_EVENT_ENC_CHANGE: {
            ble_gap_conn_desc d = {};
            ble_gap_conn_find(e->enc_change.conn_handle, &d);
            printf("ble: encryption, status %d: encrypted %d, authenticated %d, bonded %d, key size %d\n",
                   e->enc_change.status, d.sec_state.encrypted, d.sec_state.authenticated, d.sec_state.bonded,
                   d.sec_state.key_size);
            shownKey = 0;
            countBonds();
            // A subscribe can come before encryption (its CCCD isn't protected): notify on the loop's next setInfo,
            // not at the 30 s heartbeat, which is the app's pairing timeout.
            if (e->enc_change.status == 0) subNew = true;
            break;
        }
        case BLE_GAP_EVENT_MTU:
            printf("ble: MTU %u\n", e->mtu.value);
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            printf("ble: subscribe, handle %u (%s), notify %d\n", e->subscribe.attr_handle,
                   e->subscribe.attr_handle == infoHandle ? "info" : "other", e->subscribe.cur_notify);
            if (e->subscribe.attr_handle == infoHandle) {
                subscribed = e->subscribe.cur_notify;
                subNew = subscribed;
            }
            break;
        case BLE_GAP_EVENT_REPEAT_PAIRING: {   // the phone lost its bond and we kept ours: drop ours and pair again
            puts("ble: repeat pairing: dropping our old bond for this phone");
            ble_gap_conn_desc d;
            if (ble_gap_conn_find(e->repeat_pairing.conn_handle, &d) == 0) ble_store_util_delete_peer(&d.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        default:
            break;
    }
    noteStack();
    return 0;
}

void onSync() {
    ble_hs_id_set_rnd(addr);
    countBonds();
    printf("ble: up as %s, %s\n", ble_svc_gap_device_name(), hasBond ? "bonded" : "no bond");
    advertise();
}

void hostTask(void*) {
    hostHandle = xTaskGetCurrentTaskHandle();
    nimble_port_run();   // until stop()
    nimble_port_freertos_deinit();
}

// The static random address from NVS, or a new one saved there (the top two bits set, as a static address needs).
void loadAddr() {
    nvs_handle_t h;
    size_t n = sizeof(addr);
    if (nvs_open("trackie", NVS_READWRITE, &h) != ESP_OK) {
        esp_fill_random(addr, sizeof(addr));
        addr[5] |= 0xC0;
        return;
    }
    if (nvs_get_blob(h, "ble_addr", addr, &n) != ESP_OK || n != sizeof(addr)) {
        esp_fill_random(addr, sizeof(addr));
        addr[5] |= 0xC0;
        if (nvs_set_blob(h, "ble_addr", addr, sizeof(addr)) != ESP_OK || nvs_commit(h) != ESP_OK)
            puts("ble: saving the address failed");
    }
    nvs_close(h);
}

}  // namespace

bool start(const char* name) {
    if (on) return true;
    if (!bufs) {
        void* p = heap_caps_malloc(sizeof(Bufs), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) return false;
        bufs = new (p) Bufs();
        reasm = new cta::Reassembler(bufs->msg, sizeof(bufs->msg));
        infoMu = xSemaphoreCreateMutex();
    }
    loadAddr();
    parseUuid(cta::kServiceUuid, svcUuid);
    parseUuid(cta::kInfoUuid, infoUuid);
    parseUuid(cta::kLinkUuid, linkUuid);
    chrs[0] = {};
    chrs[0].uuid = &infoUuid.u;
    chrs[0].access_cb = accessInfo;
    chrs[0].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN | BLE_GATT_CHR_F_NOTIFY;
    chrs[0].val_handle = &infoHandle;
    chrs[1] = {};
    chrs[1].uuid = &linkUuid.u;
    chrs[1].access_cb = accessLink;
    chrs[1].flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN;
    chrs[2] = {};
    svcs[0] = {};
    svcs[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    svcs[0].uuid = &svcUuid.u;
    svcs[0].characteristics = chrs;
    svcs[1] = {};
    if (nimble_port_init() != ESP_OK) return false;
    ble_hs_cfg.sync_cb = onSync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_sc_only = 1;   // no legacy pairing: its 6-digit key falls to a sniffed exchange (spec 4.3: SC)
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_gatts_count_cfg(svcs) != 0 || ble_gatts_add_svcs(svcs) != 0 || ble_svc_gap_device_name_set(name) != 0) {
        nimble_port_deinit();
        return false;
    }
    ble_store_config_init();
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
    conn = BLE_HS_CONN_HANDLE_NONE;
    subscribed = false;
    shownKey = 0;
    on = false;
}

bool running() { return on; }

bool connected() { return on && secure(conn); }

bool bonded() { return hasBond; }

void setInfo(const char* json, size_t len, bool notify) {
    if (!bufs || len >= sizeof(bufs->info)) return;
    xSemaphoreTake(infoMu, portMAX_DELAY);
    memcpy(bufs->info, json, len);
    infoLen = len;
    xSemaphoreGive(infoMu);
    const uint16_t c = conn;
    if (!(notify || subNew) || !subscribed || !connected()) return;
    subNew = false;
    // The CCCD's own permission isn't relied on: the link was checked encrypted and bonded just above.
    struct Ctx { uint16_t conn; };
    Ctx ctx{c};
    const size_t chunkMax = ble_att_mtu(c) - 3;
    cta::splitFrames(json, len, chunkMax, [](const uint8_t* chunk, size_t n, void* p) {
        os_mbuf* om = ble_hs_mbuf_from_flat(chunk, n);
        return om && ble_gatts_notify_custom(static_cast<Ctx*>(p)->conn, infoHandle, om) == 0;
    }, &ctx);
}

uint32_t passkey() { return shownKey; }

void forgetBonds() {
    hasBond = false;
    nvs_handle_t h;
    if (nvs_open("nimble_bond", NVS_READWRITE, &h) == ESP_OK) {   // NimBLE's store (ble_store_nvs.c)
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    if (nvs_open("trackie", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "ble_addr");
        nvs_commit(h);
        nvs_close(h);
    }
}

uint32_t stackLeft() { return minStack; }

}  // namespace ble_setup
#endif
