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

#define IMU_TIMESTAMP_US_PER_LSB 25
#define IMU_TIMESTAMP_MASK       0x00FFFFFFu

// Raw sensor counts, sensor axis order (X, Y, Z).
typedef struct {
    int16_t gyro[3];
    int16_t accel[3];
    uint32_t timestamp; // sensor timer, 24-bit, IMU_TIMESTAMP_US_PER_LSB, wraps
} imu_sample_t;

// Sets up SPI/GPIO, identifies the chip by WHO_AM_I, applies config.h and starts the
// INT1 IRQ. Returns IMU_MODEL_NONE on failure. Safe to call again.
imu_model_t imu_init(void);

const char *imu_model_name(imu_model_t model);

// Samples are read in the INT1 (data-ready) IRQ and queued. Returns false when empty.
bool imu_pop(imu_sample_t *out);

// Samples dropped because the queue was full.
uint32_t imu_overflow_count(void);

#endif
