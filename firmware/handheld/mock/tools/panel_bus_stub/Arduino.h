#pragma once
// Stand-in for the Arduino core, for tools/panel_bus_test.cpp: register writes go to the test's model of the GPIOs.
#include <stddef.h>
#include <stdint.h>
void mock_reg_write(uint32_t reg, uint32_t val);
#define REG_WRITE(r, v) mock_reg_write((r), (v))
