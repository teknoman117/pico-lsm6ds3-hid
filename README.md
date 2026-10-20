# pico-lsm6ds3

RP2040 firmware that exposes an LSM6DS3 accelerometer + gyroscope as a USB HID gamepad named
`LSM6DS3`. It works with the Linux generic HID driver, follows the kernel gamepad spec, reports at
1000 Hz, and carries the sensor's hardware timestamp for sensor fusion.

See `docs/architecture.md` for internals and `AGENTS.md` for project rules.

## Wiring

| LSM6DS3 | Pico |
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

Edit `src/config.h`:

- `IMU_XL_ODR`, `IMU_GY_ODR`: output data rate (default 1.66 kHz).
- `IMU_XL_FS`, `IMU_GY_FS`: full scale (default ±4 g, ±2000 dps).
- `IMU_XL_FILTER`: `IMU_XL_FILTER_NONE`, `_LPF2` or `_HPF`, with `IMU_XL_LPF2_BW` / `IMU_XL_HPF_BW`.
- `IMU_XL_AA_BW`: analog anti-aliasing bandwidth, or `IMU_XL_AA_AUTO`.
- `IMU_GY_HPF_EN`, `IMU_GY_HPF_BW`: gyro high-pass filter.

## Using it on Linux

As a gamepad (evdev / SDL / joystick APIs):

| evdev | Data |
|---|---|
| `ABS_X`, `ABS_Y`, `ABS_Z` | accel X, Y, Z |
| `ABS_RX`, `ABS_RY`, `ABS_RZ` | gyro X, Y, Z |
| `BTN_SOUTH`, `BTN_EAST` | always released (spec requirement) |
| `ABS_MISC` | sensor timestamp, 24-bit, 25 µs/LSB |

```sh
evtest   # pick "LSM6DS3"
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
| 13 | uint32 | timestamp, 24-bit, 25 µs/LSB |

The timestamp comes from the LSM6DS3's internal timer and wraps every ~419 s; compute
`dt = ((t - t_prev) & 0xFFFFFF) * 25e-6`.

Scale (LSB → unit):

| Accel FS | mg/LSB | Gyro FS | mdps/LSB |
|---|---|---|---|
| ±2 g | 0.061 | ±125 dps | 4.375 |
| ±4 g | 0.122 | ±250 dps | 8.75 |
| ±8 g | 0.244 | ±500 dps | 17.5 |
| ±16 g | 0.488 | ±1000 dps | 35 |
| | | ±2000 dps | 70 |

```python
import struct
with open("/dev/hidraw0", "rb", buffering=0) as f:
    prev = None
    while True:
        ax, ay, az, gx, gy, gz, btn, ts = struct.unpack("<6hBI", f.read(17))
        if prev is not None:
            dt = ((ts - prev) & 0xFFFFFF) * 25e-6
        prev = ts
```

To check the kernel mapping without hardware (creates a virtual device via `/dev/uhid`):

```sh
sudo python3 tools/uhid_check.py build/pico-lsm6ds3.elf
```

hidraw access for your user (udev rule, e.g. `/etc/udev/rules.d/70-lsm6ds3.rules`):

```
KERNEL=="hidraw*", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000a", TAG+="uaccess"
```

## Status LED

| Colour | Meaning |
|---|---|
| Red | IMU not detected (retries every second) |
| Yellow | Waiting for USB host |
| Green | Streaming |
| Blue | USB suspended |

## Debug output

Debug builds print over SEGGER RTT through the debug probe (no UART/USB serial). In VS Code the
"Pico Debug (Cortex-Debug)" launch config opens the RTT console. With plain OpenOCD:

```
rtt setup 0x20000000 0x42000 "SEGGER RTT"
rtt start
rtt server start 9090 0
```

Release builds contain no debug output.

## Third-party

- `lib/lsm6ds3-pid`: STMicroelectronics LSM6DS3 driver, BSD-3-Clause.
- `src/ws2812.pio`: from pico-examples, BSD-3-Clause.
