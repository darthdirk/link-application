"""Check bounded capture parsing and startup failure handling."""

from datetime import datetime
from pathlib import Path
import sys
import tempfile
import unittest
from zoneinfo import ZoneInfo

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from capture import CaptureLines, MAX_LINE_BYTES
from capture_log import CSV_HEADER, DailyCsvLog


ROW = "113280,1794331567132,24.550,20.603,844.77"


class CaptureSessionTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.writer = DailyCsvLog(Path(temporary.name), ZoneInfo("America/Denver"))
        self.addCleanup(self.writer.close)
        self.now = datetime.fromisoformat("2026-11-17T12:00:00-07:00")
        self.path = self.writer.rotate(self.now)
        self.lines = CaptureLines(self.writer, "CAPTURE_READY", "CAPTURE_FAILED")

    def test_chunked_startup_discards_old_rows_and_terminal_messages(self):
        self.lines.feed((ROW + "\nCAPTURE_RE").encode(), self.now)
        self.assertFalse(self.lines.ready)
        self.assertEqual(self.path.read_text(), CSV_HEADER + "\n")
        self.lines.feed(b"ADY\r\nQueued\r\n", self.now)
        self.lines.feed((ROW[:12]).encode(), self.now)
        self.lines.feed((ROW[12:] + "\r\nDone; state=logging+streaming\r\n").encode(), self.now)
        self.assertTrue(self.lines.ready)
        self.assertEqual(self.path.read_text(), CSV_HEADER + "\n" + ROW + "\n")

    def test_overlong_line_is_discarded_and_next_record_recovers(self):
        self.lines.feed(b"CAPTURE_READY\n", self.now)
        self.lines.feed(b"x" * (MAX_LINE_BYTES * 4), self.now)
        self.assertLessEqual(len(self.lines.pending), MAX_LINE_BYTES)
        self.lines.feed(("\n" + ROW + "\n").encode(), self.now)
        self.assertEqual(self.path.read_text(), CSV_HEADER + "\n" + ROW + "\n")

    def test_setup_failure_does_not_accept_a_record(self):
        with self.assertRaisesRegex(RuntimeError, "Device setup failed"):
            self.lines.feed(("response CAPTURE_FAILED\n" + ROW + "\n").encode(), self.now)
        self.assertFalse(self.lines.ready)
        self.assertEqual(self.path.read_text(), CSV_HEADER + "\n")


if __name__ == "__main__":
    unittest.main()
