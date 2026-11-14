"""Tests for dock_perf_report.py on small made-up runs."""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dock_perf_report as rep  # noqa: E402

COLS = ("ms,phase,screen,cpu_c,busy0,busy1,frames,bands,draw_ms,copy_ms,wait_ms,move_fps,slow_frame_ms,tap_ms,"
        "tap_max_ms,drag_ms,drag_max_ms,touches,list_ms,req_started,req_done,req_ms,req_bytes,wifi_status,rssi,"
        "scan_late,recoveries,switch_timeouts,int_free,int_min,psram_free,sections,picked,data_age,log_us")


def make_run(change, draw_per_frame, temp, tap_ms):
    """Phase 1 only, 30 s: two frames a second at draw_per_frame ms each, one tap at 20 s."""
    lines = [f"# change: {change}", "H," + COLS, "E,0,start,dock-stress-v1", "E,1000,phase,1",
             "E,31000,phase,2", "E,1000,boot,wifi=2000,clock=4000,loaded=9000"]
    for s in range(1, 31):
        ms = 1000 + s * 1000
        row = [ms, 1, 0, temp, 5, 20, 2, 1, 2 * draw_per_frame, 2, 10, "", "", "", "", "", "", 0, "", 0, 0, 0, 0, 3,
               -50, 0, 0, 0, 100000, 90000, 4000000, 3, 3, 12, 500]
        lines.append("P," + ",".join(str(x) for x in row))
    lines.append(f"E,21000,tap,{tap_ms}")
    lines.append("E,5000,tap,999")   # in the dropped first 10 s: not scored
    lines.append("E,22000,req_end,t,200,5000,700")
    lines.append("E,23000,req_end,b,-1,0,12000")
    f = tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False)
    f.write("\n".join(lines) + "\n")
    f.close()
    return f.name


class ReportTest(unittest.TestCase):
    def test_garbled_lines_are_skipped(self):
        # Another task's serial print can land inside a perf line (seen 2026-11-13: "E,5[23956,req_5start,w"): such a
        # line is skipped, the rest of the run read as usual.
        path = make_run("a", 50, 40.0, 60)
        with open(path, "a") as f:
            f.write("E,5[23956,req_5start,w\nP,4[1000,1,0\n")
        run = rep.read_run(path)
        self.assertEqual(len(run["rows"]), 30)
        self.assertEqual(rep.phase_metrics(run, 1)["tap"], 60)

    def test_phase_metrics(self):
        run = rep.read_run(make_run("a", 50, 40.0, 60))
        m = rep.phase_metrics(run, 1)
        self.assertAlmostEqual(m["draw"], 50)
        self.assertAlmostEqual(m["frame"], 50 + 1 + 5)
        self.assertEqual(m["tap"], 60)          # the tap in the dropped 10 s is left out
        self.assertEqual(m["req"], 700)         # only successful requests are timed
        self.assertEqual(m["req_fail"], 1)
        self.assertAlmostEqual(m["int_min"], 90000 / 1024)
        self.assertEqual(run["boot"]["loaded"], 9.0)

    def test_verdicts(self):
        paths = [make_run("base", 50, 40.0, 60), make_run("base", 52, 40.0, 62), make_run("r1", 20, 40.0, 30),
                 make_run("bad", 50, 45.0, 60)]
        groups = rep.group_runs(paths)
        base, r1, bad = groups
        self.assertEqual(base["noise"][1]["draw"], 2)
        self.assertEqual(rep.verdict(base, r1, 1, "draw")[0], "better")
        self.assertEqual(rep.verdict(base, r1, 1, "tap")[0], "better")
        self.assertEqual(rep.verdict(base, bad, 1, "temp")[0], "worse")
        self.assertEqual(rep.verdict(base, bad, 1, "draw")[0], "")
        self.assertIn("better", rep.compare(base, r1))


if __name__ == "__main__":
    unittest.main()
