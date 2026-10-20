# TODO

## Raw capture mode for the vibration spectrum check

`docs/requirements.md` section 7 step 5 needs raw, un-averaged samples for a few seconds while
the robot drives: gyro at 1666 Hz and accelerometer at 6.66 kHz (`CTRL1_XL` ODR `1010`,
anti-aliasing 400 Hz). The normal 208 Hz report cannot carry this. Decide how to implement a
long capture mode.

- Data rate: gyro 1666 x 6 B + accel 6666 x 6 B is about 50 KB/s, close to the 64 KB/s limit of
  one full-speed interrupt endpoint.
- Option: capture to RAM, then download. About 4 s fits on the RP2040 (264 KB RAM), about 9 s
  on the Pico 2 (520 KB).
- Needs a host trigger (e.g. HID output report), sensor reconfiguration and restore, and a
  `tools/` script to fetch and plot both spectra.
