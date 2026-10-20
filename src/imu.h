#ifndef IMU_H
#define IMU_H

#include <stdbool.h>
#include <stdint.h>
#include "lsm6ds3_reg.h"

#define IMU_XL_AA_AUTO (-1)

typedef enum {
    IMU_XL_FILTER_NONE,
    IMU_XL_FILTER_LPF2,
    IMU_XL_FILTER_HPF,
} imu_xl_filter_t;

typedef struct {
    lsm6ds3_odr_xl_t xl_odr;
    lsm6ds3_xl_fs_t xl_fs;
    int xl_aa_bw; // lsm6ds3_bw_xl_t or IMU_XL_AA_AUTO
    imu_xl_filter_t xl_filter;
    lsm6ds3_lp_bw_t xl_lpf2_bw;
    lsm6ds3_hp_bw_t xl_hpf_bw;

    lsm6ds3_odr_g_t gy_odr;
    lsm6ds3_fs_g_t gy_fs;
    bool gy_hpf_en;
    lsm6ds3_hpcf_g_t gy_hpf_bw;
} imu_config_t;

// Raw sensor counts, sensor axis order (X, Y, Z).
typedef struct {
    int16_t gyro[3];
    int16_t accel[3];
} imu_sample_t;

// Sets up SPI/GPIO, verifies WHO_AM_I and applies cfg. Safe to call again on failure.
bool imu_init(const imu_config_t *cfg);

// INT1 level; high while unread samples are pending.
bool imu_data_ready(void);

bool imu_read(imu_sample_t *out);

#endif
