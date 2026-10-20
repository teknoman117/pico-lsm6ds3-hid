# Architecture

RP2040 firmware (also builds for RP2350 / Pico 2) that samples an LSM6DS3 or LSM6DS3TR-C at
1666 Hz, sums 8 samples per window, timestamps with the Pico's crystal clock and sends one USB HID
report per window (~208 Hz). Requirements: `docs/requirements.md`. Bare-metal, core 0, no RTOS.

## Layout

| Path | Role |
|---|---|
| `src/config.h` | All tunables: pins, ODR, full scale, filters, averaging, USB IDs, LED. Edit here first. |
| `src/main.c` | Init, main loop: USB task, report sending, error-state reports, IMU retry. |
| `src/imu.[ch]` | Public IMU API, chip-neutral config enums, SPI bus, WHO_AM_I probe, soft reset, config read-back, INT1 IRQ + DMA sampling, averaging windows. |
| `src/imu_bus.h` | Private: bus callbacks for the ST drivers, per-chip configure functions. |
| `src/imu_lsm6ds3.c` | LSM6DS3 configuration via `lsm6ds3-pid`. |
| `src/imu_lsm6ds3trc.c` | LSM6DS3TR-C configuration via `lsm6ds3tr-c-pid`. |
| `src/report.[ch]` | HID report descriptor and layout, sequence/flags, single pending-report slot, HID callbacks. |
| `src/usb_descriptors.[ch]` | Device/config/string descriptors (HID + picotool reset interface), runtime product string. |
| `src/tusb_config.h` | TinyUSB device config. |
| `src/status_led.[ch]`, `src/ws2812.pio` | NeoPixel status via PIO. |
| `src/debug.h` | `DBG()` printf macro, compiled out when `NDEBUG` is defined. |
| `docs/TODO.md` | Open work: raw capture mode for requirements step 5. |
| `tools/verify_evdev.py` | Requirements section 7 steps 1-2 against the live device. |
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

## Startup

`imu_init()` (blocking SPI; sampling IRQs disabled):

1. WHO_AM_I: 0x69 is LSM6DS3, 0x6A is LSM6DS3TR-C, anything else is an error.
2. Soft reset (CTRL3_C SW_RESET, same on both), wait for it to clear.
3. Chip file applies `config.h` through ST driver calls: I2C off, BDU, auto-increment,
   high-performance modes, full scale, filters, gyro data-ready on INT1, ODR last.
4. Every byte the driver wrote is recorded by the bus layer and read back; any mismatch is an error.
5. INT1 IRQ enabled. The first `IMU_SETTLE_SAMPLES` (168, 100.8 ms) are discarded.

Errors leave `imu_init()` returning `IMU_MODEL_NONE`; it is retried every second.

## Sampling

- INT1 level-high IRQ: records `time_us_64()`, disables itself, drops CS, starts a TX/RX DMA pair
  for one 14-byte burst from `OUT_TEMP_L` (0x20): temperature, gyro XYZ, accel XYZ.
  INT1 is level- not edge-triggered: DRDY is latched until read and neither chip has a pulsed
  DRDY mode, so a missed edge would stop sampling for good.
- DMA RX-complete IRQ: raises CS, accumulates the sample, re-enables INT1.
- Averaging by count: `IMU_AVERAGE_N` (8) consecutive samples per window, never skipped. Axes are
  summed in int32 (the 3 extra bits are kept); the window time is the mean of the sample times.
- A gap > 1.5 sample periods between samples flags the window "sample missed".
- Sample periods are exact: ODRs are binary divisions of 6.667 kHz, so period = 150 us << (10 - code).
- Window complete: callback into `report_submit_window()` (IRQ context).
- Only the IRQ/DMA path touches SPI while sampling; re-init disables it and waits first.

## Reports

`report.c` holds one pending report. Building a report (IRQ for windows, main for errors)
increments the sequence and, if the slot still holds an unsent report, replaces it and sets
`USB_BUSY`. `report_task()` in the main loop sends the slot when the endpoint is free. Sampling
never waits on USB.

