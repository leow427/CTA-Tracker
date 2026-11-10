"""Tests for power_model.py's CPU term: one clock all second (older runs), or time and busy split by clock."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import power_model  # noqa: E402

MODEL = power_model.load()
IDLE = MODEL["cpu"]["idle_ma"]
BUSY = MODEL["cpu"]["per_busy_core_ma"]


def cpu(row):
    return power_model.estimate(MODEL, row)["cpu"]


class CpuTermTests(unittest.TestCase):
    def test_one_clock_from_the_snapshot_without_clock_columns(self):
        row = {"cpu_mhz": "240", "busy0": "7", "busy1": "2"}
        self.assertEqual(cpu(row), IDLE["240"] + 0.09 * BUSY["240"])
        row = {"cpu_mhz": "80", "busy0": "30", "busy1": "10"}
        self.assertEqual(cpu(row), IDLE["80"] + 0.4 * BUSY["80"])

    def test_empty_clock_columns_fall_back_to_the_snapshot(self):
        row = {"cpu_mhz": "240", "busy0": "7", "busy1": "2", "clk_ms_80": "", "clk_ms_160": "", "clk_ms_240": "",
               "clk_busy_80": "", "clk_busy_160": "", "clk_busy_240": ""}
        self.assertEqual(cpu(row), IDLE["240"] + 0.09 * BUSY["240"])

    def test_all_second_at_one_clock_matches_the_snapshot_formula(self):
        row = {"cpu_mhz": "240", "busy0": "7", "busy1": "2", "clk_ms_80": "0", "clk_ms_160": "0", "clk_ms_240": "1000",
               "clk_busy_80": "0", "clk_busy_160": "0", "clk_busy_240": "90"}
        self.assertAlmostEqual(cpu(row), IDLE["240"] + 0.09 * BUSY["240"], places=9)

    def test_a_second_split_between_clocks_costs_each_part_at_its_own_clock(self):
        # 300 ms at 240 MHz with 400 busy core-ms (a request), 700 ms at 80 MHz with 100 busy core-ms. The snapshot
        # says 80 MHz, which alone would cost the whole second at 80.
        row = {"cpu_mhz": "80", "busy0": "40", "busy1": "10", "clk_ms_80": "700", "clk_ms_160": "0",
               "clk_ms_240": "300", "clk_busy_80": "100", "clk_busy_160": "0", "clk_busy_240": "400"}
        want = (700 * IDLE["80"] + 300 * IDLE["240"] + 100 * BUSY["80"] + 400 * BUSY["240"]) / 1000
        self.assertAlmostEqual(cpu(row), want, places=9)

    def test_the_split_is_over_the_logged_time_not_a_nominal_second(self):
        row = {"cpu_mhz": "80", "busy0": "0", "busy1": "0", "clk_ms_80": "1003", "clk_ms_160": "0",
               "clk_ms_240": "0", "clk_busy_80": "0", "clk_busy_160": "0", "clk_busy_240": "0"}
        self.assertAlmostEqual(cpu(row), IDLE["80"], places=9)

    def test_no_logged_time_falls_back_to_the_snapshot(self):
        row = {"cpu_mhz": "240", "busy0": "10", "busy1": "0", "clk_ms_80": "0", "clk_ms_160": "0", "clk_ms_240": "0",
               "clk_busy_80": "0", "clk_busy_160": "0", "clk_busy_240": "0"}
        self.assertEqual(cpu(row), IDLE["240"] + 0.1 * BUSY["240"])


if __name__ == "__main__":
    unittest.main()
