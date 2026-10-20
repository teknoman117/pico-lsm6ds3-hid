#include <assert.h>

#include "config.h"
#include "debug.h"
#include "imu_bus.h"
#include "lsm6ds3_reg.h"

// Neutral imu.h codes must match the driver's register codes.
#define SAME(a, b) ((int)(a) == (int)(b))
static_assert(SAME(IMU_ODR_6K66HZ, LSM6DS3_XL_ODR_6k66Hz), "IMU_ODR_6K66HZ");
static_assert(SAME(IMU_ODR_1K66HZ, LSM6DS3_GY_ODR_1k66Hz), "IMU_ODR_1K66HZ");
static_assert(SAME(IMU_XL_FS_16G, LSM6DS3_16g), "IMU_XL_FS_16G");
static_assert(SAME(IMU_XL_FS_8G, LSM6DS3_8g), "IMU_XL_FS_8G");
static_assert(SAME(IMU_GY_FS_125DPS, LSM6DS3_125dps), "IMU_GY_FS_125DPS");
static_assert(SAME(IMU_GY_FS_2000DPS, LSM6DS3_2000dps), "IMU_GY_FS_2000DPS");
static_assert(SAME(IMU_XL_LPF2_ODR_DIV_400, LSM6DS3_XL_LP_ODR_DIV_400), "IMU_XL_LPF2_ODR_DIV_400");
static_assert(SAME(IMU_XL_HPF_ODR_DIV_400, LSM6DS3_XL_HP_ODR_DIV_400), "IMU_XL_HPF_ODR_DIV_400");

static const stmdev_ctx_t ctx = {
    .write_reg = imu_bus_write,
    .read_reg = imu_bus_read,
    .mdelay = imu_bus_delay_ms,
};

static int32_t config_xl_filters(void) {
    int32_t err = 0;

#if IMU_DS3_XL_AA_MANUAL
    lsm6ds3_ctrl4_c_t ctrl4;
    err |= lsm6ds3_read_reg(&ctx, LSM6DS3_CTRL4_C, (uint8_t *)&ctrl4, 1);
    ctrl4.xl_bw_scal_odr = 1;
    err |= lsm6ds3_write_reg(&ctx, LSM6DS3_CTRL4_C, (uint8_t *)&ctrl4, 1);
    err |= lsm6ds3_xl_filter_analog_set(&ctx, IMU_DS3_XL_AA_BW);
#endif

    switch (IMU_XL_FILTER) {
    case IMU_XL_FILTER_LPF2:
        err |= lsm6ds3_xl_lp2_bandwidth_set(&ctx, (lsm6ds3_lp_bw_t)IMU_XL_LPF2_BW);
        break;
    case IMU_XL_FILTER_HPF:
        err |= lsm6ds3_xl_hp_bandwidth_set(&ctx, (lsm6ds3_hp_bw_t)IMU_XL_HPF_BW);
        break;
    case IMU_XL_FILTER_NONE:
        break;
    }
    return err;
}

static int32_t config_gy_filters(void) {
#if IMU_DS3_GY_HPF_EN
    int32_t err = lsm6ds3_gy_hp_bandwidth_set(&ctx, IMU_DS3_GY_HPF_BW);
    lsm6ds3_ctrl7_g_t ctrl7;
    err |= lsm6ds3_read_reg(&ctx, LSM6DS3_CTRL7_G, (uint8_t *)&ctrl7, 1);
    ctrl7.hp_g_en = 1;
    err |= lsm6ds3_write_reg(&ctx, LSM6DS3_CTRL7_G, (uint8_t *)&ctrl7, 1);
    return err;
#else
    return 0;
#endif
}

bool imu_lsm6ds3_configure(void) {
    imu_odr_t gy_odr = IMU_GY_ODR;
    if (gy_odr > IMU_ODR_1K66HZ) {
        DBG("imu: LSM6DS3 gyro ODR limited to 1.66 kHz\n");
        gy_odr = IMU_ODR_1K66HZ;
    }

    int32_t err = 0;
    err |= lsm6ds3_i2c_interface_set(&ctx, LSM6DS3_I2C_DISABLE);
    err |= lsm6ds3_block_data_update_set(&ctx, PROPERTY_ENABLE);
    err |= lsm6ds3_auto_increment_set(&ctx, PROPERTY_ENABLE);

    err |= lsm6ds3_xl_full_scale_set(&ctx, (lsm6ds3_xl_fs_t)IMU_XL_FS);
    err |= lsm6ds3_gy_full_scale_set(&ctx, (lsm6ds3_fs_g_t)IMU_GY_FS);
    err |= config_xl_filters();
    err |= config_gy_filters();

    lsm6ds3_int1_route_t route = {
        .int1_drdy_xl = IMU_XL_ODR != IMU_ODR_OFF,
        .int1_drdy_g = gy_odr != IMU_ODR_OFF,
    };
    err |= lsm6ds3_pin_int1_route_set(&ctx, &route);

    err |= lsm6ds3_timestamp_res_set(&ctx, LSM6DS3_LSB_25us);
    err |= lsm6ds3_timestamp_set(&ctx, PROPERTY_ENABLE);
    err |= lsm6ds3_timestamp_rst_set(&ctx);

    err |= lsm6ds3_xl_data_rate_set(&ctx, (lsm6ds3_odr_xl_t)IMU_XL_ODR);
    err |= lsm6ds3_gy_data_rate_set(&ctx, (lsm6ds3_odr_g_t)gy_odr);
    return err == 0;
}
