#include "config.h"
#include "debug.h"
#include "gamepad.h"
#include "imu.h"
#include "pico/stdlib.h"
#include "status_led.h"
#include "tusb.h"

#define IMU_RETRY_MS 1000

static const imu_config_t imu_cfg = {
    .xl_odr = IMU_XL_ODR,
    .xl_fs = IMU_XL_FS,
    .xl_aa_bw = IMU_XL_AA_BW,
    .xl_filter = IMU_XL_FILTER,
    .xl_lpf2_bw = IMU_XL_LPF2_BW,
    .xl_hpf_bw = IMU_XL_HPF_BW,
    .gy_odr = IMU_GY_ODR,
    .gy_fs = IMU_GY_FS,
    .gy_hpf_en = IMU_GY_HPF_EN,
    .gy_hpf_bw = IMU_GY_HPF_BW,
};

static status_t current_status(bool imu_ok) {
    if (!imu_ok) {
        return STATUS_IMU_ERROR;
    }
    if (tud_suspended()) {
        return STATUS_SUSPENDED;
    }
    return tud_mounted() ? STATUS_STREAMING : STATUS_USB_WAIT;
}

int main(void) {
#ifndef NDEBUG
    stdio_init_all();
#endif
    DBG("pico-lsm6ds3 start\n");

    status_led_init();
    tusb_init();

    bool imu_ok = imu_init(&imu_cfg);
    absolute_time_t next_retry = make_timeout_time_ms(IMU_RETRY_MS);
    imu_sample_t sample;
    bool pending = false;

    while (true) {
        tud_task();

        if (!imu_ok) {
            if (time_reached(next_retry)) {
                imu_ok = imu_init(&imu_cfg);
                next_retry = make_timeout_time_ms(IMU_RETRY_MS);
            }
        } else if (imu_data_ready()) {
            pending = imu_read(&sample);
        }

        if (pending && tud_mounted() && gamepad_send(&sample)) {
            pending = false;
        }

        status_led_set(current_status(imu_ok));
    }
}
