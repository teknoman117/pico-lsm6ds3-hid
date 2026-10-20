# AGENTS.md

Rules for working in this repo. See `docs/requirements.md` for what the firmware must do and
`docs/architecture.md` for structure.

## Project

- Raspberry Pi Pico (RP2040; must also build for RP2350 / Pico 2), Pico C/C++ SDK,
  CMake + Ninja. SDK/toolchain from `~/.pico-sdk`.
- Samples an LSM6DS3 or LSM6DS3TR-C (accel + gyro) and sends averaged windows as USB HID reports
  for the Linux generic HID driver, per `docs/requirements.md`.
- HID application is a Multi-axis Controller, not Gamepad/Joystick. Every field maps to a
  distinct evdev code with fuzz 0; keep the mapping table in `docs/architecture.md` current.
- The chip is identified at runtime by WHO_AM_I (0x69 LSM6DS3, 0x6A LSM6DS3TR-C).
- USB product name is exactly the detected chip: `LSM6DS3` or `LSM6DS3TR-C`.
- Report rate = gyro ODR / `IMU_AVERAGE_N` (~208 Hz). No minimum rate; the endpoint is polled
  every 1 ms (`bInterval` 1).
- Timestamps come from the Pico clock (`time_us_64()`), never the sensor.
- Sampling never waits on USB: one pending report, replaced (with the USB-busy flag) if unsent.
- The sampling path (IRQ handlers and everything they call) runs from RAM and must not call
  into flash: no SDK calls that are not inline, no struct copies/initialisers or copy loops that
  compile to `memcpy`/`memset`. Check with `arm-none-eabi-objdump -d` that every call from a
  `__not_in_flash_func` function targets a 0x2000xxxx address.

## Hardware (fixed)

- LSM6DS3 / LSM6DS3TR-C on SPI0: MOSI GP19, MISO GP16, SCK GP18, CS GP17, INT1 GP20.
- NeoPixel-compatible LED on GP23.

## Constraints

- No FreeRTOS. Run everything on core 0.
- stdio goes to the debug probe (RTT). Never UART or USB stdio.
- Only print in Debug builds. Use `DBG()` from `src/debug.h`, never bare `printf`.
- Keep the picotool reset interface (`pico_usb_reset`) in the USB descriptors.
- Prefer existing open-source drivers. ST's `lsm6ds3-pid` and `lsm6ds3tr-c-pid` live in `lib/`;
  do not edit them. Never include both ST headers in one translation unit.
- Sensor ODR, full scale and filter mode must stay easy to change in `src/config.h`.
- Use the closest setting the sensor supports. Rates are binary divisions of the sensor clock;
  keep timing integral rather than converting between nominal rates.

## Style

- Comments are concise and state facts. No essays, no reasoning or thinking in comments.
- Update `docs/architecture.md` when structure, mapping or config changes, and
  `docs/requirements.md` sections 8-9 when the HID mapping or a deviation changes. Keep it brief.
- Build must stay warning-free for `src/` with `-Wall -Wextra`.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # or Release
ninja -C build
picotool load -f -x build/pico-lsm6ds3.uf2
```
