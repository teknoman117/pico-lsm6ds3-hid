// SPDX-License-Identifier: MPL-2.0

#ifndef CONFIG_H
#define CONFIG_H

#include "imu.h"

// Pins
#define IMU_SPI          spi0
#define IMU_SPI_BAUD_HZ  (10 * 1000 * 1000)
#define PIN_IMU_MISO     16
#define PIN_IMU_CS       17
#define PIN_IMU_SCK      18
#define PIN_IMU_MOSI     19
#define PIN_IMU_INT1     20
#define PIN_NEOPIXEL     23

// --- Both sensors (imu.h enums) ---

// Output data rates. Gyro data-ready paces sampling; keep both equal so they share a window.
// Gyro up to 1.66 kHz on LSM6DS3 (clamped), 6.66 kHz on LSM6DS3TR-C.
#define IMU_XL_ODR       IMU_ODR_1K66HZ
#define IMU_GY_ODR       IMU_ODR_1K66HZ

// Full scale
#define IMU_XL_FS        IMU_XL_FS_4G
#define IMU_GY_FS        IMU_GY_FS_500DPS

// Accel digital filter: IMU_XL_FILTER_NONE, _LPF2 or _HPF. Averaging does the low-pass.
#define IMU_XL_FILTER    IMU_XL_FILTER_NONE
#define IMU_XL_LPF2_BW   IMU_XL_LPF2_ODR_DIV_50 // used with _LPF2
#define IMU_XL_HPF_BW    IMU_XL_HPF_ODR_DIV_100 // used with _HPF

// Samples summed per report (report rate = gyro ODR / IMU_AVERAGE_N).
#define IMU_AVERAGE_N       8
// Samples discarded after start while the sensors settle (168 at 1666 Hz = 100.8 ms).
#define IMU_SETTLE_SAMPLES  168

// --- LSM6DS3 only (lsm6ds3_reg.h enums) ---

// Accel analog anti-aliasing: 0 picks bandwidth from ODR, 1 uses IMU_DS3_XL_AA_BW.
#define IMU_DS3_XL_AA_MANUAL 1
#define IMU_DS3_XL_AA_BW     LSM6DS3_ANTI_ALIASING_100Hz

// Gyro high-pass filter. Off: it would remove slow real rotation along with the bias.
#define IMU_DS3_GY_HPF_EN    0
#define IMU_DS3_GY_HPF_BW    LSM6DS3_HP_CUT_OFF_32mHz4

// --- LSM6DS3TR-C only (lsm6ds3tr-c_reg.h enums) ---

// Accel analog bandwidth: LSM6DS3TR_C_XL_ANA_BW_1k5Hz or _400Hz (no 100 Hz option).
#define IMU_TRC_XL_ANALOG_BW      LSM6DS3TR_C_XL_ANA_BW_400Hz
// LPF2 input: 0 low latency, 1 low noise.
#define IMU_TRC_XL_LPF2_LOW_NOISE 0
// Gyro HPF/LPF1 chain, a lsm6ds3tr_c_lpf1_sel_g_t value. LP2_ONLY = both off.
#define IMU_TRC_GY_FILTER         LSM6DS3TR_C_LP2_ONLY

// USB. Raspberry Pi stdio VID/PID so picotool finds the reset interface.
// Product string is the detected sensor name, or USB_PRODUCT_DEFAULT if none was found.
#define USB_VID             0x2E8A
#if PICO_RP2350
#define USB_PID             0x0009
#else
#define USB_PID             0x000A
#endif
#define USB_PRODUCT_DEFAULT "LSM6DS3"
#define USB_HID_POLL_MS     1

// Status LED
#define NEOPIXEL_RGBW       0
#define NEOPIXEL_BRIGHTNESS 16 // 0..255

#endif
