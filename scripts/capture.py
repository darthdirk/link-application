#!/usr/bin/env python3
"""Launch Minicom and save its sensor CSV output in daily host files."""

import argparse
from contextlib import contextmanager
from datetime import datetime
import fcntl
import os
from pathlib import Path
import re
import selectors
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import termios
import time
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

from capture_log import DailyCsvLog


MAX_LINE_BYTES = 1024
STARTUP_TIMEOUT_SECONDS = 120


def device_log_name(value: str) -> str:
    if re.fullmatch(r"[A-Za-z0-9_-]{1,43}\.csv", value) is None:
        raise ValueError("Use 1-43 letters, digits, underscores or hyphens followed by .csv")
    return value


def wait_for_serial_port(port: str, timeout: float = 10) -> None:
    """Allow USB re-enumeration after flashing, without reconnecting an active capture."""
    deadline = time.monotonic() + timeout
    waiting = False
    while True:
        try:
            info = os.stat(port)
        except FileNotFoundError:
            if time.monotonic() >= deadline:
                raise RuntimeError(
                    f"Serial device did not appear: {port}; check SERIAL_PORT"
                ) from None
            if not waiting:
                print(f"Waiting for USB serial device {port}...", flush=True)
                waiting = True
            time.sleep(0.1)
            continue
        if not stat.S_ISCHR(info.st_mode):
            raise RuntimeError(f"Serial port is not a character device: {port}")
        return


def startup_script(
    interval_ms: int, ready: str, failed: str, device_log: str = "readings.csv"
) -> str:
    """Use Minicom's serial owner to configure one acknowledged command at a time."""
    device_log = device_log_name(device_log)
    expect = """expect {
  \"Command \" goto failed
  \"command not found\" goto failed
  \"wrong parameter count\" goto failed
  \"Done; state=\" break
  timeout 10 goto failed
}
"""
    lines = ["timeout 110\nverbose on\n"]
    for command in ("stream stop", "log stop"):
        lines.extend((f'send "{command}"\n', expect))
    # !< executes locally and sends stdout to the device. Clear runscript's
    # previous expect buffer first; unlike send, !< does not clear it itself.
    lines.extend(
        (
            'send ""\n',
            "!< date -u '+time set %Y-%m-%dT%H:%M:%SZ'\n",
            "if $? != 0 goto failed\n",
            expect,
        )
    )
    for command in (
        f"config set log_rate_ms {interval_ms}",
        f"config set stream_rate_ms {interval_ms}",
        "config save",
        f"log start {device_log}",
        "stream start",
    ):
        lines.extend((f'send "{command}"\n', expect))
    lines.extend(
        (
            f'print "\\n{ready}"\nexit 0\n',
            f'failed:\nprint "\\n{failed}"\nexit 1\n',
        )
    )
    return "".join(lines)


def create_minicom_profile() -> tuple[str, Path]:
    """Create an isolated personal profile without changing an existing profile."""
    descriptor, name = tempfile.mkstemp(prefix=".minirc.logger-", dir=Path.home())
    path = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="ascii") as profile:
            profile.write(
                "# Temporary profile owned by the logger capture process.\n"
                "pu rtscts No\n"
                "pu xonxoff No\n"
                "pu hasdcd No\n"
                "pu localecho No\n"
                "pu addlinefeed No\n"
                "pu addcarreturn No\n"
                "pu linewrap No\n"
                "pu bits 8\n"
                "pu parity N\n"
                "pu stopbits 1\n"
                "pu minit\n"
                "pu mreset\n"
                "pu mhangup\n"
                "pu mdialcan\n"
                "pu macros\n"
                "pu macenab No\n"
                "pu scriptdir\n"
                "pu scriptprog runscript\n"
                "pu logfname\n"
                "pu escape-key ^A\n"
            )
    except BaseException:
        path.unlink(missing_ok=True)
        raise
    return path.name.removeprefix(".minirc."), path


@contextmanager
def capture_directory_lock(directory: Path):
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / ".capture.lock").open("a+", encoding="ascii") as lock:
        try:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError(f"Another capture is using {directory}") from error
        lock.seek(0)
        lock.truncate()
        lock.write(f"{os.getpid()}\n")
        lock.flush()
        yield


class CaptureLines:
    """Bound input memory and keep shell/status text out of saved sensor CSVs."""

    def __init__(self, writer: DailyCsvLog, ready: str, failed: str):
        self.writer = writer
        self.ready_marker = ready
        self.failed_marker = failed
        self.ready = False
        self.pending = bytearray()
        self.discarding = False

    def feed(self, data: bytes, now: datetime) -> None:
        for byte in data:
            if byte == 10:
                if not self.discarding:
                    self._line(self.pending.decode("ascii", errors="replace").rstrip("\r"), now)
                self.pending.clear()
                self.discarding = False
            elif not self.discarding:
                if len(self.pending) == MAX_LINE_BYTES:
                    self.pending.clear()
                    self.discarding = True
                else:
                    self.pending.append(byte)

    def _line(self, line: str, now: datetime) -> None:
        # Runscript can print its marker before an incomplete response line ends.
        if self.failed_marker in line:
            raise RuntimeError("Device setup failed; check the Minicom error above")
        if self.ready_marker in line:
            self.ready = True
            return
        if self.ready:
            self.writer.write_line(line, now)


