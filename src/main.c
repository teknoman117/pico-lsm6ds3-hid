#include <string.h>

#include "config.h"
#include "debug.h"
#include "imu.h"
#include "pico/stdlib.h"
#include "report.h"
#include "status_led.h"
#include "tusb.h"
#include "usb_descriptors.h"

#define IMU_RETRY_MS 1000

static status_t current_status(bool imu_ok) {
    if (!imu_ok) {
        return STATUS_IMU_ERROR;
    }
    if (tud_suspended()) {
        return STATUS_SUSPENDED;
    }
    return tud_mounted() ? STATUS_STREAMING : STATUS_USB_WAIT;
}

// Re-enumerate if the detected sensor differs from the advertised product name.
static void update_product_name(imu_model_t model) {
    const char *name = imu_model_name(model);
    if (strcmp(name, usb_product()) == 0) {
        return;
    }
    usb_set_product(name);
    tud_disconnect();
    sleep_ms(USB_REENUM_MS);
    tud_connect();
}

int main(void) {
#ifndef NDEBUG
    stdio_init_all();
#endif
    DBG("pico-lsm6ds3 start\n");

    status_led_init();

    // Identify the sensor before enumeration so the product string is right first time.
    imu_model_t model = imu_init(report_submit_window);
    if (model != IMU_MODEL_NONE) {
        usb_set_product(imu_model_name(model));
    }
    tusb_init();

    absolute_time_t next_retry = make_timeout_time_ms(IMU_RETRY_MS);
    absolute_time_t next_error_report = get_absolute_time();

    while (true) {
        tud_task();

        // Error state: keep enumerating and reporting at the normal rate with the error flag.
        if (model == IMU_MODEL_NONE) {
            if (time_reached(next_error_report)) {
                report_submit_error();
                next_error_report = delayed_by_us(next_error_report, imu_window_period_us());
            }
            if (time_reached(next_retry)) {
                model = imu_init(report_submit_window);
                if (model != IMU_MODEL_NONE) {
                    update_product_name(model);
                }
                next_retry = make_timeout_time_ms(IMU_RETRY_MS);
            }
        }

        report_task();
        status_led_set(current_status(model != IMU_MODEL_NONE));
    }
}
