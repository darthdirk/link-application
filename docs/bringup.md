# Thingy:53 storage and BME280 bring-up

## Filesystem configuration

NCS 3.0.0 selects Zephyr `v4.0.99-ncs1`, which resolves to the application's
`a0e545cb437a44b6f9cd176ef28a055403f8d2d8` pin. Its
[LittleFS example](https://github.com/nrfconnect/sdk-zephyr/blob/v4.0.99-ncs1/samples/subsys/fs/littlefs/src/main.c#L262-L281)
supports either a devicetree `fstab` entry or a C `fs_mount_t`. This app uses the
C mount in `logging.c`. The Thingy board overlay includes `logger-common.dtsi`,
which defines the external QSPI partition at offset `0x400000`, size `0x400000`.
An additional board filesystem node is not required.

The [sample README](https://github.com/nrfconnect/sdk-zephyr/blob/v4.0.99-ncs1/samples/subsys/fs/littlefs/README.rst#L26-L35)
shows the same `Corrupted dir pair` error on new storage. The sample then formats
automatically. This logger deliberately uses `FS_MOUNT_FLAG_NO_FORMAT` so a failed
mount cannot silently erase logs. Here `-14` is the filesystem adapter's mapping
of LittleFS metadata corruption, not a CPU null-pointer fault.

Blank, incompatible, corrupt, or incorrectly read contents can produce this
message. If this is a new partition, or you intend to discard its logs and saved
configuration, initialize it once in the device shell:

```text
storage format CONFIRM
```

Wait for `Done`, then run `status` and `config save`. Wait for the save to finish
before resetting. After reset, `status` should show storage ready and `config show`
should display the saved settings. This checks filesystem initialization and
retention independently of the sensor. If formatting fails or the error returns
after a successful save/reset, retain the complete output rather than repeatedly
formatting.

The Nordic QSPI example chooses 4096-byte layout pages; this application currently
uses the driver's valid 65536-byte layout pages (64 blocks in 4 MiB). Do not change
that geometry on an existing filesystem as a remedy for missing metadata.

## Identify the BME280 bus

The supplied PCA20053 hardware 1.1.0 schematic confirms TP4 is SDA/P1.02 and TP5
is SCL/P1.03. The normal overlay now binds the external BME280 at `0x77` to I2C1
on these pins. Its green heartbeat is P1.06, active high. Keep the default sensor
rail enabled for the bus pull-ups and the 3V3 rail enabled for the LED supply.

The optional diagnostic image enables the native Zephyr I2C shell and both buses:

| Shell device | Pins | Configuration source |
| --- | --- | --- |
| `i2c@8000` | SDA P0.04, SCL P0.05 | Optional diagnostic edge bus |
| `i2c@9000` | SDA P1.02/TP4, SCL P1.03/TP5 | Normal BME280 bus |

Build/flash it from an activated `.envrc` terminal:

```bash
just build_53_diagnostics
just flash_53_diagnostics
just serial
```

It uses a separate `build/thingy53-diagnostics` directory, the same selected
J-Link, and normal sector programming. It does not format LittleFS or change the
normal BME280 binding. Keep logging and streaming stopped and run:

```text
i2c read_byte i2c@9000 0x77 0xd0
i2c read_byte i2c@8000 0x77 0xd0
```

`Output: 0x60` identifies a BME280 on that bus. `0x58` identifies a BMP280 with no
humidity channel; `0x61` identifies a BME680/BME688, which requires a different
driver. A failed read means that bus/address did not return a usable identity;
it does not alone identify whether wiring, supply, pull-ups, or the address is wrong.

For a control reading from the onboard BME688, whose driver remains disabled:

```text
i2c read_byte i2c@9000 0x76 0xd0
```

That sensor normally returns `0x61`. The external BME280 should return `0x60` at
`0x77` on I2C1. Adding a filesystem node would not correct a sensor bus mismatch.

Return to the normal image with `just build_53` and `just flash_53` after the
production wiring/binding is established. The normal build does not include the
I2C shell or enable the extra diagnostic bus.
