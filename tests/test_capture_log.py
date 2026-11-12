"""Focused checks for host capture rotation and append safety."""

from datetime import datetime
from pathlib import Path
import sys
import tempfile
import unittest
from zoneinfo import ZoneInfo

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from capture_log import CSV_HEADER, DailyCsvLog


ROW = "113280,1794331567132,24.550,20.603,844.77"
OTHER_ROW = "143280,-1,-0.001,20.600,844.70"


def instant(text):
    return datetime.fromisoformat(text)


class DailyCsvLogTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.zone = ZoneInfo("America/Denver")

    def test_midnight_rotates_without_data_but_noon_does_not(self):
        with DailyCsvLog(self.directory, self.zone) as capture:
            before = instant("2026-11-12T06:59:59+00:00")
            midnight = instant("2026-11-12T07:00:00+00:00")
            noon = instant("2026-11-12T19:00:00+00:00")
            self.assertTrue(capture.write_line(ROW, before))
            self.assertEqual(capture.path.name, "env-2026-11-11.csv")
            new_path = capture.rotate(midnight)
            self.assertEqual(new_path.name, "env-2026-11-12.csv")
            self.assertEqual(new_path.read_text(), CSV_HEADER + "\n")
            self.assertEqual(capture.rotate(noon), new_path)
            self.assertTrue(capture.write_line(OTHER_ROW, noon))

        self.assertEqual(
            (self.directory / "env-2026-11-11.csv").read_text(),
            CSV_HEADER + "\n" + ROW + "\n",
        )
        self.assertEqual(new_path.read_text(), CSV_HEADER + "\n" + OTHER_ROW + "\n")

    def test_dst_uses_local_midnight_on_short_and_long_days(self):
        cases = (
            ("2026-03-08T06:59:59+00:00", "2026-03-08T07:00:00+00:00", "2026-03-08"),
            ("2026-03-09T05:59:59+00:00", "2026-03-09T06:00:00+00:00", "2026-03-09"),
            ("2026-11-01T05:59:59+00:00", "2026-11-01T06:00:00+00:00", "2026-11-01"),
            ("2026-11-02T06:59:59+00:00", "2026-11-02T07:00:00+00:00", "2026-11-02"),
        )
        with DailyCsvLog(self.directory, self.zone) as capture:
            for before, midnight, date in cases:
                with self.subTest(date=date):
                    old_path = capture.rotate(instant(before))
                    new_path = capture.rotate(instant(midnight))
                    self.assertNotEqual(old_path, new_path)
                    self.assertEqual(new_path.name, f"env-{date}.csv")

    def test_restart_appends_without_repeating_header(self):
        now = instant("2026-11-12T12:00:00-07:00")
        with DailyCsvLog(self.directory, self.zone) as capture:
            self.assertTrue(capture.write_line(ROW, now))
        with DailyCsvLog(self.directory, self.zone) as capture:
            self.assertFalse(capture.write_line(CSV_HEADER, now))
            self.assertTrue(capture.write_line(OTHER_ROW, now))
        self.assertEqual(
            (self.directory / "env-2026-11-12.csv").read_text(),
            CSV_HEADER + "\n" + ROW + "\n" + OTHER_ROW + "\n",
        )

    def test_only_complete_sensor_csv_rows_are_saved(self):
        now = instant("2026-11-12T12:00:00-07:00")
        invalid = (
            CSV_HEADER, "Queued", "Done; state=logging+streaming", "",
            "logger> " + ROW, "[00:00:01] <err> sensors: failed",
            "113280,1794331567132,24550,20603,84477",
            ROW + "\n", ROW + ",extra", ROW + "\x00", " " + ROW,
            "113280,-2,24.550,20.603,844.77",
            "113280,-1,nan,20.603,844.77", "9" * 112,
        )
        with DailyCsvLog(self.directory, self.zone) as capture:
            for line in invalid:
                with self.subTest(line=line):
                    self.assertFalse(capture.write_line(line, now))
            self.assertTrue(capture.write_line(OTHER_ROW, now))
        self.assertEqual(
            (self.directory / "env-2026-11-12.csv").read_text(),
            CSV_HEADER + "\n" + OTHER_ROW + "\n",
        )

    def test_incompatible_or_partial_existing_file_is_preserved(self):
        now = instant("2026-11-12T12:00:00-07:00")
        path = self.directory / "env-2026-11-12.csv"
        for content in ("wrong,header\n", CSV_HEADER + "\n" + ROW):
            with self.subTest(content=content):
                path.write_text(content)
                with DailyCsvLog(self.directory, self.zone) as capture:
                    with self.assertRaises(ValueError):
                        capture.write_line(OTHER_ROW, now)
                    self.assertIsNone(capture.path)
                self.assertEqual(path.read_text(), content)

    def test_second_writer_fails_until_first_writer_closes(self):
        now = instant("2026-11-12T12:00:00-07:00")
        with DailyCsvLog(self.directory, self.zone) as first:
            first.write_line(ROW, now)
            with DailyCsvLog(self.directory, self.zone) as second:
                with self.assertRaises(BlockingIOError):
                    second.write_line(OTHER_ROW, now)
                first.close()
                self.assertTrue(second.write_line(OTHER_ROW, now))
        self.assertEqual(
            (self.directory / "env-2026-11-12.csv").read_text(),
            CSV_HEADER + "\n" + ROW + "\n" + OTHER_ROW + "\n",
        )

    def test_naive_time_is_rejected_without_creating_a_file(self):
        with DailyCsvLog(self.directory, self.zone) as capture:
            with self.assertRaises(ValueError):
                capture.rotate(datetime(2026, 11, 12))
        self.assertEqual(list(self.directory.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
