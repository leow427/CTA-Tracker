#pragma once
// Stand-in for Arduino_GFX, for tools/panel_bus_test.cpp: just the SPI host its QSPI bus uses (ESP32QSPI_SPI_HOST).
#include <driver/spi_master.h>
#define ESP32QSPI_SPI_HOST SPI2_HOST