def stop_minicom(child: subprocess.Popen) -> None:
    # Minicom's SIGTERM handler may wait in a terminal dialog. Bound shutdown,
    # and include runscript even if Minicom itself has already exited.
    try:
        os.killpg(child.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        child.wait(timeout=2)
    except subprocess.TimeoutExpired:
        pass
    # A reaped Minicom can still have a runscript descendant in its session.
    try:
        os.killpg(child.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    child.wait()


def capture(args: argparse.Namespace) -> None:
    timezone = ZoneInfo(args.timezone)
    minicom = shutil.which("minicom")
    if minicom is None or shutil.which("runscript") is None:
        raise RuntimeError("Install Minicom first: sudo apt install minicom")
    if shutil.which("date") is None:
        raise RuntimeError("The date command is required to set the device's UTC clock")
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        raise RuntimeError("Run capture in an interactive terminal so Minicom can display readings")
    wait_for_serial_port(args.port)

    output = args.output_dir.expanduser().resolve()
    ready = "Capture ready: saving environmental records."
    failed = "Capture setup failed: check the command error above."
    child = None
    profile_path = None
    terminal = termios.tcgetattr(sys.stdin.fileno())
    try:
        with capture_directory_lock(output), DailyCsvLog(output, timezone) as writer:
            current = writer.rotate(datetime.now(timezone))
            print(f"Saving CSV to {current}")
            print(f"New file each midnight in {args.timezone}; interval {args.interval_seconds}s.")
            print(
                "Minicom: Ctrl+A then Q, then Enter exits capture. Device logging continues.",
                flush=True,
            )
            with tempfile.TemporaryDirectory(prefix="logger-capture-") as temporary:
                work = Path(temporary)
                fifo = work / "capture.pipe"
                os.mkfifo(fifo, 0o600)
                script = work / "startup.run"
                script.write_text(
                    startup_script(
                        args.interval_seconds * 1000, ready, failed, args.device_log
                    ),
                    encoding="ascii",
                )
                profile, profile_path = create_minicom_profile()
                # Open both ends so Minicom can open its writer without deadlocking.
                descriptor = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
                try:
                    environment = os.environ.copy()
                    environment.pop("MINICOM", None)
                    child = subprocess.Popen(
                        [
                            minicom, "-8", "-c", "off", "-D", args.port,
                            "-b", "115200", "-C", str(fifo),
                            "--capturefile-buffer-mode=L", "-S", script.name, profile,
                        ],
                        cwd=work, env=environment, start_new_session=True,
                    )
                    lines = CaptureLines(writer, ready, failed)
                    deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
                    with selectors.DefaultSelector() as selector:
                        selector.register(descriptor, selectors.EVENT_READ)
                        while True:
                            now = datetime.now(timezone)
                            writer.rotate(now)
                            for _, _ in selector.select(timeout=0.5):
                                data = os.read(descriptor, 4096)
                                lines.feed(data, datetime.now(timezone))
                            if child.poll() is not None:
                                while True:
                                    try:
                                        data = os.read(descriptor, 4096)
                                    except BlockingIOError:
                                        break
                                    if not data:
                                        break
                                    lines.feed(data, datetime.now(timezone))
                                if child.returncode != 0 or not lines.ready:
                                    raise RuntimeError(
                                        "Minicom exited before capture completed successfully"
                                    )
                                return
                            if not Path(args.port).exists():
                                raise RuntimeError(f"Serial device disconnected: {args.port}")
                            if not lines.ready and time.monotonic() >= deadline:
                                raise RuntimeError(
                                    "Timed out waiting for device setup; capture stopped"
                                )
                finally:
                    if child is not None:
                        stop_minicom(child)
                    os.close(descriptor)
    finally:
        try:
            if profile_path is not None:
                profile_path.unlink(missing_ok=True)
        finally:
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSANOW, terminal)


def interrupt_capture(signum, frame):
    raise KeyboardInterrupt


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=os.environ.get("SERIAL_PORT", "/dev/ttyACM0"))
    parser.add_argument(
        "--output-dir", type=Path,
        default=Path(os.environ.get(
            "LOGGER_CAPTURE_DIR", Path(__file__).resolve().parents[1] / "logs"
        )),
    )
    parser.add_argument(
        "--timezone", default=os.environ.get("LOGGER_CAPTURE_TIMEZONE", "America/Denver")
    )
    parser.add_argument("--interval-seconds", type=int, default=30)
    parser.add_argument(
        "--device-log", type=device_log_name,
        default=os.environ.get("LOGGER_DEVICE_LOG", "readings.csv"),
        help="LittleFS log filename; choose a new name when the current file reaches 1 MiB",
    )
    args = parser.parse_args(argv)
    if not 1 <= args.interval_seconds <= 3600:
        parser.error("--interval-seconds must be between 1 and 3600")
    previous_handler = signal.signal(signal.SIGTERM, interrupt_capture)
    try:
        capture(args)
    except KeyboardInterrupt:
        print("\nCapture stopped. Device logging may still be active.", file=sys.stderr)
        return 130
    except (OSError, RuntimeError, ValueError, ZoneInfoNotFoundError) as error:
        print(f"\nCapture failed: {error}", file=sys.stderr)
        return 1
    finally:
        signal.signal(signal.SIGTERM, previous_handler)
    print("Capture closed. Device logging continues until log stop or reboot.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
