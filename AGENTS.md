# AGENTS.md

Rules for working in this repo. See `docs/architecture.md` for structure.

## Project

- Raspberry Pi Pico (RP2040), Pico C/C++ SDK, CMake + Ninja. SDK/toolchain from `~/.pico-sdk`.
- Exposes an LSM6DS3 (accel + gyro) as a USB HID gamepad for the Linux generic HID driver,
  following the kernel gamepad spec (`Documentation/input/gamepad.rst`).
- Gamepad/USB product name is exactly `LSM6DS3`.
- HID rate at least 1000 Hz (full-speed `bInterval` 1).

## Hardware (fixed)

- LSM6DS3 on SPI0: MOSI GP19, MISO GP16, SCK GP18, CS GP17, INT1 GP20.
- NeoPixel-compatible LED on GP23.

## Constraints

- No FreeRTOS. Run everything on core 0.
- stdio goes to the debug probe (RTT). Never UART or USB stdio.
- Only print in Debug builds. Use `DBG()` from `src/debug.h`, never bare `printf`.
- Keep the picotool reset interface (`pico_usb_reset`) in the USB descriptors.
- Prefer existing open-source drivers. The LSM6DS3 uses ST's `lsm6ds3-pid` in `lib/`; do not edit it.
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