Error state (no/unknown sensor, reset timeout, read-back mismatch): the main loop builds reports
every window period (4800 us) with all data 0 and `ERROR` set; sequence and timestamp still run.

## HID mapping

Application collection Generic Desktop / Multi-axis Controller (not Game Pad/Joystick, which get
fuzz = range/256 from `hid-input`). `hid-input` ignores duplicate codes, so each field uses a usage
with a distinct mapping.

| Offset | Type | Field | Usage | evdev |
|---|---|---|---|---|
| 0 | int32 ×3 | gyro X, Y, Z sum | GD Rx, Ry, Rz | `ABS_RX`, `ABS_RY`, `ABS_RZ` |
| 12 | int32 ×3 | accel X, Y, Z sum | GD X, Y, Z | `ABS_X`, `ABS_Y`, `ABS_Z` |
| 24 | uint32 | timestamp, us mod 2^31 | Sensor 0x20:0x0529 Time Stamp | `ABS_MISC` |
| 28 | uint32 | sequence mod 2^31 | GD Dial | `ABS_RUDDER` |
| 32 | int16 | temperature (OUT_TEMP raw) | GD Slider | `ABS_THROTTLE` |
| 34 | uint8 | flags | GD Wheel | `ABS_WHEEL` |

35 bytes, little endian, no report ID. Flags: bit 0 sample missed, bit 1 USB busy, bit 2 error.
The Sensor page appears only as a field, never as a collection, so `hid-core` does not group the
device as a sensor hub. All axes have fuzz 0 and flat 0.

With no keys and `ABS_X/Y/Z`, udev's `input_id` tags the device `ID_INPUT_ACCELEROMETER`, not
`ID_INPUT_JOYSTICK`, so SDL-based joystick code ignores it. `joydev` still creates `/dev/input/js*`.

## USB

- VID:PID `2E8A:000A` (RP2040) / `2E8A:0009` (RP2350) so `picotool` finds the device without `--vid/--pid`.
- No manufacturer string, so the kernel device name is exactly the product string.
- Product string is the detected sensor: `LSM6DS3` or `LSM6DS3TR-C`; `USB_PRODUCT_DEFAULT`
  (`LSM6DS3`) if none was found at boot. A later detection with a different name re-enumerates.
- Serial number is the flash unique ID.
- Interrupt IN endpoint, 64 bytes, `bInterval` 1 ms.
- Interface 1 is the SDK `pico_usb_reset` vendor interface (`picotool reboot`, `picotool load -f`).

## Status LED

| Colour | State |
|---|---|
| Red | IMU error (reports carry the error flag) |
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

- `IMU_XL_ODR`, `IMU_GY_ODR`: output data rates; keep equal. Gyro data-ready paces sampling.
  LSM6DS3 gyro is clamped to 1.66 kHz.
- `IMU_XL_FS`, `IMU_GY_FS`: full scale.
- `IMU_XL_FILTER`: `IMU_XL_FILTER_NONE`, `_LPF2` (`IMU_XL_LPF2_BW`) or `_HPF` (`IMU_XL_HPF_BW`).
- `IMU_AVERAGE_N`: samples per report. `IMU_SETTLE_SAMPLES`: samples discarded at start
  (a multiple of `IMU_AVERAGE_N`).

LSM6DS3 only (`lsm6ds3_reg.h` enums): `IMU_DS3_XL_AA_MANUAL` / `IMU_DS3_XL_AA_BW` (analog
anti-aliasing), `IMU_DS3_GY_HPF_EN` / `IMU_DS3_GY_HPF_BW` (gyro high-pass).

LSM6DS3TR-C only (`lsm6ds3tr-c_reg.h` enums): `IMU_TRC_XL_ANALOG_BW`, `IMU_TRC_XL_LPF2_LOW_NOISE`,
`IMU_TRC_GY_FILTER` (gyro HPF/LPF1 chain).
