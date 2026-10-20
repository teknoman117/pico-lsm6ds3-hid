# pico-lsm6ds3

RP2040 (or RP2350 / Pico 2) firmware that reads an LSM6DS3 or LSM6DS3TR-C over SPI at 1666 Hz,
sums 8 samples per window, timestamps each window with the Pico's crystal clock, and sends one
USB HID report per window (~208 Hz) to a Linux host. The device is named after the detected
sensor (`LSM6DS3` or `LSM6DS3TR-C`) and appears as an evdev device via the generic HID driver.

Requirements: `docs/requirements.md`. Internals: `docs/architecture.md`. Project rules: `AGENTS.md`.

## Wiring

| LSM6DS3 / LSM6DS3TR-C | Pico |
|---|---|
| SDA / SDI (MOSI) | GP19 |
| SDO / SA0 (MISO) | GP16 |
| SCL / SPC (SCK) | GP18 |
| CS | GP17 |
| INT1 | GP20 |

NeoPixel-compatible status LED on GP23.

## Build

Requires the Pico SDK 2.x toolchain (as installed by the Raspberry Pi Pico VS Code extension in
`~/.pico-sdk`).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # or Debug; add -DPICO_BOARD=pico2 for Pico 2
ninja -C build
```

## Flash

First time: hold BOOTSEL, plug in, copy `build/pico-lsm6ds3.uf2` to the drive.
Afterwards, picotool can reboot the running firmware into BOOTSEL itself:

```sh
picotool load -f -x build/pico-lsm6ds3.uf2
```

## Configuration

Edit `src/config.h`. Defaults follow `docs/requirements.md`:

- `IMU_XL_ODR`, `IMU_GY_ODR`: 1666 Hz. Keep them equal; gyro data-ready paces sampling.
- `IMU_XL_FS`, `IMU_GY_FS`: ±4 g, ±500 dps.
- `IMU_XL_FILTER`: off (`IMU_XL_FILTER_NONE`); `_LPF2` or `_HPF` with `IMU_XL_LPF2_BW` / `IMU_XL_HPF_BW`.
- `IMU_AVERAGE_N`: 8 samples per report. `IMU_SETTLE_SAMPLES`: 168 discarded at start (100.8 ms).

The report rate is the gyro ODR divided by `IMU_AVERAGE_N`; the host polls every 1 ms.

Chip-specific filters:

- LSM6DS3: `IMU_DS3_XL_AA_MANUAL` / `IMU_DS3_XL_AA_BW` (analog anti-aliasing, 100 Hz),
  `IMU_DS3_GY_HPF_EN` / `IMU_DS3_GY_HPF_BW` (gyro high-pass, off).
- LSM6DS3TR-C: `IMU_TRC_XL_ANALOG_BW` (400 Hz), `IMU_TRC_XL_LPF2_LOW_NOISE`,
  `IMU_TRC_GY_FILTER` (gyro high-pass / LPF1, off).

## Using it on Linux

| evdev | Field |
|---|---|
| `ABS_RX`, `ABS_RY`, `ABS_RZ` | gyro X, Y, Z: sum of 8 raw samples |
| `ABS_X`, `ABS_Y`, `ABS_Z` | accel X, Y, Z: sum of 8 raw samples |
| `ABS_MISC` | timestamp: window midpoint, Pico µs mod 2^31 |
| `ABS_RUDDER` | sequence: +1 per report, mod 2^31 |
| `ABS_THROTTLE` | temperature: `OUT_TEMP` raw, last sample |
| `ABS_WHEEL` | flags: bit 0 sample missed, bit 1 USB busy (reports were replaced), bit 2 error |

- **Scaling:** divide sums by 8, then use the full-scale sensitivity:

  | Accel FS | mg/LSB | Gyro FS | mdps/LSB |
  |---|---|---|---|
  | ±2 g | 0.061 | ±125 dps | 4.375 |
  | ±4 g | 0.122 | ±250 dps | 8.75 |
  | ±8 g | 0.244 | ±500 dps | 17.5 |
  | ±16 g | 0.488 | ±1000 dps | 35 |
  | | | ±2000 dps | 70 |

  Temperature: 25 °C + raw / 16 (LSM6DS3) or raw / 256 (LSM6DS3TR-C).
- **Timing:** use the timestamp, not event arrival time. It comes from the Pico's crystal
  (±20 ppm); the sensor's own rate is only nominal (one LSM6DS3 measured 0.9% fast).
- **Events:** the input core sends no event for an unchanged axis; keep the last value of each and
  assemble a sample at every `SYN_REPORT`. Sequence and timestamp change in every report.
- **Errors:** with no, unknown or misconfigured sensor the device keeps reporting at the normal
  rate with all data 0 and flag bit 2 set.
- **Classification:** udev tags the device `ID_INPUT_ACCELEROMETER`, not a joystick, so
  SDL-based joystick code ignores it. The kernel's `joydev` still creates a `/dev/input/jsN`.

Raw reports are also on `/dev/hidrawN`: 35 bytes, little endian, no report ID, in this order:
gyro sums (3 × int32), accel sums (3 × int32), timestamp (uint32), sequence (uint32),
temperature (int16), flags (uint8).

```python
import struct
REPORT = struct.Struct("<6i2IhB")
with open("/dev/hidraw0", "rb", buffering=0) as f:
    while True:
        gx, gy, gz, ax, ay, az, ts, seq, temp, flags = REPORT.unpack(f.read(REPORT.size))
```

hidraw access for your user (udev rule, e.g. `/etc/udev/rules.d/70-lsm6ds3.rules`):

```
KERNEL=="hidraw*", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000a", TAG+="uaccess"
```

(`0009` instead of `000a` on a Pico 2.)

## Verification

```sh
python3 tools/verify_evdev.py 60      # requirements section 7 steps 1-2; keep the sensor still
sudo python3 tools/uhid_check.py build/pico-lsm6ds3.elf   # kernel mapping via /dev/uhid, no hardware
```

## Status LED

| Colour | Meaning |
|---|---|
| Red | Sensor error: none, unknown or failed read-back (retries every second) |
| Yellow | Waiting for USB host |
| Green | Streaming |
| Blue | USB suspended |

## Debug output

Debug builds print over SEGGER RTT through the debug probe (no UART/USB serial). VS Code launch
configs (Cortex-Debug + the bundled OpenOCD) open an "RTT" console automatically:

- "Pico Debug (Cortex-Debug)": reset, flash, run to `main`.
- "Pico Attach (Cortex-Debug, RTT)": attach to a running board without reset or reflash.

RTT only appears with a Debug build on the board; a Release build has no RTT control block.
With plain OpenOCD:

```
rtt setup 0x20000000 0x42000 "SEGGER RTT"
rtt start
rtt server start 9090 0
```

then `nc localhost 9090`.

## Third-party

- `lib/lsm6ds3-pid`: STMicroelectronics LSM6DS3 driver, BSD-3-Clause.
- `lib/lsm6ds3tr-c-pid`: STMicroelectronics LSM6DS3TR-C driver, BSD-3-Clause.
- `src/ws2812.pio`: from pico-examples, BSD-3-Clause.
