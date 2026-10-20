# Pico + LSM6DS3 IMU: firmware requirements

Copied from `manipulator/docs/pico-lsm6ds3-requirements.md`. Section 8 is filled in from this
firmware, and section 9 records how each requirement maps onto it.

A Raspberry Pi Pico 2 reads an LSM6DS3 (the original part, `WHO_AM_I` = `0x69`) and sends
averaged gyro and accelerometer samples to the robot's Pi 5 as a USB HID device. On the Pi, a
ROS 2 node turns the reports into `sensor_msgs/Imu`, and `robot_localization` fuses the gyro's
yaw rate with the wheel odometry. The gyro corrects the heading errors that wheel slip causes
(snags on surface transitions); slam_toolbox corrects the slow drift.

This document covers the Pico side only: the sensor configuration, sampling, averaging, timing,
and the HID report that the ROS node depends on.

**Register addresses and bit values below are from memory of the LSM6DS3 datasheet. Check each
one against the datasheet's register map before relying on it.**

## 1. Responsibilities

The Pico:

- configures the LSM6DS3 and checks that it is the expected part;
- reads every sample at 1666 Hz over SPI, driven by the sensor's data-ready interrupt;
- averages 8 samples into one report (about 208 Hz), keeping the extra resolution;
- timestamps each report with its own crystal clock;
- sends each report over USB HID without ever blocking the sampling.

The Pico does **not** correct gyro bias, scale to physical units, or estimate orientation. Bias
correction needs to know when the robot is stationary, which only the Pi knows (from the wheel
odometry). Unit scaling and fusion happen in ROS.

## 2. Sensor configuration

| Setting | Value | Register (verify) | Reason |
|---|---|---|---|
| Gyro sample rate | 1666 Hz | `CTRL2_G` (0x11) bits 7:4 = `1000` | Pushes the aliasing point (half the sample rate, 833 Hz) above servo and motor vibration; averaged down on the Pico |
| Gyro range | ±500 °/s (17.50 m°/s per count) | `CTRL2_G` bits 3:2 = `01` | Headroom for snags without clipping |
| Gyro high-pass filter | off | `CTRL7_G` (0x16) `HP_G_EN` = 0 | A high-pass would remove slow real rotation along with the bias |
| Gyro high-performance mode | on (default) | `CTRL7_G` `G_HM_MODE` = 0 | Lowest noise |
| Accel sample rate | 1666 Hz | `CTRL1_XL` (0x10) bits 7:4 = `1000` | Same as the gyro, so both share one averaging window |
| Accel range | ±4 g (0.122 mg per count) | `CTRL1_XL` bits 3:2 = `10` | Bumps and vibration spikes don't clip |
| Accel anti-aliasing filter | 100 Hz | `CTRL1_XL` bits 1:0 = `10`, **and** `CTRL4_C` (0x13) `XL_BW_SCAL_ODR` = 1 | Without the `CTRL4_C` bit the manual bandwidth is ignored |
| Accel LPF2 / high-pass | off | `CTRL8_XL` (0x17) = 0 | The Pico's averaging does this job |
| Accel high-performance mode | on (default) | `CTRL6_C` (0x15) `XL_HM_MODE` = 0 | Lowest noise |
| Block data update | on | `CTRL3_C` (0x12) `BDU` = 1 | High and low bytes always come from the same sample |
| Address auto-increment | on (default) | `CTRL3_C` `IF_INC` = 1 | One burst read covers all 12 output bytes |
| Data-ready interrupt | gyro data-ready on INT1 | `INT1_CTRL` (0x0D) `INT1_DRDY_G` = 1 | Wakes the Pico for each sample |
| FIFO | off (bypass) | | Not needed: the Pico reads every sample |

### Startup sequence

1. Read `WHO_AM_I` (0x0F). If it is not `0x69`, do not start sampling; report the error (see
   section 6).
