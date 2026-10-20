// SPDX-License-Identifier: MPL-2.0

#ifndef IMU_H
#define IMU_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    IMU_MODEL_NONE,
    IMU_MODEL_LSM6DS3,    // WHO_AM_I 0x69
    IMU_MODEL_LSM6DS3TRC, // WHO_AM_I 0x6A
} imu_model_t;

// Values are the register codes, identical on both chips.
typedef enum {
    IMU_ODR_OFF = 0,
    IMU_ODR_12HZ5 = 1,
    IMU_ODR_26HZ = 2,
    IMU_ODR_52HZ = 3,
    IMU_ODR_104HZ = 4,
    IMU_ODR_208HZ = 5,
    IMU_ODR_416HZ = 6,
    IMU_ODR_833HZ = 7,
    IMU_ODR_1K66HZ = 8,
    IMU_ODR_3K33HZ = 9, // gyro: LSM6DS3TR-C only
    IMU_ODR_6K66HZ = 10, // gyro: LSM6DS3TR-C only
} imu_odr_t;

typedef enum {
    IMU_XL_FS_2G = 0,
    IMU_XL_FS_16G = 1,
    IMU_XL_FS_4G = 2,
    IMU_XL_FS_8G = 3,
} imu_xl_fs_t;

typedef enum {
    IMU_GY_FS_250DPS = 0,
    IMU_GY_FS_125DPS = 1,
    IMU_GY_FS_500DPS = 2,
    IMU_GY_FS_1000DPS = 4,
    IMU_GY_FS_2000DPS = 6,
} imu_gy_fs_t;

typedef enum {
    IMU_XL_FILTER_NONE,
    IMU_XL_FILTER_LPF2,
    IMU_XL_FILTER_HPF,
} imu_xl_filter_t;

typedef enum {
    IMU_XL_LPF2_ODR_DIV_50 = 0,
    IMU_XL_LPF2_ODR_DIV_100 = 1,
    IMU_XL_LPF2_ODR_DIV_9 = 2,
    IMU_XL_LPF2_ODR_DIV_400 = 3,
} imu_xl_lpf2_bw_t;

typedef enum {
    IMU_XL_HPF_ODR_DIV_4 = 0, // slope filter
    IMU_XL_HPF_ODR_DIV_100 = 1,
    IMU_XL_HPF_ODR_DIV_9 = 2,
    IMU_XL_HPF_ODR_DIV_400 = 3,
} imu_xl_hpf_bw_t;

// One averaging window of IMU_AVERAGE_N consecutive gyro data-ready samples.
typedef struct {
    int32_t gyro_sum[3];  // raw counts, sensor axes
    int32_t accel_sum[3]; // raw counts, sensor axes
    uint64_t time_us;     // mean of the samples' Pico time_us_64() (window midpoint)
    int16_t temperature;  // OUT_TEMP raw, last sample of the window
    bool sample_missed;   // a gap > 1.5 sample periods occurred in this window
} imu_window_t;

// Called in IRQ context for each completed window.
typedef void (*imu_window_cb_t)(const imu_window_t *window);

// Sets up SPI/GPIO, identifies the chip by WHO_AM_I, applies config.h, verifies it by
// read-back and starts sampling. Returns IMU_MODEL_NONE on failure. Call once.
imu_model_t imu_init(imu_window_cb_t cb);

const char *imu_model_name(imu_model_t model);

// Nominal time per window for the configured gyro ODR.
uint32_t imu_window_period_us(void);

#ifndef NDEBUG
// CPU cycles spent in sample accumulation (incl. building reports), Debug builds only.
typedef struct {
    uint32_t calls;
    uint32_t total_cycles;
    uint32_t max_cycles;
} imu_profile_t;

// Copies and resets the counters.
void imu_profile_take(imu_profile_t *out);
#endif

#endif
