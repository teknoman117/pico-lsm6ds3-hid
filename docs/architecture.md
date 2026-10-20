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

SPI mode 3, 10 MHz requested.

## Data flow

1. `imu_init()` checks WHO_AM_I (0x69), soft-resets, disables I2C, enables BDU and auto-increment,
   applies full scale and filters, routes accel/gyro data-ready to INT1, then sets ODR last.
2. Main loop polls INT1 level. When high, one 12-byte burst reads gyro XYZ then accel XYZ.
3. When the HID endpoint is free, the latest sample is sent. Interrupt IN endpoint, `bInterval` 1 ms
   (1000 Hz, the full-speed maximum).
4. If the IMU is missing, init is retried every second; USB stays up so picotool still works.

## HID mapping

Application collection is Generic Desktop / Game Pad, so the kernel's `hid-generic` + `hid-input`
produce codes per `Documentation/input/gamepad.rst`:

| Report field | Source | evdev code |
|---|---|---|
| X, Y, Z (int16) | accel X, Y, Z raw | `ABS_X`, `ABS_Y`, `ABS_Z` |
| Rx, Ry, Rz (int16) | gyro X, Y, Z raw | `ABS_RX`, `ABS_RY`, `ABS_RZ` |
| Button 1, 2 | unused, always 0 | `BTN_SOUTH`, `BTN_EAST` |

The two buttons exist so the device advertises `BTN_GAMEPAD`, which the spec requires.
Axis range is the full int16 range, so scale follows `IMU_XL_FS` / `IMU_GY_FS`.
`hid-input` sets fuzz 255 / flat 4095 on gamepad axes; userspace can clear them with `EVIOCSABS`.

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
