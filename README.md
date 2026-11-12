# LINK BME280 logger

A small Zephyr application for an **external BME280 over I²C**, with a **USB CDC ACM shell**, CSV recording to LittleFS, and simultaneous streaming. It starts idle. Logging and streaming each default to one reading every 30 seconds. No Bluetooth, Wi-Fi, cellular modem, or network-core firmware is built.

The application uses the Zephyr revision shipped with nRF Connect SDK 3.0.0 and Zephyr SDK 0.17.0. `west.yml` pins the needed modules instead of pulling the full Nordic SDK. Board hardware belongs in the board overlays/BSP; application modules do not contain board pin numbers. A separate `Hyrule-Zephyr-SDK` checkout is left untouched and can be enabled with `BSP_DIR` when developing a custom board.

## Ubuntu/Debian setup

Use a dedicated west workspace. If you already have this changed checkout, run the setup command there instead of cloning again. The commands below require these logger changes to be present in the checkout; cloning an older upstream revision will not include them.

```bash
mkdir -p "$HOME/link-workspace"
cd "$HOME/link-workspace"
git clone https://github.com/darthdirk/link-application.git
cd link-application
bash scripts/setup-local.sh
```

`setup-local.sh` is the full setup command: installs Linux prerequisites, creates the parent `.venv`, installs pinned tools, fetches pinned west modules, checks SDK download hashes, installs the ARM toolchain, installs Git hooks, runs the small Unity suite and static checks, and builds Thingy:53. It does not flash a device. Rerun later with `just fullsetup`. CI/prepared hosts can use `--no-apt`, `--no-sdk`, or `--no-build`.

Activate the same tools for your terminal and VS Code:

```bash
eval "$(direnv hook bash)"
[ -e .envrc.private ] || cp .envrc.private.example .envrc.private
# Edit .envrc.private for this PC; the default probe is 504403721.
direnv allow
just --list
code local.code-workspace
```

Add `eval "$(direnv hook bash)"` once to `~/.bashrc` if desired. Private settings are ignored by Git. Run `direnv allow` again after changing them. Open VS Code from this activated terminal so its debugger inherits the SDK and build-directory paths.

