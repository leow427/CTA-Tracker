#include "state.h"

DockState gState;
static SemaphoreHandle_t sMutex;

void stateBegin() { sMutex = xSemaphoreCreateRecursiveMutex(); }
Lock::Lock() { xSemaphoreTakeRecursive(sMutex, portMAX_DELAY); }
Lock::~Lock() { xSemaphoreGiveRecursive(sMutex); }

void pickChanged(int i) {
    gState.recs[i] = cta::StopRecord{};
    gState.masks[i] = 0;
    gState.maskTs[i] = 0;
    gState.stopsChanged |= 1 << i;
}
