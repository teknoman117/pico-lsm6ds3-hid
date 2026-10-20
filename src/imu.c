#include "imu.h"

#include "config.h"
#include "debug.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/time.h"

#define SPI_READ_BIT   0x80
#define BOOT_TIME_MS   20
#define RESET_TIMEOUT_MS 50

static int32_t spi_write_reg(void *handle, uint8_t reg, const uint8_t *buf, uint16_t len) {
    (void)handle;
    uint8_t addr = reg & ~SPI_READ_BIT;
    gpio_put(PIN_IMU_CS, 0);
    spi_write_blocking(IMU_SPI, &addr, 1);
    spi_write_blocking(IMU_SPI, buf, len);
    gpio_put(PIN_IMU_CS, 1);
    return 0;
}

static int32_t spi_read_reg(void *handle, uint8_t reg, uint8_t *buf, uint16_t len) {
    (void)handle;
    uint8_t addr = reg | SPI_READ_BIT;
    gpio_put(PIN_IMU_CS, 0);
    spi_write_blocking(IMU_SPI, &addr, 1);
    spi_read_blocking(IMU_SPI, 0, buf, len);
    gpio_put(PIN_IMU_CS, 1);
    return 0;
}

static void delay_ms(uint32_t ms) {
    sleep_ms(ms);
}

static const stmdev_ctx_t ctx = {
    .write_reg = spi_write_reg,
    .read_reg = spi_read_reg,
    .mdelay = delay_ms,
    .handle = NULL,
};

static void bus_init(void) {
    spi_init(IMU_SPI, IMU_SPI_BAUD_HZ);
    spi_set_format(IMU_SPI, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_set_function(PIN_IMU_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_IMU_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_IMU_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_IMU_CS);
    gpio_put(PIN_IMU_CS, 1);
    gpio_set_dir(PIN_IMU_CS, GPIO_OUT);

    gpio_init(PIN_IMU_INT1);
    gpio_set_dir(PIN_IMU_INT1, GPIO_IN);
    gpio_pull_down(PIN_IMU_INT1);
}

static int32_t soft_reset(void) {
    int32_t err = lsm6ds3_reset_set(&ctx, PROPERTY_ENABLE);
    absolute_time_t deadline = make_timeout_time_ms(RESET_TIMEOUT_MS);
    uint8_t busy = 1;
    while (!err && busy) {
        if (time_reached(deadline)) {
            return -1;
        }
        err = lsm6ds3_reset_get(&ctx, &busy);
    }
    return err;
}

static int32_t config_xl_filters(const imu_config_t *cfg) {
    int32_t err = 0;

    if (cfg->xl_aa_bw != IMU_XL_AA_AUTO) {
        lsm6ds3_ctrl4_c_t ctrl4;
        err |= lsm6ds3_read_reg(&ctx, LSM6DS3_CTRL4_C, (uint8_t *)&ctrl4, 1);
        ctrl4.xl_bw_scal_odr = 1;
        err |= lsm6ds3_write_reg(&ctx, LSM6DS3_CTRL4_C, (uint8_t *)&ctrl4, 1);
        err |= lsm6ds3_xl_filter_analog_set(&ctx, (lsm6ds3_bw_xl_t)cfg->xl_aa_bw);
    }

    switch (cfg->xl_filter) {
    case IMU_XL_FILTER_LPF2:
        err |= lsm6ds3_xl_lp2_bandwidth_set(&ctx, cfg->xl_lpf2_bw);
        break;
    case IMU_XL_FILTER_HPF:
        err |= lsm6ds3_xl_hp_bandwidth_set(&ctx, cfg->xl_hpf_bw);
        break;
    case IMU_XL_FILTER_NONE:
        break;
    }
    return err;
}

static int32_t config_gy_filters(const imu_config_t *cfg) {
    if (!cfg->gy_hpf_en) {
        return 0;
    }
    int32_t err = lsm6ds3_gy_hp_bandwidth_set(&ctx, cfg->gy_hpf_bw);
    lsm6ds3_ctrl7_g_t ctrl7;
    err |= lsm6ds3_read_reg(&ctx, LSM6DS3_CTRL7_G, (uint8_t *)&ctrl7, 1);
    ctrl7.hp_g_en = 1;
    err |= lsm6ds3_write_reg(&ctx, LSM6DS3_CTRL7_G, (uint8_t *)&ctrl7, 1);
    return err;
}

bool imu_init(const imu_config_t *cfg) {
    bus_init();
    sleep_ms(BOOT_TIME_MS);

    uint8_t id = 0;
    if (lsm6ds3_device_id_get(&ctx, &id) || id != LSM6DS3_ID) {
        DBG("imu: WHO_AM_I 0x%02x, expected 0x%02x\n", id, LSM6DS3_ID);
        return false;
    }
    if (soft_reset()) {
        DBG("imu: reset timeout\n");
        return false;
    }

    int32_t err = 0;
    err |= lsm6ds3_i2c_interface_set(&ctx, LSM6DS3_I2C_DISABLE);
    err |= lsm6ds3_block_data_update_set(&ctx, PROPERTY_ENABLE);
    err |= lsm6ds3_auto_increment_set(&ctx, PROPERTY_ENABLE);

    err |= lsm6ds3_xl_full_scale_set(&ctx, cfg->xl_fs);
    err |= lsm6ds3_gy_full_scale_set(&ctx, cfg->gy_fs);
    err |= config_xl_filters(cfg);
    err |= config_gy_filters(cfg);

    lsm6ds3_int1_route_t route = {
        .int1_drdy_xl = cfg->xl_odr != LSM6DS3_XL_ODR_OFF,
        .int1_drdy_g = cfg->gy_odr != LSM6DS3_GY_ODR_OFF,
    };
    err |= lsm6ds3_pin_int1_route_set(&ctx, &route);

    err |= lsm6ds3_xl_data_rate_set(&ctx, cfg->xl_odr);
    err |= lsm6ds3_gy_data_rate_set(&ctx, cfg->gy_odr);

    if (err) {
        DBG("imu: config failed\n");
        return false;
    }
    DBG("imu: ready\n");
    return true;
}

bool imu_data_ready(void) {
    return gpio_get(PIN_IMU_INT1);
}

bool imu_read(imu_sample_t *out) {
    // OUTX_L_G..OUTZ_H_XL: gyro XYZ then accel XYZ, little endian.
    uint8_t buf[12];
    if (lsm6ds3_read_reg(&ctx, LSM6DS3_OUTX_L_G, buf, sizeof(buf))) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        out->gyro[i] = (int16_t)(buf[2 * i] | (buf[2 * i + 1] << 8));
        out->accel[i] = (int16_t)(buf[6 + 2 * i] | (buf[6 + 2 * i + 1] << 8));
    }
    return true;
}
