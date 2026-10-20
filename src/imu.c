#include "imu.h"

#include "config.h"
#include "debug.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "imu_bus.h"
#include "pico/time.h"
#include "pico/util/queue.h"

#define SPI_READ_BIT     0x80
#define BOOT_TIME_MS     20
#define RESET_TIMEOUT_MS 50
#define SAMPLE_QUEUE_LEN 32

// Registers shared by LSM6DS3 and LSM6DS3TR-C.
#define REG_WHO_AM_I     0x0F
#define REG_CTRL3_C      0x12
#define CTRL3_C_SW_RESET 0x01
#define REG_OUTX_L_G     0x22
#define REG_TIMESTAMP0   0x40
#define REG_TIMESTAMP2   0x42

// One burst covers outputs through the timestamp. 0x2E..0x3F (sensor hub, FIFO status/data)
// have no read side effects with the FIFO in bypass.
#define BURST_LEN        (REG_TIMESTAMP2 - REG_OUTX_L_G + 1)
#define BURST_TS_OFFSET  (REG_TIMESTAMP0 - REG_OUTX_L_G)
#define TS_LEN           3
#define TS_RESET_VALUE   0xAA

// The sensor counter saturates at 0xFFFFFF instead of wrapping. Past this raw value it is
// reset and the elapsed count folded into ts_base, so reported time wraps mod 2^24.
#ifndef TS_RESET_THRESHOLD
#define TS_RESET_THRESHOLD 0x800000u
#endif

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

static queue_t samples;
static volatile uint32_t overflows;

// DMA read: INT1 IRQ starts the burst, DMA IRQ chains a second timestamp read, queues the
// sample and, when due, a counter reset write.
// Byte 0 of each rx buffer is the address phase.
typedef enum {
    XFER_IDLE,
    XFER_BURST,
    XFER_TIMESTAMP,
    XFER_TS_RESET,
} xfer_state_t;

static int dma_tx;
static int dma_rx;
static volatile xfer_state_t xfer = XFER_IDLE;
static uint8_t tx_burst[1 + BURST_LEN] = {REG_OUTX_L_G | SPI_READ_BIT};
static uint8_t rx_burst[1 + BURST_LEN];
static uint8_t tx_ts[1 + TS_LEN] = {REG_TIMESTAMP0 | SPI_READ_BIT};
static uint8_t rx_ts[1 + TS_LEN];
static const uint8_t tx_ts_reset[2] = {REG_TIMESTAMP2, TS_RESET_VALUE};
static uint8_t rx_ts_reset[2];
static uint32_t ts_base;

static void int1_irq_handler(void);
static void dma_irq_handler(void);

static void dma_init(void) {
    dma_tx = dma_claim_unused_channel(true);
    dma_rx = dma_claim_unused_channel(true);

    dma_channel_config c = dma_channel_get_default_config(dma_tx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, spi_get_dreq(IMU_SPI, true));
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    dma_channel_configure(dma_tx, &c, &spi_get_hw(IMU_SPI)->dr, NULL, 0, false);

    c = dma_channel_get_default_config(dma_rx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, spi_get_dreq(IMU_SPI, false));
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    dma_channel_configure(dma_rx, &c, NULL, &spi_get_hw(IMU_SPI)->dr, 0, false);

    dma_channel_set_irq0_enabled(dma_rx, true);
    irq_add_shared_handler(DMA_IRQ_0, dma_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);
}

static void start_xfer(const uint8_t *tx, uint8_t *rx, uint32_t len) {
    gpio_put(PIN_IMU_CS, 0);
    dma_channel_set_read_addr(dma_tx, tx, false);
    dma_channel_set_trans_count(dma_tx, len, false);
    dma_channel_set_write_addr(dma_rx, rx, false);
    dma_channel_set_trans_count(dma_rx, len, false);
    dma_start_channel_mask((1u << dma_tx) | (1u << dma_rx));
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

    queue_init(&samples, sizeof(imu_sample_t), SAMPLE_QUEUE_LEN);
    dma_init();
    gpio_add_raw_irq_handler(PIN_IMU_INT1, int1_irq_handler);
    irq_set_enabled(IO_IRQ_BANK0, true);

    sleep_ms(BOOT_TIME_MS);
}

static void set_int1_irq(bool enabled) {
    gpio_set_irq_enabled(PIN_IMU_INT1, GPIO_IRQ_LEVEL_HIGH, enabled);
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

    // The IRQ/DMA path owns SPI while streaming; stop it before touching the chip.
    set_int1_irq(false);
    while (xfer != XFER_IDLE) {
        tight_loop_contents();
    }
    imu_sample_t stale;
    while (queue_try_remove(&samples, &stale)) {
    }
    ts_base = 0;

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
    set_int1_irq(true);
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

static uint32_t ts_from_bytes(const uint8_t *t) {
    return t[0] | (t[1] << 8) | ((uint32_t)t[2] << 16);
}

// Returns true if the sensor counter should be reset now.
static bool finish_sample(void) {
    const uint8_t *out = &rx_burst[1]; // OUTX_L_G..OUTZ_H_XL: gyro XYZ then accel XYZ, LE
    imu_sample_t s;
    for (int i = 0; i < 3; i++) {
        s.gyro[i] = (int16_t)(out[2 * i] | (out[2 * i + 1] << 8));
        s.accel[i] = (int16_t)(out[6 + 2 * i] | (out[6 + 2 * i + 1] << 8));
    }
    // Bytes are not latched and read low first, so a carry mid-read gives a value that is
    // too high. The burst read precedes the second read; if it is larger, it is torn.
    uint32_t a = ts_from_bytes(&rx_burst[1 + BURST_TS_OFFSET]);
    uint32_t b = ts_from_bytes(&rx_ts[1]);
    s.timestamp = (ts_base + (a <= b ? a : b)) & IMU_TIMESTAMP_MASK;

    if (!queue_try_add(&samples, &s)) {
        overflows++;
    }

    if (b < TS_RESET_THRESHOLD) {
        return false;
    }
    ts_base += b;
    return true;
}

// INT1 is a level that stays high until the outputs are read. Level triggering re-fires
// if a new sample lands during the read, where an edge trigger would stall.
static void int1_irq_handler(void) {
    if (!(gpio_get_irq_event_mask(PIN_IMU_INT1) & GPIO_IRQ_LEVEL_HIGH)) {
        return;
    }
    set_int1_irq(false);
    xfer = XFER_BURST;
    start_xfer(tx_burst, rx_burst, sizeof(tx_burst));
}

// RX completion means every byte has been clocked, so CS can be released.
static void dma_irq_handler(void) {
    if (!dma_channel_get_irq0_status(dma_rx)) {
        return;
    }
    dma_channel_acknowledge_irq0(dma_rx);
    gpio_put(PIN_IMU_CS, 1);

    if (xfer == XFER_BURST) {
        xfer = XFER_TIMESTAMP;
        start_xfer(tx_ts, rx_ts, sizeof(tx_ts));
        return;
    }
    if (xfer == XFER_TIMESTAMP && finish_sample()) {
        xfer = XFER_TS_RESET;
        start_xfer(tx_ts_reset, rx_ts_reset, sizeof(tx_ts_reset));
        return;
    }
    xfer = XFER_IDLE;
    set_int1_irq(true);
}

bool imu_pop(imu_sample_t *out) {
    return queue_try_remove(&samples, out);
}

uint32_t imu_overflow_count(void) {
    return overflows;
}