2. Software reset (`CTRL3_C` `SW_RESET`), and wait for it to clear.
3. Write the configuration above, then **read every written register back** and compare. Treat
   a mismatch like a wrong `WHO_AM_I`.
4. Enable the INT1 interrupt on the Pico.
5. Discard the first ~100 ms of samples: the gyro and accelerometer need time to settle after
   being switched on.

## 3. Bus and wiring

- **SPI.** 12 bytes per sample at 1666 Hz; each read takes a small fraction of the 0.6 ms sample
  period. (I2C at 400 kHz needs about 0.35 ms per read, too tight at this rate: an I2C build
  would drop to 833 Hz with 4 samples per report, keeping the same report rate.)
- INT1 goes to a Pico GPIO with a rising-edge interrupt.
- SPI mode 3 (clock idles high), up to 10 MHz per the datasheet (verify); a few MHz is plenty.
- The pin assignments go in section 8.

## 4. Sampling and averaging

On each INT1 interrupt:

1. Record the Pico's microsecond timer (`time_us_64()`) for this sample.
2. Burst-read 12 bytes starting at `OUTX_L_G` (0x22): gyro X, Y, Z, then accel X, Y, Z, each a
   little-endian `int16`.
3. Add each axis to a **32-bit integer accumulator**, and add the timestamp to a timestamp
   accumulator.
4. After the 8th sample of a window, build one report from the accumulators (section 5), then
   reset them.

Requirements:

- **Average by count, not by time:** exactly 8 consecutive samples per report. Never skip or
  drop individual samples inside a window. The averaged rate times the window length must equal
  the rotation over that window.
- **Send sums, not means.** Averaging 8 samples gains 3 bits of resolution; dividing on the Pico
  and sending a 16-bit value would throw them away. The ROS node divides by 8.
- **The ISR must finish the read well within one sample period** (0.6 ms). If a sample is
  missed, so that the time since the previous sample is more than 1.5 times the nominal period,
  set the "sample missed" flag in the next report (section 5) instead of silently shortening the
  window.
- Sampling and averaging must never wait on USB.

## 5. Report contents

One report per window, about 208 Hz:

| Field | Type | Content |
|---|---|---|
| Gyro X, Y, Z | 32-bit signed | Sum of 8 raw samples (counts). Divide by 8, multiply by 17.50 m°/s. |
| Accel X, Y, Z | 32-bit signed | Sum of 8 raw samples (counts). Divide by 8, multiply by 0.122 mg. |
| Timestamp | 32-bit | Midpoint of the window: the mean of the 8 samples' Pico microsecond times, sent modulo 2³¹ (the ROS node unwraps it; it wraps every ~35.8 min). |
| Sequence | 32-bit | Increments by 1 per report, wrapping modulo 2³¹. Gaps tell the ROS node that reports were dropped. |
| Temperature | 16-bit signed (optional) | `OUT_TEMP` (0x20), read once per window. Gyro bias follows temperature; useful for later modelling. |
| Flags | bit field | Bit 0: a sample was missed in this window. Bit 1: a previous report could not be sent (USB busy). Other bits reserved, 0. |

Requirements:

- **Timestamps come from the Pico's clock, not the sensor's.** The LSM6DS3 runs on an internal
  oscillator whose real sample rate can be off from nominal by a noticeable percentage. The
  Pico's timer is crystal-derived and accurate to parts per million. The ROS node maps Pico
  time to ROS time.
- Axis directions are the sensor's own. The mounting rotation to the robot is in the URDF
  (`imu_link`), not in the firmware.

## 6. USB HID

- **Declare the application collection as a Multi-axis Controller** (Generic Desktop usage
  0x08), **not Gamepad or Joystick.** For Gamepad and Joystick devices, the kernel's
  `hid-input` driver sets each axis's `fuzz` to range/256, and the input core then drops changes
  smaller than about half of that. On these axes that would discard real gyro changes. A
  Multi-axis Controller gets no fuzz, and ROS's `joy_node` won't mistake it for the teleop
  joystick.
