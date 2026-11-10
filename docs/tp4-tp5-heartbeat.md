# TP4/TP5 wiring and green power indicator update

This update replaces the original Thingy edge-connector sensor assignment.
The supplied PCA20053 hardware 1.1.0 schematic confirms:

| Signal | Thingy connection |
| --- | --- |
| External BME280 SDA | TP4 / P1.02 |
| External BME280 SCL | TP5 / P1.03 |
| I2C address | 0x77 |
| Green LED | P1.06, active high |

The external BME280 now uses I2C1. Its address does not conflict with the onboard
BME688 at 0x76. The onboard sensor drivers remain disabled. Keep the switched
sensor rail enabled for the existing 4.7 kOhm I2C pull-ups and the 3V3 rail enabled
for the green LED; both retain their board defaults. Match the external sensor's
I/O voltage to the board bus and use a common ground.

`heartbeat.c` controls the LED through a `heartbeat-led` devicetree alias. It
starts inactive, then emits a 100 ms pulse every ten seconds using one delayed
work item. It runs independently of recording and streaming. The application
still blocks when idle, and the kernel stays tickless; the heartbeat adds brief
scheduled wakeups. This indicates system activity rather than watchdog health.
The DK build uses its green LED0 with the correct active-low polarity.

After applying the patch in the existing checkout, close the serial terminal
and use the normal build/flash recipes:

```bash
direnv exec . just build_53
direnv exec . just flash_53
direnv exec . just serial
```

The selected J-Link remains 504403721 unless overridden in `.envrc.private`.
Normal flashing does not mass erase or format LittleFS. Wait ten seconds after
boot for the first green pulse, then use `status` to check sensor readiness.
The nRF5340 DK uses the same P1.02/P1.03 sensor wiring; build/flash with the DK
recipes when using that board.

If the optional diagnostic build was previously installed, use the normal build
above for this update. The corrected sensor can be checked through the optional
I2C shell with `i2c read_byte i2c@9000 0x77 0xd0`; a BME280 returns 0x60.
