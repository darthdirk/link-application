"""Append sensor CSV records to files named for the host's local calendar date."""

from datetime import datetime
import errno
import fcntl
import os
from pathlib import Path
import re
from typing import BinaryIO
from zoneinfo import ZoneInfo


CSV_HEADER = "uptime_ms,epoch_ms,temperature_C,humidity_pct,pressure_hPa"
_HEADER_BYTES = (CSV_HEADER + "\n").encode("ascii")
_RECORD = re.compile(
    r"[0-9]+,(?:-1|[0-9]+),-?[0-9]+\.[0-9]{3},"
    r"-?[0-9]+\.[0-9]{3},-?[0-9]+\.[0-9]{2}",
    re.ASCII,
)


class DailyCsvLog:
    """Own one append-only capture file, rotating at midnight in the given zone.

    Call rotate periodically even without incoming readings. File selection uses
    the PC's receipt time; the device's epoch_ms remains unchanged in each row.
    Every accepted row is flushed and synced before write_line returns.
    """

    def __init__(self, directory: Path, timezone: ZoneInfo):
        self.directory = Path(directory)
        self.timezone = timezone
        self._file: BinaryIO | None = None
        self._path: Path | None = None

    @property
    def path(self) -> Path | None:
        return self._path

    @staticmethod
    def _sync(file: BinaryIO) -> None:
        file.flush()
        os.fsync(file.fileno())

    @staticmethod
    def _open(path: Path) -> BinaryIO:
        file = path.open("a+b")
        try:
            try:
                fcntl.flock(file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise BlockingIOError(
                    errno.EWOULDBLOCK, "Capture file is already in use", str(path)
                ) from error

            file.seek(0, os.SEEK_END)
            if file.tell() == 0:
                file.write(_HEADER_BYTES)
                DailyCsvLog._sync(file)
            else:
                file.seek(0)
                if file.readline(len(_HEADER_BYTES) + 1) != _HEADER_BYTES:
                    raise ValueError(f"Capture file has an incompatible header: {path}")
                file.seek(-1, os.SEEK_END)
                if file.read(1) != b"\n":
                    raise ValueError(f"Capture file has an incomplete final row: {path}")
            file.seek(0, os.SEEK_END)
            return file
        except BaseException:
            file.close()
            raise

    def rotate(self, now: datetime) -> Path:
        if now.tzinfo is None or now.utcoffset() is None:
            raise ValueError("Capture time must include a timezone")
        date = now.astimezone(self.timezone).date()
        path = self.directory / f"env-{date.isoformat()}.csv"
        if self._file is not None and self._path == path:
            return path

        self.directory.mkdir(parents=True, exist_ok=True)
        # Keep the previous handle until the new file is open and validated.
        new_file = self._open(path)
        previous = self._file
        self._file = new_file
        self._path = path
        if previous is not None:
            previous.close()
        return path

    def write_line(self, line: str, now: datetime) -> bool:
        self.rotate(now)
        # The firmware's bounded CSV records are shorter than 112 characters.
        # Reject headers, command echoes, acknowledgements and diagnostic text.
        if len(line) >= 112 or _RECORD.fullmatch(line) is None:
            return False
        assert self._file is not None
        data = (line + "\n").encode("ascii")
        if self._file.write(data) != len(data):
            raise OSError("Short write to capture file")
        self._sync(self._file)
        return True

    def close(self) -> None:
        if self._file is not None:
            file = self._file
            self._file = None
            self._path = None
            file.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()
