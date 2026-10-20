#include "imu.h"

#include "config.h"
#include "debug.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/systick.h"
#include "hardware/sync.h"
#include "imu_bus.h"
#include "pico/time.h"

#define SPI_READ_BIT     0x80
#define BOOT_TIME_MS     20
#define RESET_TIMEOUT_MS 50
#define MAX_RECORDED     32

// Registers shared by LSM6DS3 and LSM6DS3TR-C.
#define REG_WHO_AM_I     0x0F
#define REG_CTRL3_C      0x12
#define CTRL3_C_SW_RESET 0x01
#define REG_OUT_TEMP_L   0x20

// OUT_TEMP_L..OUTZ_H_XL: temperature, gyro XYZ, accel XYZ; int16 little endian.
#define BURST_LEN        14

#define WHO_AM_I_LSM6DS3    0x69
#define WHO_AM_I_LSM6DS3TRC 0x6A

static_assert(IMU_GY_ODR != IMU_ODR_OFF, "gyro data-ready drives sampling");
static_assert(IMU_SETTLE_SAMPLES % IMU_AVERAGE_N == 0, "settling must end on a window boundary");

// ODRs are binary divisions of 6.667 kHz (150 us), so every period is a whole number of us.
static uint32_t odr_period_us(imu_odr_t odr) {
    return 150u << (IMU_ODR_6K66HZ - odr);
}

// Every register written during configuration, for read-back verification.
static struct {
    uint8_t reg;
    uint8_t val;
} recorded[MAX_RECORDED];
static int recorded_count = -1; // -1: not recording

int32_t imu_bus_write(void *handle, uint8_t reg, const uint8_t *buf, uint16_t len) {
    (void)handle;
    uint8_t addr = reg & ~SPI_READ_BIT;
    gpio_put(PIN_IMU_CS, 0);
    spi_write_blocking(IMU_SPI, &addr, 1);
    spi_write_blocking(IMU_SPI, buf, len);
    gpio_put(PIN_IMU_CS, 1);

    for (uint16_t i = 0; recorded_count >= 0 && i < len; i++) {
        int j = 0;
        while (j < recorded_count && recorded[j].reg != reg + i) {
            j++;
        }
        if (j == MAX_RECORDED) {
            return -1;
        }
        recorded[j].reg = reg + i;
        recorded[j].val = buf[i];
        if (j == recorded_count) {
            recorded_count++;
        }
    }
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

// DMA read: INT1 IRQ timestamps the sample and starts the burst; the DMA IRQ accumulates it.
// Byte 0 of rx is the address phase.
static int dma_tx;
static int dma_rx;
static volatile bool xfer_busy;
static uint8_t tx_burst[1 + BURST_LEN] = {REG_OUT_TEMP_L | SPI_READ_BIT};
static uint8_t rx_burst[1 + BURST_LEN];

// Sampling state; touched only by the IRQs while sampling runs.
static imu_window_cb_t window_cb;
static uint32_t sample_period_us;
static uint64_t sample_time_us;
static uint64_t prev_time_us;
static uint32_t settle_samples;
static imu_window_t window;
static uint64_t window_time_sum;
static uint32_t window_count;

#ifndef NDEBUG
// SysTick as a 24-bit down-counting cycle counter for profiling accumulate().
#define SYSTICK_MASK       0x00FFFFFFu
#define SYSTICK_ENABLE_CPU 0x5 // CSR: ENABLE | CLKSOURCE (processor clock)
static imu_profile_t profile;
#endif

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
    dma_channel_configure(dma_tx, &c, &spi_get_hw(IMU_SPI)->dr, tx_burst, sizeof(tx_burst), false);

    c = dma_channel_get_default_config(dma_rx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, spi_get_dreq(IMU_SPI, false));
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    dma_channel_configure(dma_rx, &c, rx_burst, &spi_get_hw(IMU_SPI)->dr, sizeof(rx_burst), false);

    dma_channel_set_irq0_enabled(dma_rx, true);
    irq_add_shared_handler(DMA_IRQ_0, dma_irq_handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);
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

#ifndef NDEBUG
    systick_hw->rvr = SYSTICK_MASK;
    systick_hw->cvr = 0;
    systick_hw->csr = SYSTICK_ENABLE_CPU;
#endif

    dma_init();
    gpio_add_raw_irq_handler(PIN_IMU_INT1, int1_irq_handler);
    irq_set_enabled(IO_IRQ_BANK0, true);

    sleep_ms(BOOT_TIME_MS);
}

// Hot-path helpers: inline so they run from RAM with their callers. The SDK's
// gpio_set_irq_enabled() and time_us_64() live in flash.

// Same register as gpio_set_irq_enabled(); everything runs on core 0.
static __force_inline void set_int1_irq(bool enabled) {
    io_rw_32 *inte = &io_bank0_hw->proc0_irq_ctrl.inte[PIN_IMU_INT1 / 8];
    uint32_t mask = GPIO_IRQ_LEVEL_HIGH << (4 * (PIN_IMU_INT1 % 8));
    if (enabled) {
        hw_set_bits(inte, mask);
    } else {
        hw_clear_bits(inte, mask);
    }
}

// Same read as timer_time_us_64(): re-read if the high word changed.
static __force_inline uint64_t now_us(void) {
    timer_hw_t *timer = PICO_DEFAULT_TIMER_INSTANCE();
    uint32_t hi = timer->timerawh;
    uint32_t lo;
    while (true) {
        lo = timer->timerawl;
        uint32_t next_hi = timer->timerawh;
        if (hi == next_hi) {
            break;
        }
        hi = next_hi;
    }
    return ((uint64_t)hi << 32) | lo;
}

