# Architecture

RP2040 firmware that streams LSM6DS3 or LSM6DS3TR-C accel/gyro data as a USB HID gamepad named
after the detected sensor (`LSM6DS3` or `LSM6DS3TR-C`).
Bare-metal, single loop on core 0. No RTOS.

## Layout

| Path | Role |
|---|---|
| `src/config.h` | All tunables: pins, ODR, full scale, filters, USB IDs, LED. Edit here first. |
| `src/main.c` | Init, then the main loop. Owns no hardware directly. |
| `src/imu.[ch]` | Public IMU API, chip-neutral config enums, SPI bus, WHO_AM_I probe, soft reset, sample/timestamp reads. |
| `src/imu_bus.h` | Private: bus callbacks for the ST drivers, per-chip configure functions. |
| `src/imu_lsm6ds3.c` | LSM6DS3 configuration via `lsm6ds3-pid`. |
| `src/imu_lsm6ds3trc.c` | LSM6DS3TR-C configuration via `lsm6ds3tr-c-pid`. |
| `src/gamepad.[ch]` | HID report descriptor, report layout, HID callbacks. |
| `src/usb_descriptors.[ch]` | Device/config/string descriptors (HID + picotool reset interface), runtime product string. |
| `src/tusb_config.h` | TinyUSB device config. |
| `src/status_led.[ch]`, `src/ws2812.pio` | NeoPixel status via PIO. |
| `src/debug.h` | `DBG()` printf macro, compiled out when `NDEBUG` is defined. |
| `tools/uhid_check.py` | Host-side check of the HID mapping via `/dev/uhid`, no hardware needed. |
| `lib/lsm6ds3-pid/`, `lib/lsm6ds3tr-c-pid/` | ST's platform-independent drivers, vendored unmodified. See each `UPSTREAM.md`. |

The two ST headers define incompatible `stmdev_ctx_t` under the same include guard, so each is
included only by its own chip file. Neither leaks into `imu.h` or `config.h`.

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

1. Before USB starts, `imu_init()` reads WHO_AM_I: 0x69 is LSM6DS3, 0x6A is LSM6DS3TR-C, anything
   else is "no sensor". It soft-resets (CTRL3_C SW_RESET, same on both), then the chip file
   disables I2C, enables BDU and auto-increment, applies full scale and filters, routes accel/gyro
   data-ready to INT1, enables and zeroes the 25 us timestamp counter, and sets ODR last.
   Output and timestamp registers are at the same addresses on both chips, so reads are shared.
2. Main loop polls INT1 level. When high, one 12-byte burst reads gyro XYZ then accel XYZ,
   followed by the 24-bit timestamp (read twice to reject a carry between bytes).
3. When the HID endpoint is free, the latest sample is sent. Interrupt IN endpoint, `bInterval` 1 ms
   (1000 Hz, the full-speed maximum).
4. If the IMU is missing, init is retried every second; USB stays up so picotool still works.
   If a later detection changes the product name, the device disconnects and re-enumerates.

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
- Product string is the detected sensor: `LSM6DS3` or `LSM6DS3TR-C`; `USB_PRODUCT_DEFAULT`
  (`LSM6DS3`) if none was found at boot.
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

All in `src/config.h`.

Both sensors, using the chip-neutral enums in `imu.h` (values are the shared register codes,
checked against each ST driver with `static_assert`):

- `IMU_XL_ODR`, `IMU_GY_ODR`: output data rates. LSM6DS3 gyro is clamped to 1.66 kHz.
- `IMU_XL_FS`, `IMU_GY_FS`: full scale.
- `IMU_XL_FILTER`: `IMU_XL_FILTER_NONE`, `_LPF2` (`IMU_XL_LPF2_BW`) or `_HPF` (`IMU_XL_HPF_BW`).

LSM6DS3 only (`lsm6ds3_reg.h` enums): `IMU_DS3_XL_AA_MANUAL` / `IMU_DS3_XL_AA_BW` (analog
anti-aliasing), `IMU_DS3_GY_HPF_EN` / `IMU_DS3_GY_HPF_BW` (gyro high-pass).

LSM6DS3TR-C only (`lsm6ds3tr-c_reg.h` enums): `IMU_TRC_XL_ANALOG_BW`, `IMU_TRC_XL_LPF2_LOW_NOISE`,
`IMU_TRC_GY_FILTER` (gyro HPF/LPF1 chain).

Keep at least one ODR at or above 1 kHz to fill every HID frame.
