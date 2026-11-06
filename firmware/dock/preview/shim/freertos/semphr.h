#pragma once
typedef void* SemaphoreHandle_t;
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return (void*)1; }
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t, unsigned) { return 1; }
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t) { return 1; }
