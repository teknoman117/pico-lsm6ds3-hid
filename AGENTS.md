# AGENTS.md

Rules for working in this repo. See `docs/architecture.md` for structure.

## Project

- Raspberry Pi Pico (RP2040), Pico C/C++ SDK, CMake + Ninja. SDK/toolchain from `~/.pico-sdk`.
- Exposes an LSM6DS3 or LSM6DS3TR-C (accel + gyro) as a USB HID gamepad for the Linux generic
  HID driver, following the kernel gamepad spec (`Documentation/input/gamepad.rst`).
- The chip is identified at runtime by WHO_AM_I (0x69 LSM6DS3, 0x6A LSM6DS3TR-C).
- Gamepad/USB product name is exactly the detected chip: `LSM6DS3` or `LSM6DS3TR-C`.
- HID rate at least 1000 Hz (full-speed `bInterval` 1).

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

## Style

- Comments are concise and state facts. No essays, no reasoning or thinking in comments.
- Update `docs/architecture.md` when structure, mapping or config changes. Keep it brief.
- Build must stay warning-free for `src/` with `-Wall -Wextra`.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # or Release
ninja -C build
picotool load -f -x build/pico-lsm6ds3.uf2
```
