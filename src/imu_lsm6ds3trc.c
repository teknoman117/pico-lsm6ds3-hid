#include <assert.h>

#include "config.h"
#include "imu_bus.h"
#include "lsm6ds3tr-c_reg.h"

#define TIMESTAMP_RESET 0xAA

// Neutral imu.h codes must match the driver's register codes.
#define SAME(a, b) ((int)(a) == (int)(b))
static_assert(SAME(IMU_ODR_6K66HZ, LSM6DS3TR_C_XL_ODR_6k66Hz), "IMU_ODR_6K66HZ");
static_assert(SAME(IMU_ODR_6K66HZ, LSM6DS3TR_C_GY_ODR_6k66Hz), "IMU_ODR_6K66HZ");
static_assert(SAME(IMU_XL_FS_16G, LSM6DS3TR_C_16g), "IMU_XL_FS_16G");
static_assert(SAME(IMU_XL_FS_8G, LSM6DS3TR_C_8g), "IMU_XL_FS_8G");
static_assert(SAME(IMU_GY_FS_125DPS, LSM6DS3TR_C_125dps), "IMU_GY_FS_125DPS");
static_assert(SAME(IMU_GY_FS_2000DPS, LSM6DS3TR_C_2000dps), "IMU_GY_FS_2000DPS");
static_assert(SAME(IMU_XL_LPF2_ODR_DIV_400, LSM6DS3TR_C_XL_LOW_LAT_LP_ODR_DIV_400), "IMU_XL_LPF2_ODR_DIV_400");
static_assert(SAME(IMU_XL_HPF_ODR_DIV_400, LSM6DS3TR_C_XL_HP_ODR_DIV_400), "IMU_XL_HPF_ODR_DIV_400");

static const stmdev_ctx_t ctx = {
    .write_reg = imu_bus_write,
    .read_reg = imu_bus_read,
    .mdelay = imu_bus_delay_ms,
};

static int32_t config_xl_filters(void) {
    int32_t err = lsm6ds3tr_c_xl_filter_analog_set(&ctx, IMU_TRC_XL_ANALOG_BW);

    switch (IMU_XL_FILTER) {
    case IMU_XL_FILTER_LPF2: {
        // Bit 4 selects the low-noise (INPUT_COMPOSITE) variant.
        uint8_t bw = IMU_XL_LPF2_BW | (IMU_TRC_XL_LPF2_LOW_NOISE ? 0x10 : 0x00);
        err |= lsm6ds3tr_c_xl_lp2_bandwidth_set(&ctx, (lsm6ds3tr_c_input_composite_t)bw);
        break;
    }
    case IMU_XL_FILTER_HPF:
        err |= lsm6ds3tr_c_xl_hp_bandwidth_set(&ctx, (lsm6ds3tr_c_hpcf_xl_t)IMU_XL_HPF_BW);
        break;
    case IMU_XL_FILTER_NONE:
        break;
    }
    return err;
}

bool imu_lsm6ds3trc_configure(void) {
    int32_t err = 0;
    err |= lsm6ds3tr_c_i2c_interface_set(&ctx, LSM6DS3TR_C_I2C_DISABLE);
    err |= lsm6ds3tr_c_block_data_update_set(&ctx, PROPERTY_ENABLE);
    err |= lsm6ds3tr_c_auto_increment_set(&ctx, PROPERTY_ENABLE);

    err |= lsm6ds3tr_c_xl_full_scale_set(&ctx, (lsm6ds3tr_c_fs_xl_t)IMU_XL_FS);
    err |= lsm6ds3tr_c_gy_full_scale_set(&ctx, (lsm6ds3tr_c_fs_g_t)IMU_GY_FS);
    err |= config_xl_filters();
    err |= lsm6ds3tr_c_gy_band_pass_set(&ctx, IMU_TRC_GY_FILTER);

    lsm6ds3tr_c_int1_route_t route = {
        .int1_drdy_xl = IMU_XL_ODR != IMU_ODR_OFF,
        .int1_drdy_g = IMU_GY_ODR != IMU_ODR_OFF,
    };
    err |= lsm6ds3tr_c_pin_int1_route_set(&ctx, route);

    err |= lsm6ds3tr_c_timestamp_res_set(&ctx, LSM6DS3TR_C_LSB_25us);
    err |= lsm6ds3tr_c_timestamp_set(&ctx, PROPERTY_ENABLE);

    err |= lsm6ds3tr_c_xl_data_rate_set(&ctx, (lsm6ds3tr_c_odr_xl_t)IMU_XL_ODR);
    err |= lsm6ds3tr_c_gy_data_rate_set(&ctx, (lsm6ds3tr_c_odr_g_t)IMU_GY_ODR);
    uint8_t rst = TIMESTAMP_RESET;
    err |= lsm6ds3tr_c_write_reg(&ctx, LSM6DS3TR_C_TIMESTAMP2_REG, &rst, 1);
    return err == 0;
}
