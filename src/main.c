// SPDX-License-Identifier: MPL-2.0

#include "config.h"
#include "debug.h"
#include "hardware/clocks.h"
#include "imu.h"
#include "pico/stdlib.h"
#include "report.h"
#include "status_led.h"
#include "tusb.h"
#include "usb_descriptors.h"

#define PROFILE_PERIOD_MS 1000

static status_t current_status(bool imu_ok) {
    if (!imu_ok) {
        return STATUS_IMU_ERROR;
    }
    if (tud_suspended()) {
        return STATUS_SUSPENDED;
    }
    return tud_mounted() ? STATUS_STREAMING : STATUS_USB_WAIT;
}

#ifndef NDEBUG
static void print_profile(void) {
    imu_profile_t p;
    imu_profile_take(&p);
    uint32_t cycles_per_us = clock_get_hz(clk_sys) / 1000000;
    uint32_t total_us = p.total_cycles / cycles_per_us;
    DBG("filter: %lu calls, %lu us/s (%lu.%lu%% CPU), avg %lu cycles, max %lu cycles\n",
        (unsigned long)p.calls, (unsigned long)total_us, (unsigned long)(total_us / 10000),
        (unsigned long)(total_us / 1000 % 10), (unsigned long)(p.calls ? p.total_cycles / p.calls : 0),
        (unsigned long)p.max_cycles);
}
#endif

int main(void) {
#ifndef NDEBUG
    stdio_init_all();
#endif
    DBG("pico-lsm6ds3 start\n");

    status_led_init();

    // Fixed hardware: the sensor is probed once. On failure the device latches into the
    // error state and keeps reporting so the host sees the fault.
    imu_model_t model = imu_init(report_submit_window);
    bool imu_ok = model != IMU_MODEL_NONE;
    if (imu_ok) {
        usb_set_product(imu_model_name(model));
    }
    tusb_init();

    absolute_time_t next_error_report = get_absolute_time();
#ifndef NDEBUG
    absolute_time_t next_profile = make_timeout_time_ms(PROFILE_PERIOD_MS);
#endif

    while (true) {
        tud_task();

        if (!imu_ok && time_reached(next_error_report)) {
            report_submit_error();
            next_error_report = delayed_by_us(next_error_report, imu_window_period_us());
        }

        report_task();
        status_led_set(current_status(imu_ok));

#ifndef NDEBUG
        if (imu_ok && time_reached(next_profile)) {
            print_profile();
            next_profile = delayed_by_ms(next_profile, PROFILE_PERIOD_MS);
        }
#endif
    }
}
