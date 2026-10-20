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

// Output data rates (accel up to 6.66 kHz, gyro up to 1.66 kHz)
#define IMU_XL_ODR       LSM6DS3_XL_ODR_1k66Hz
#define IMU_GY_ODR       LSM6DS3_GY_ODR_1k66Hz

// Full scale
#define IMU_XL_FS        LSM6DS3_4g
#define IMU_GY_FS        LSM6DS3_2000dps

// Accel analog anti-aliasing: IMU_XL_AA_AUTO picks bandwidth from ODR,
// otherwise use a lsm6ds3_bw_xl_t value.
#define IMU_XL_AA_BW     IMU_XL_AA_AUTO

// Accel digital filter: IMU_XL_FILTER_NONE, _LPF2 or _HPF.
#define IMU_XL_FILTER    IMU_XL_FILTER_LPF2
#define IMU_XL_LPF2_BW   LSM6DS3_XL_LP_ODR_DIV_50  // used with _LPF2
#define IMU_XL_HPF_BW    LSM6DS3_XL_HP_ODR_DIV_100 // used with _HPF

// Gyro high-pass filter
#define IMU_GY_HPF_EN    0
#define IMU_GY_HPF_BW    LSM6DS3_HP_CUT_OFF_32mHz4

// USB. Raspberry Pi stdio VID/PID so picotool finds the reset interface.
#define USB_VID          0x2E8A
#define USB_PID          0x000A
#define USB_PRODUCT      "LSM6DS3"
#define USB_HID_POLL_MS  1

// Status LED
#define NEOPIXEL_RGBW       0
#define NEOPIXEL_BRIGHTNESS 16 // 0..255

#endif
