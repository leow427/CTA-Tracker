// LVGL's heap in PSRAM (lv_conf.h: LV_STDLIB_CUSTOM), so long picker lists never use internal RAM,
// which Wi-Fi, TLS and BLE need.
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <string.h>

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
void* lv_malloc_core(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
void* lv_realloc_core(void* p, size_t size) { return heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM); }
void lv_free_core(void* p) { heap_caps_free(p); }
void lv_mem_monitor_core(lv_mem_monitor_t* m) { memset(m, 0, sizeof *m); }
lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }
