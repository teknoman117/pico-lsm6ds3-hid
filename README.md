# pico-lsm6ds3

RP2040 firmware that exposes an LSM6DS3 or LSM6DS3TR-C accelerometer + gyroscope as a USB HID
gamepad. The chip is detected by its WHO_AM_I register and the gamepad is named after it:
`LSM6DS3` or `LSM6DS3TR-C`. It works with the Linux generic HID driver, follows the kernel gamepad
spec, reports at 1000 Hz, and carries the sensor's hardware timestamp for sensor fusion.

See `docs/architecture.md` for internals and `AGENTS.md` for project rules.

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
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # or Debug
ninja -C build
```

## Flash

First time: hold BOOTSEL, plug in, copy `build/pico-lsm6ds3.uf2` to the drive.
Afterwards, picotool can reboot the running firmware into BOOTSEL itself:

```sh
picotool load -f -x build/pico-lsm6ds3.uf2
```

## Configuration

Edit `src/config.h`. These apply to whichever sensor is fitted:

- `IMU_XL_ODR`, `IMU_GY_ODR`: output data rate (default 1.66 kHz). The LSM6DS3 gyro tops out at
  1.66 kHz and is clamped; the LSM6DS3TR-C goes to 6.66 kHz.
- `IMU_XL_FS`, `IMU_GY_FS`: full scale (default ±4 g, ±2000 dps).
- `IMU_XL_FILTER`: `IMU_XL_FILTER_NONE`, `_LPF2` or `_HPF`, with `IMU_XL_LPF2_BW` / `IMU_XL_HPF_BW`.

Chip-specific filters:

- LSM6DS3: `IMU_DS3_XL_AA_MANUAL` / `IMU_DS3_XL_AA_BW` (analog anti-aliasing),
  `IMU_DS3_GY_HPF_EN` / `IMU_DS3_GY_HPF_BW` (gyro high-pass).
- LSM6DS3TR-C: `IMU_TRC_XL_ANALOG_BW`, `IMU_TRC_XL_LPF2_LOW_NOISE`, `IMU_TRC_GY_FILTER`
  (gyro high-pass / LPF1 combinations).

## Using it on Linux

As a gamepad (evdev / SDL / joystick APIs):

| evdev | Data |
|---|---|
| `ABS_X`, `ABS_Y`, `ABS_Z` | accel X, Y, Z |
| `ABS_RX`, `ABS_RY`, `ABS_RZ` | gyro X, Y, Z |
| `BTN_SOUTH`, `BTN_EAST` | always released (spec requirement) |
| `ABS_MISC` | sensor timestamp, 24-bit, ~25 µs/LSB |

```sh
evtest   # pick "LSM6DS3" or "LSM6DS3TR-C"
```

Each `SYN_REPORT` frame holds one sample and its `ABS_MISC` timestamp. Axes have no kernel
fuzz/deadzone applied. Note that SDL and games will list `ABS_MISC` as an extra, constantly
moving axis.

Raw reports are also available on `/dev/hidrawN`: 17 bytes, little endian, no report ID:

| Offset | Type | Field |
|---|---|---|
| 0 | int16 ×3 | accel X, Y, Z |
| 6 | int16 ×3 | gyro X, Y, Z |
| 12 | uint8 | buttons |
| 13 | uint32 | timestamp, 24-bit, ~25 µs/LSB |

The timestamp comes from the sensor's internal timer (same format on both chips) and wraps
modulo 2^24; compute `dt = ((t - t_prev) & 0xFFFFFF) * tick`. The tick is 25 µs nominal but
follows the sensor's internal oscillator (one LSM6DS3 measured ~24.5 µs, ~2% fast), so for
accurate absolute time estimate `tick` against the host clock.

Scale (LSB → unit), identical on both chips:

| Accel FS | mg/LSB | Gyro FS | mdps/LSB |
|---|---|---|---|
| ±2 g | 0.061 | ±125 dps | 4.375 |
| ±4 g | 0.122 | ±250 dps | 8.75 |
| ±8 g | 0.244 | ±500 dps | 17.5 |
| ±16 g | 0.488 | ±1000 dps | 35 |
| | | ±2000 dps | 70 |

```python
import struct
TICK = 25e-6  # nominal; calibrate against the host clock for absolute accuracy
with open("/dev/hidraw0", "rb", buffering=0) as f:
    prev = None
    while True:
        ax, ay, az, gx, gy, gz, btn, ts = struct.unpack("<6hBI", f.read(17))
        if prev is not None:
            dt = ((ts - prev) & 0xFFFFFF) * TICK
        prev = ts
```

To check the kernel mapping without hardware (creates a virtual device via `/dev/uhid`):

```sh
sudo python3 tools/uhid_check.py build/pico-lsm6ds3.elf [LSM6DS3TR-C]
```

hidraw access for your user (udev rule, e.g. `/etc/udev/rules.d/70-lsm6ds3.rules`):

```
KERNEL=="hidraw*", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000a", TAG+="uaccess"
```

## Status LED

| Colour | Meaning |
|---|---|
| Red | No LSM6DS3 / LSM6DS3TR-C detected (retries every second) |
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

Release builds contain no debug output.

## Third-party

- `lib/lsm6ds3-pid`: STMicroelectronics LSM6DS3 driver, BSD-3-Clause.
- `lib/lsm6ds3tr-c-pid`: STMicroelectronics LSM6DS3TR-C driver, BSD-3-Clause.
- `src/ws2812.pio`: from pico-examples, BSD-3-Clause.