Install the [official SEGGER J-Link Linux package](https://www.segger.com/downloads/jlink/) separately, accepting its license. For a downloaded Debian package, use `sudo apt install ./JLink_Linux_V…_x86_64.deb` with its actual filename. Flash/debug require `JLinkExe` and `JLinkGDBServer` on `PATH`. Give your login serial-port access with `sudo usermod -aG dialout "$USER"`, then log out and back in.

## Wiring and boards

For mount errors or an unready sensor, see [storage and I2C diagnostics](docs/bringup.md), checked against the NCS 3.0.0 examples.

Both builds use the external BME280 at **7-bit I²C address `0x77`**, with SDO and CSB tied to the sensor's I/O supply. Use a BME280, not a BMP280 (which does not measure humidity). Connect ground, ensure suitable SDA/SCL pull-ups, and match the board's logic voltage. An address change requires changing `reg` and the unit address in `app/boards/logger-common.dtsi`, then rebuilding and flashing; sensor readiness is checked at boot.

| BME280 connection | Thingy:53 (hardware 1.1.0) | nRF5340 DK |
| --- | --- | --- |
| SCL | TP5, P1.03 | P1.03 |
| SDA | TP4, P1.02 | P1.02 |
| VDD/VDDIO and pull-ups | Contact 4, `VDD_nRF53` | DK VDD logic rail |
| GND | Contact 20 | GND |

The uploaded `PCA20053_Schematic_And_PCB.pdf`, revision 1.1.0 sheet 2, confirms TP4/P1.02 and TP5/P1.03. Both builds use I²C1. This replaces the earlier edge-connector SDA/SCL assignment. The Thingy bus already has 4.7 kΩ pull-ups powered by its switched sensor rail, which remains enabled. Use a breakout compatible with that logic voltage; do not add pull-ups to an unrelated supply. The onboard BME688 at `0x76` shares the bus but its driver is disabled; it does not conflict with the external BME280 at `0x77`.

The green power indicator pulses for 100 ms every 10 seconds, starting ten seconds after application initialization. Thingy uses P1.06 (active high); the DK uses its green LED0 (active low). `heartbeat.c` uses a separate delayed work item and the board's `heartbeat-led` alias. GPIO access runs in thread context. The LED supply stays enabled, and the kernel remains tickless; the pulse adds short wakeups even when recording and streaming are idle. This is an activity indicator, not a watchdog guarantee.

The pinned Zephyr board definition exposes **`thingy53/nrf5340/cpuapp`**, without revision qualifiers. Your example `@2_0_0` and `@3_0_0` targets are not defined in this pin. `just build_53` uses the supported target; confirm the connector mapping against the hardware revision printed on your board.

Thingy:53 needs an external J-Link attached to SWD; its USB cable provides the serial console. On the nRF5340 DK, use the **nRF USB connector** for this CDC ACM console; the separate debug USB connector is for its onboard probe. Set `DEBUG_53_SN` to the actual probe being used for either target.

## Build, flash, and debug

```bash
just build_53
just flash_53
just serial                  # default /dev/ttyACM0; Ctrl+] exits
```

For the DK:

```bash
just build_dk
just flash_dk
```

These are standalone application-core builds, with debug symbols, debug optimizations, and Zephyr thread information. **Flashing Thingy:53 replaces the factory bootloader/application at address zero.** The factory USB DFU workflow is therefore replaced by SWD programming. Ordinary flashing programs the image's sectors; the recipes do not run recovery, mass erase, or format the logger filesystem. Recovering a locked chip or restoring the factory firmware is a separate deliberate operation.

Debug using the same external-server workflow as your example:

```bash
just debug_server_53
# Leave that terminal running. In VS Code, choose:
# “Thingy:53 — external J-Link server”, then press F5.
```

Use `just debug_server_dk` with the DK launch entry. GDB connects to `localhost:2331`; firmware ELF paths are `build/thingy53/zephyr/zephyr.elf` and `build/dk/zephyr/zephyr.elf`. This application does not use sysbuild. To connect the local debug server to a remote J-Link, set `IP_DEBUG=true` and `DEBUG_HOST` to that probe/server address. The pinned west J-Link runner selects a remote probe by IP; local USB mode selects serial `504403721` by default. DNS lookup occurs only in IP mode.

## Daily capture with Minicom

`just capture` opens Minicom, sets the board's UTC clock from the PC, saves 30-second logging and streaming rates, and starts both activities. Close other programs using the serial port first. The PC clock must be correct. Existing installations need Minicom 2.8 or newer and timezone data; the full setup script installs these automatically:

```bash
sudo apt install minicom tzdata
direnv allow
direnv exec . just build_53 &&
direnv exec . just flash_53 &&
direnv exec . just capture
```

On later runs, use `direnv exec . just capture` without flashing again. The program sends `time set YYYY-MM-DDTHH:MM:SSZ` using the current UTC date/time at startup, then waits for each setup command to succeed. It sets both rates to 30,000 ms and saves `config.cfg`, so previously saved five-second rates are updated too. It never formats storage; initialization errors stop capture with an error.

Minicom displays the device terminal while the program writes only environmental CSV records to `logs/env-YYYY-MM-DD.csv`. For example, November 17 in Denver uses `logs/env-2026-11-17.csv`. Each file has one header. **Rollover is at midnight in America/Denver**, following local calendar dates through daylight-saving changes. Restarting during the same day appends to the existing file. Files with a different header or incomplete final row are rejected for inspection. Each accepted row is flushed and synced to disk.

Set the port, output folder, and timezone in `.envrc.private`, then run `direnv allow`:

```bash
export SERIAL_PORT=/dev/ttyACM0
export LOGGER_CAPTURE_DIR="$HOME/link-logs"
export LOGGER_CAPTURE_TIMEZONE=America/Denver
```

For a stable USB path, `SERIAL_PORT` can use the appropriate entry in `/dev/serial/by-id/`. `logs/` is ignored by Git. Minicom is the only reader of the serial device; its capture stream feeds the dated-file writer. The program uses a temporary Minicom profile and removes it on exit, preserving your normal Minicom settings. Leave Minicom's capture enabled; this program manages its destination and rollover.

Keep the PC awake and the program running for continuous capture. To stop the board as well, enter `stream stop` and `log stop`, wait for successful completion, then exit Minicom with **Ctrl+A, Q, Enter**. Closing Minicom alone ends PC capture; the board continues recording to LittleFS in `readings.csv`, subject to its existing 1 MiB file limit. When that file fills, set `export LOGGER_DEVICE_LOG=readings-2.csv` in `.envrc.private` and run `direnv allow` before restarting capture; previous logs are preserved. Midnight rollover applies to the PC files. After a board reset or USB reconnection, restart `just capture` to set UTC and resume acquisition. `Queued`, `Done`, and diagnostic messages remain visible in the terminal and are excluded from the PC CSV files.

## Device commands

Commands run in the USB serial terminal, not your Linux shell. `help` lists them. Shell handlers copy requests into a bounded queue; an acknowledgement that a request was queued is followed by its execution result.

The USB shell starts with an empty prompt and VT100 colors/cursor control disabled in `app/prj.conf`. These defaults apply after every reboot, so Minicom captures do not contain repeated `logger>` prompts or shell terminal-formatting sequences. Typed commands, `Queued`/`Done` acknowledgements, and enabled diagnostic messages can still appear. Restoring the prompt and VT100 support requires changing these build settings and reflashing.

On first use, run `status`. Blank or damaged flash is **not automatically formatted**. If you intend to initialize the logger partition, explicitly run:

```text
storage format CONFIRM
```

This deletes all logger files and saved configuration in the dedicated 4 MiB LittleFS partition. Do not run it when preserving existing logs.

Set UTC after every boot. In a Linux terminal, `just time_command` prints a current `time set …` command; paste it into the device terminal:

```text
time set 2026-11-10T15:30:00Z
time get
config show
log start experiment.csv
stream start
status
log flush
stream stop
log stop
log list
log dump experiment.csv
```

`log start` without a name uses `readings.csv`. Existing compatible CSV files are appended, with a 1 MiB limit per file. Choose another filename when that limit is reached. `log stop` syncs and closes the file; `log flush` syncs an open file. Stop both logging and streaming before `log list` or `log dump` so a long USB dump cannot delay acquisition. Streaming can run on its own; stopping either activity leaves the other running.

Files and saved configuration persist across ordinary resets and flashes. Wait for a successful `log stop` or `log flush` before resetting or removing power; the default periodic flush is every ten seconds. A sudden power loss can lose records since the last completed flush. Every boot starts idle: logging and streaming must be started again.

UTC is maintained from Zephyr's monotonic uptime after `time set`; there is no assumed battery-backed calendar RTC. UTC starts unset after a reset/power loss. Records use `epoch_ms=-1` until it is set. Both saved CSVs and live readings use decimal degrees Celsius, percent RH, and hPa, with no `STREAM,` prefix:

```text
uptime_ms,epoch_ms,temperature_C,humidity_pct,pressure_hPa
113280,1794331567132,24.550,20.603,844.77
```

Older files with `temperature_mC,humidity_mpercent,pressure_Pa` in their header are still supported. `log dump` displays them in the readable units above without rewriting the file. On the first `log start` for an old file, the logger validates and converts all existing rows into a temporary file, syncs and closes it, then atomically replaces the original before appending. If validation or storage fails before replacement, the original remains unchanged. Conversion needs room for the temporary copy and can briefly pause streaming while that one-time operation runs. No format is needed. A damaged or incomplete last row is rejected; use a different filename to keep recording while preserving the damaged file for inspection.

A disconnected or slow USB host may lose stream/output records; `status` reports the drop count. Recording does not depend on USB delivery. A storage error stops recording and reports the error while leaving streaming enabled. Sensor failures are counted rather than storing fabricated readings.

## Saved configuration and diagnostic messages

The runtime file is `/lfs/config.cfg`; the root `config.cfg` shows its format. Rate changes apply in RAM immediately. Save explicitly to persist them:

```text
config set log_rate_ms 30000
config set stream_rate_ms 30000
config set flush_rate_ms 10000
config save
config show
```

All intervals are milliseconds, from 100 to 3,600,000. Defaults are 30,000 ms for log/stream and 10,000 ms for flush. An existing `/lfs/config.cfg` survives flashing and overrides these defaults; run the commands above to update a previously configured board to 30-second sampling. Saving writes and syncs a temporary file before renaming it. A malformed configuration is rejected as a whole; defaults remain active. A sudden power loss can lose records since the last completed flush; use `log flush`/`log stop` before removing power.

Diagnostic messages are separate from CSV recordings. Select the minimum severity per module or globally:

```text
debug list
debug app info
debug sensors warn
debug logging err
debug com off
debug all debug
```

Levels are `off`, `err`, `warn`, `info`, `debug`; a level includes more severe messages. These runtime filters reset on reboot.

## Modules and execution

| Module | Responsibility |
| --- | --- |
| `app.c` / `state.c` | Own commands, four states, and independent acquisition/flush deadlines |
| `com.c` / `shell.c` | USB CDC ACM transport, bounded output, command parsing |
| `sensors.c` | BME280 acquisition through Zephyr's sensor API |
| `logging.c` | LittleFS mount, bounded CSV files, flush/list/dump |
| `config.c` | Validate/load/save `config.cfg` |
| `timekeeping.c` | Validated UTC conversion and uptime-based clock |
| `record.c` | Portable CSV record formatting |
| `heartbeat.c` | Brief green power LED pulse every ten seconds |

The shell thread enqueues requests; a communications worker drains bounded output queues. One application thread owns sensor reads, filesystem operations, configuration, and state changes, avoiding shared mutable file handles and timer-callback races. The states are `IDLE`, `LOGGING`, `STREAMING`, and `BOTH`; a failed log start never commits the logging state. Sensor reads and flash writes run in thread context. Matching log/stream deadlines share one reading.

`CONFIG_TICKLESS_KERNEL=y` remains enabled. When idle the application thread blocks for commands; the heartbeat independently schedules its short LED pulse. The BME280 uses forced measurements and sleeps between them, with runtime power management for supported peripherals. On Thingy, 3V3 powers the LED and the switched sensor rail supplies the I²C pull-ups, so both rails remain enabled. USB, the debugger, board regulators, and attached breakout pull-ups affect current consumption; battery current still needs measurement on hardware.

## Checks

```bash
just format        # apply the repository clang-format rules
just check         # format check, cppcheck, focused Unity tests
just hooks         # reinstall local Git hooks if needed
```

The Unity suite covers UTC/calendar validation, all start/stop state combinations, record formatting bounds, and legacy CSV validation. Python capture tests cover calendar rollover, daylight-saving transitions, append/restart, and invalid files. Cppcheck checks `state.c`, `timekeeping.c`, and `record.c`. Zephyr kernel/shell macros exceed its preprocessor support, so the remaining firmware modules are checked by ARM GCC with `-Wall -Wextra -Werror -Wformat=2`. CI builds both boards and runs the same checks. These checks do not replace hardware validation of USB enumeration, I²C wiring, flash retention, and power consumption.