- Use **32-bit logical ranges** for the 32-bit fields in section 5.
- Map the fields to distinct axis codes, and record the resulting evdev codes in section 8 so
  the ROS node can match them. Suggested, following the kernel's convention for controller
  motion sensors: accel X/Y/Z to Generic Desktop X/Y/Z (`ABS_X/Y/Z`), gyro to Rx/Ry/Rz
  (`ABS_RX/RY/RZ`). The timestamp currently uses `ABS_MISC`; whatever codes the timestamp,
  sequence, temperature and flags land on, record them in section 8.
- Interrupt IN endpoint, full speed, `bInterval` = 1 ms, one report per window.
- **Never block or queue reports.** If the previous report has not been sent yet, replace it
  with the new one and set flag bit 1. The sequence counter still increments for every report
  built, so the ROS node sees the gap.
- Error state (wrong `WHO_AM_I`, configuration read-back mismatch): keep enumerating, but send
  reports with every data field 0 and a reserved error flag set (assign a bit in section 8), so
  the ROS node can report the fault instead of seeing a silent device.

The ROS node keeps the last value of every axis and assembles a sample at each `SYN_REPORT`,
because the input core sends no event for an axis whose value has not changed. Every report's
sequence (and timestamp) changes, so every report produces a `SYN_REPORT`.

## 7. Verification

1. `sudo evtest /dev/input/eventN`: the device name is as expected, every axis shows `Fuzz 0`
   and `Flat 0`, and reports arrive at about 208 Hz.
2. Stationary for 60 s: the sequence counter has no gaps; the timestamp steps are about 4.8 ms
   (8 / 1666 Hz); the gyro means are small and steady (the bias); the accel magnitude is about
   1 g (8197 counts per axis-g at ±4 g, about 65580 in the sums).
3. Rotate by hand about the sensor's Z axis, 90° each way: the summed gyro Z, scaled and
   integrated over the timestamps, comes to about ±90°.
4. Load the Pico's CPU (for example, log over a second USB interface) and confirm that no
   "sample missed" flags appear.
5. Vibration spectrum, once: for a few seconds with the robot driving over carpet and a surface
   transition, log the raw gyro at 1666 Hz (its maximum rate; bypassing the averaging) and look
   at its frequency spectrum up to 833 Hz (half the sample rate). The spectrum should fall off
   toward 833 Hz. Strong content near 833 Hz suggests vibration above it, folding down into the
   output. The gyro can't sample faster to show that directly, so as a proxy, also log the
   accelerometer at 6.66 kHz (`CTRL1_XL` ODR `1010`, anti-aliasing filter at its widest, 400 Hz)
   in the same run: it shows the robot's mechanical vibration frequencies above 833 Hz. Its
   filter rolls off gradually, so peaks there are visible but read lower than they are; look
   for where they are, not how big. If either shows
   strong vibration there, the fix is mechanical: a softer mount (foam or damping tape), not a
   different sample rate.

## 8. To fill in

| Item | Value |
|---|---|
| Bus | SPI0, mode 3, 10 MHz requested |
| Pico pins (SCK, MOSI, MISO, CS) | GP18, GP19, GP16, GP17 (CS GPIO-driven, active low) |
| INT1 GPIO | GP20 |
| USB device name (as under `/dev/input/by-id/`) | Input name `LSM6DS3` (`LSM6DS3TR-C` if that part is fitted). Link: `/dev/input/by-id/usb-2e8a_LSM6DS3_<serial>-event-if00`, serial = Pico flash unique ID (e.g. `E46788D3671F3E29`) |
| evdev codes: gyro X/Y/Z | `ABS_RX`, `ABS_RY`, `ABS_RZ` (0x03, 0x04, 0x05) |
| evdev codes: accel X/Y/Z | `ABS_X`, `ABS_Y`, `ABS_Z` (0x00, 0x01, 0x02) |
| evdev codes: timestamp, sequence, temperature, flags | `ABS_MISC` (0x28), `ABS_RUDDER` (0x07), `ABS_THROTTLE` (0x06), `ABS_WHEEL` (0x08) |
| Error flag bit | Bit 2 (0x04). Bit 0 sample missed, bit 1 USB busy. |
| Sensor mounting (sensor axes relative to `base_footprint`) | Not determined by the firmware; set in the URDF (`imu_link`). |

