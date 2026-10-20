# Architecture

RP2040 firmware that streams LSM6DS3 accel/gyro data as a USB HID gamepad named `LSM6DS3`.
Bare-metal, single loop on core 0. No RTOS.

## Layout

| Path | Role |
|---|---|
| `src/config.h` | All tunables: pins, ODR, full scale, filters, USB IDs, LED. Edit here first. |
| `src/main.c` | Init, then the main loop. Owns no hardware directly. |
| `src/imu.[ch]` | LSM6DS3 setup and reads. SPI glue for the ST driver. |
| `src/gamepad.[ch]` | HID report descriptor, report layout, HID callbacks. |
| `src/usb_descriptors.c` | Device/config/string descriptors (HID + picotool reset interface). |
| `src/tusb_config.h` | TinyUSB device config. |
| `src/status_led.[ch]`, `src/ws2812.pio` | NeoPixel status via PIO. |
| `src/debug.h` | `DBG()` printf macro, compiled out when `NDEBUG` is defined. |
| `tools/uhid_check.py` | Host-side check of the HID mapping via `/dev/uhid`, no hardware needed. |
| `lib/lsm6ds3-pid/` | ST's platform-independent LSM6DS3 driver, vendored unmodified. See `UPSTREAM.md`. |

## Hardware

| Signal | GPIO |
|---|---|
| SPI0 MISO | 16 |
| SPI0 CS (GPIO-driven) | 17 |
| SPI0 SCK | 18 |
| SPI0 MOSI | 19 |
| IMU INT1 | 20 |
| NeoPixel | 23 |

SPI mode 3, 10 MHz requested. Bus bring-up order (once per boot): CS driven high, SPI block
configured for mode 3, then pins muxed so SCK starts idle high with CS deasserted; then 20 ms boot wait.

## Data flow

1. `imu_init()` checks WHO_AM_I (0x69), soft-resets, disables I2C, enables BDU and auto-increment,
   applies full scale and filters, routes accel/gyro data-ready to INT1, enables and zeroes the
   25 us timestamp counter, then sets ODR last.
2. Main loop polls INT1 level. When high, one 12-byte burst reads gyro XYZ then accel XYZ,
   followed by the 24-bit timestamp (read twice to reject a carry between bytes).
3. When the HID endpoint is free, the latest sample is sent. Interrupt IN endpoint, `bInterval` 1 ms
   (1000 Hz, the full-speed maximum).
4. If the IMU is missing, init is retried every second; USB stays up so picotool still works.

## HID mapping

Top-level application is Generic Desktop / Multi-axis Controller; axes and buttons sit in a
Physical Game Pad collection. The kernel's `hid-generic` + `hid-input` then produce codes per
`Documentation/input/gamepad.rst`:

| Report field | Source | evdev code |
|---|---|---|
| X, Y, Z (int16) | accel X, Y, Z raw | `ABS_X`, `ABS_Y`, `ABS_Z` |
| Rx, Ry, Rz (int16) | gyro X, Y, Z raw | `ABS_RX`, `ABS_RY`, `ABS_RZ` |
| Button 1, 2 | unused, always 0 | `BTN_SOUTH`, `BTN_EAST` |
| Sensor 0x20:0x0529 Time Stamp (uint32) | sensor timer | `ABS_MISC` |

Why not a Game Pad application: `hid-input` gives every field in one fuzz = range/256 and
flat = range/16. That would suppress small IMU changes and drop most timestamp updates.
Buttons still map to `BTN_GAMEPAD+n` through the Physical Game Pad usage.

The Sensor page usage appears only as a field, never as a collection, so `hid-core` does not
group the device as a sensor hub.

Report is 17 bytes, little endian, no report ID: 6 x int16 axes, 1 button byte, uint32 timestamp.
The timestamp is the sensor's 24-bit counter (25 us/LSB nominal, wraps every ~419 s); hosts take
deltas modulo 2^24. It is captured when the sample is fetched, within one loop pass of data-ready.
It arrives in the same `SYN_REPORT` frame as the axes it belongs to.

The two buttons exist so the device advertises `BTN_GAMEPAD`, which the spec requires.
Axis range is the full int16 range, so scale follows `IMU_XL_FS` / `IMU_GY_FS`.

## USB

- VID:PID `2E8A:000A` so `picotool` finds the device without `--vid/--pid`.
- No manufacturer string, so the kernel device name is exactly the product string.
- Serial number is the flash unique ID.
- Interface 1 is the SDK `pico_usb_reset` vendor interface (`picotool reboot`, `picotool load -f`).

## Status LED

| Colour | State |
|---|---|
| Red | IMU not detected |
| Yellow | IMU OK, USB not configured |
| Green | Streaming |
| Blue | USB suspended |

## Debug output

Debug builds enable `pico_stdio_rtt` and `DBG()` prints over SEGGER RTT through the debug probe.
Release builds define `NDEBUG`: `DBG()` expands to nothing and no stdio driver is linked.
UART and USB stdio are always disabled.

## Configuring the sensor

All in `src/config.h`, using the ST driver enums from `lsm6ds3_reg.h`:

- `IMU_XL_ODR`, `IMU_GY_ODR`: output data rates.
- `IMU_XL_FS`, `IMU_GY_FS`: full scale.
- `IMU_XL_AA_BW`: analog anti-aliasing bandwidth, or `IMU_XL_AA_AUTO`.
- `IMU_XL_FILTER`: `IMU_XL_FILTER_NONE`, `_LPF2` (`IMU_XL_LPF2_BW`) or `_HPF` (`IMU_XL_HPF_BW`).
- `IMU_GY_HPF_EN`, `IMU_GY_HPF_BW`: gyro high-pass filter.

Keep at least one ODR at or above 1 kHz to fill every HID frame.
