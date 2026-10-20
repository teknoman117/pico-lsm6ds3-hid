#include "imu.h"

#include "config.h"
#include "debug.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "imu_bus.h"
#include "pico/time.h"

#define SPI_READ_BIT     0x80
#define BOOT_TIME_MS     20
#define RESET_TIMEOUT_MS 50

// Registers shared by LSM6DS3 and LSM6DS3TR-C.
#define REG_WHO_AM_I     0x0F
#define REG_CTRL3_C      0x12
#define CTRL3_C_SW_RESET 0x01
#define REG_OUTX_L_G     0x22
#define REG_TIMESTAMP0   0x40

#define WHO_AM_I_LSM6DS3    0x69
#define WHO_AM_I_LSM6DS3TRC 0x6A

int32_t imu_bus_write(void *handle, uint8_t reg, const uint8_t *buf, uint16_t len) {
    (void)handle;
    uint8_t addr = reg & ~SPI_READ_BIT;
    gpio_put(PIN_IMU_CS, 0);
    spi_write_blocking(IMU_SPI, &addr, 1);
    spi_write_blocking(IMU_SPI, buf, len);
    gpio_put(PIN_IMU_CS, 1);
    return 0;
}

int32_t imu_bus_read(void *handle, uint8_t reg, uint8_t *buf, uint16_t len) {
    (void)handle;
    uint8_t addr = reg | SPI_READ_BIT;
    gpio_put(PIN_IMU_CS, 0);
    spi_write_blocking(IMU_SPI, &addr, 1);
    spi_read_blocking(IMU_SPI, 0, buf, len);
    gpio_put(PIN_IMU_CS, 1);
    return 0;
}

void imu_bus_delay_ms(uint32_t ms) {
    sleep_ms(ms);
}

// Once only: re-running spi_init() resets the block while pins are muxed and glitches SCK.
static void bus_init(void) {
    static bool done;
    if (done) {
        return;
    }
    done = true;

    // CS high before any clock edge; GPIO reset state is pulled low.
    gpio_init(PIN_IMU_CS);
    gpio_put(PIN_IMU_CS, 1);
    gpio_set_dir(PIN_IMU_CS, GPIO_OUT);

    // Mode 3 set before muxing so SCK comes up idle high.
    spi_init(IMU_SPI, IMU_SPI_BAUD_HZ);
    spi_set_format(IMU_SPI, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_set_function(PIN_IMU_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_IMU_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_IMU_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_IMU_INT1);
    gpio_set_dir(PIN_IMU_INT1, GPIO_IN);
    gpio_pull_down(PIN_IMU_INT1);

    sleep_ms(BOOT_TIME_MS);
}

static imu_model_t probe(void) {
    uint8_t id = 0;
    imu_bus_read(NULL, REG_WHO_AM_I, &id, 1);
    switch (id) {
    case WHO_AM_I_LSM6DS3:
        return IMU_MODEL_LSM6DS3;
    case WHO_AM_I_LSM6DS3TRC:
        return IMU_MODEL_LSM6DS3TRC;
    default:
        DBG("imu: unknown WHO_AM_I 0x%02x\n", id);
        return IMU_MODEL_NONE;
    }
}

// SW_RESET is CTRL3_C bit 0 on both chips and self-clears when done.
static bool soft_reset(void) {
    uint8_t val = CTRL3_C_SW_RESET;
    imu_bus_write(NULL, REG_CTRL3_C, &val, 1);
    absolute_time_t deadline = make_timeout_time_ms(RESET_TIMEOUT_MS);
    do {
        if (time_reached(deadline)) {
            return false;
        }
        imu_bus_read(NULL, REG_CTRL3_C, &val, 1);
    } while (val & CTRL3_C_SW_RESET);
    return true;
}

imu_model_t imu_init(void) {
    bus_init();

    imu_model_t model = probe();
    if (model == IMU_MODEL_NONE) {
        return IMU_MODEL_NONE;
    }
    if (!soft_reset()) {
        DBG("imu: reset timeout\n");
        return IMU_MODEL_NONE;
    }

    bool ok = (model == IMU_MODEL_LSM6DS3) ? imu_lsm6ds3_configure() : imu_lsm6ds3trc_configure();
    if (!ok) {
        DBG("imu: config failed\n");
        return IMU_MODEL_NONE;
    }
    DBG("imu: %s ready\n", imu_model_name(model));
    return model;
}

const char *imu_model_name(imu_model_t model) {
    switch (model) {
    case IMU_MODEL_LSM6DS3:
        return "LSM6DS3";
    case IMU_MODEL_LSM6DS3TRC:
        return "LSM6DS3TR-C";
    case IMU_MODEL_NONE:
        break;
    }
    return "none";
}

// The 24-bit counter is not latched across bytes; a second read resolves a carry mid-read.
static void read_timestamp(uint32_t *ts) {
    uint8_t a[3], b[3];
    imu_bus_read(NULL, REG_TIMESTAMP0, a, sizeof(a));
    imu_bus_read(NULL, REG_TIMESTAMP0, b, sizeof(b));
    const uint8_t *t = (a[1] == b[1] && a[2] == b[2]) ? a : b;
    *ts = t[0] | (t[1] << 8) | ((uint32_t)t[2] << 16);
}

bool imu_data_ready(void) {
    return gpio_get(PIN_IMU_INT1);
}

bool imu_read(imu_sample_t *out) {
    // OUTX_L_G..OUTZ_H_XL: gyro XYZ then accel XYZ, little endian.
    uint8_t buf[12];
    if (imu_bus_read(NULL, REG_OUTX_L_G, buf, sizeof(buf))) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        out->gyro[i] = (int16_t)(buf[2 * i] | (buf[2 * i + 1] << 8));
        out->accel[i] = (int16_t)(buf[6 + 2 * i] | (buf[6 + 2 * i + 1] << 8));
    }
    read_timestamp(&out->timestamp);
    return true;
}