## 9. Implementation notes

How the requirements map onto this firmware (`docs/architecture.md` has the design).

- **Register table:** every address and bit in section 2 matches ST's `lsm6ds3-pid` driver. The
  settings are applied through the driver's calls from `src/config.h`, not hand-built values.
- **Read-back (section 2 step 3):** the bus layer records every byte the driver writes during
  configuration and reads each one back.
- **Settling (step 5):** 168 samples discarded (100.8 ms at 1666 Hz), a whole number of windows.
- **LSM6DS3TR-C:** also accepted (`WHO_AM_I` 0x6A) and named `LSM6DS3TR-C` over USB. It has no
  100 Hz anti-aliasing setting; it uses its 400 Hz analog bandwidth. Temperature scale differs
  (TR-C 256 LSB/°C, LSM6DS3 16 LSB/°C; 0 = 25 °C on both). Any other `WHO_AM_I` is an error.
- **INT1 trigger (section 3):** level-high, not rising-edge. DRDY is latched until the outputs
  are read and the LSM6DS3 has no pulsed DRDY mode, so one missed edge would stop sampling for
  good. The sample time is taken at IRQ entry either way.
- **Read time (section 4):** one 15-byte SPI DMA burst per sample, about 15 us of the 600 us
  period; the CPU does not wait on SPI.
- **Temperature (section 5):** read in the same SPI burst as the outputs (from `OUT_TEMP_L`
  0x20, 14 bytes) for every sample; the report carries the last sample's value.
- **Report rate:** 1666.67 Hz / 8 = 208.33 Hz nominal. Measured on one LSM6DS3: 210.3 Hz
  (timestamp step 4756 us), i.e. the sensor oscillator runs ~0.9% fast. This is what the Pico
  timestamps are for.
- **Error reports (section 6):** sent every 4800 us with gyro, accel and temperature 0 and bit 2
  set. Sequence and timestamp keep running so every error report still produces a `SYN_REPORT`.
  The sensor is probed once at boot; an error latches until reset.
- **joy_node (section 6):** a Multi-axis Controller alone does not stop udev classifying a
  device as a joystick. This device is safe because it has no keys and has `ABS_X/Y/Z`:
  systemd's `input_id` tags it `ID_INPUT_ACCELEROMETER` instead, so SDL-based `joy_node`
  ignores it. The kernel's `joydev` still creates `/dev/input/jsN` for it, so `joy_linux`
  pointed at a `js` path could open it.
- **Target:** developed on an RP2040 Pico; also builds for the Pico 2 (`-DPICO_BOARD=pico2`).

### Verification results (RP2040 + LSM6DS3, stationary)

| Step | Result |
|---|---|
| 1 | Name `LSM6DS3`; all 10 axes fuzz 0, flat 0; 210.3 reports/s |
| 2 (60 s) | 0 sequence gaps; timestamp step 4755-4758 us; gyro bias 1.5 / -8.5 / 0.3 dps (within the ±10 dps zero-rate spec); \|a\| 1.035 g (sum ~67843); 0 flags |
| 3 | Not run (needs a hand rotation) |
| 4 | Run at 833 Hz / 4 (same sampling path): 3 ms busy loop in every main-loop pass gave 0 sample-missed flags; 882 replaced reports, each with the USB-busy flag and a matching sequence gap |
| 5 | TODO, see `docs/TODO.md` |
| Error state | Forced WHO_AM_I failure: 208.5 reports/s, all with bit 2, data 0, no sequence gaps |

`tools/verify_evdev.py` runs steps 1-2.