// Explicit stores, no loop: memset (or a zeroing loop at -O3) calls into flash.
static __force_inline void window_reset(void) {
    window.gyro_sum[0] = 0;
    window.gyro_sum[1] = 0;
    window.gyro_sum[2] = 0;
    window.accel_sum[0] = 0;
    window.accel_sum[1] = 0;
    window.accel_sum[2] = 0;
    window.time_us = 0;
    window.temperature = 0;
    window.sample_missed = false;
    window_time_sum = 0;
    window_count = 0;
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

// Applies config.h, then reads back every register the driver wrote.
static bool configure(imu_model_t model) {
    recorded_count = 0;
    bool ok = (model == IMU_MODEL_LSM6DS3) ? imu_lsm6ds3_configure() : imu_lsm6ds3trc_configure();
    int count = recorded_count;
    recorded_count = -1;
    if (!ok) {
        DBG("imu: config failed\n");
        return false;
    }

    for (int i = 0; i < count; i++) {
        uint8_t val = 0;
        imu_bus_read(NULL, recorded[i].reg, &val, 1);
        if (val != recorded[i].val) {
            DBG("imu: reg 0x%02x wrote 0x%02x read 0x%02x\n", recorded[i].reg, recorded[i].val, val);
            ok = false;
        }
    }
    return ok;
}

static void reset_sampling(imu_model_t model) {
    imu_odr_t gy_odr = IMU_GY_ODR;
    if (model == IMU_MODEL_LSM6DS3 && gy_odr > IMU_ODR_1K66HZ) {
        gy_odr = IMU_ODR_1K66HZ;
    }
    sample_period_us = odr_period_us(gy_odr);
    prev_time_us = 0;
    window_reset();
    settle_samples = IMU_SETTLE_SAMPLES;
}

imu_model_t imu_init(imu_window_cb_t cb) {
    bus_init();

    // The IRQ/DMA path owns SPI while sampling; stop it before touching the chip.
    set_int1_irq(false);
    while (xfer_busy) {
        tight_loop_contents();
    }
    window_cb = cb;

    imu_model_t model = probe();
    if (model == IMU_MODEL_NONE) {
        return IMU_MODEL_NONE;
    }
    if (!soft_reset()) {
        DBG("imu: reset timeout\n");
        return IMU_MODEL_NONE;
    }
    if (!configure(model)) {
        return IMU_MODEL_NONE;
    }

    reset_sampling(model);
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

#ifndef NDEBUG
void imu_profile_take(imu_profile_t *out) {
    uint32_t irq = save_and_disable_interrupts();
    *out = profile;
    profile = (imu_profile_t){0};
    restore_interrupts(irq);
}
#endif

uint32_t imu_window_period_us(void) {
    return odr_period_us(IMU_GY_ODR) * IMU_AVERAGE_N;
}

static __force_inline int16_t le16(const uint8_t *b) {
    return (int16_t)(b[0] | (b[1] << 8));
}

// Average by count: exactly IMU_AVERAGE_N consecutive samples per window, none dropped.
static void __not_in_flash_func(accumulate)(uint64_t t, const uint8_t *b) {
    bool gap = prev_time_us != 0 && (t - prev_time_us) > sample_period_us * 3 / 2;
    prev_time_us = t;
    if (settle_samples > 0) {
        settle_samples--;
        return;
    }

    window.sample_missed |= gap;
    window.temperature = le16(&b[0]);
    for (int i = 0; i < 3; i++) {
        window.gyro_sum[i] += le16(&b[2 + 2 * i]);
        window.accel_sum[i] += le16(&b[8 + 2 * i]);
    }
    window_time_sum += t;

    if (++window_count == IMU_AVERAGE_N) {
        window.time_us = window_time_sum / IMU_AVERAGE_N;
        window_cb(&window);
        window_reset();
    }
}

// INT1 is a latched level that stays high until the outputs are read; the LSM6DS3 has no
// pulsed DRDY mode. Level triggering re-fires after a late read, where an edge would stall.
static void __not_in_flash_func(int1_irq_handler)(void) {
    if (!(gpio_get_irq_event_mask(PIN_IMU_INT1) & GPIO_IRQ_LEVEL_HIGH)) {
        return;
    }
    sample_time_us = now_us();
    set_int1_irq(false);
    xfer_busy = true;

    gpio_put(PIN_IMU_CS, 0);
    dma_channel_set_read_addr(dma_tx, tx_burst, false);
    dma_channel_set_trans_count(dma_tx, sizeof(tx_burst), false);
    dma_channel_set_write_addr(dma_rx, rx_burst, false);
    dma_channel_set_trans_count(dma_rx, sizeof(rx_burst), false);
    dma_start_channel_mask((1u << dma_tx) | (1u << dma_rx));
}

// RX completion means every byte has been clocked, so CS can be released.
static void __not_in_flash_func(dma_irq_handler)(void) {
    if (!dma_channel_get_irq0_status(dma_rx)) {
        return;
    }
    dma_channel_acknowledge_irq0(dma_rx);
    gpio_put(PIN_IMU_CS, 1);

#ifndef NDEBUG
    uint32_t start = systick_hw->cvr;
#endif
    accumulate(sample_time_us, &rx_burst[1]);
#ifndef NDEBUG
    uint32_t cycles = (start - systick_hw->cvr) & SYSTICK_MASK;
    profile.calls++;
    profile.total_cycles += cycles;
    if (cycles > profile.max_cycles) {
        profile.max_cycles = cycles;
    }
#endif
    xfer_busy = false;
    set_int1_irq(true);
}
