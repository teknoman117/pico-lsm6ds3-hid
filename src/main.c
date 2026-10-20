#include <string.h>

#include "config.h"
#include "debug.h"
#include "gamepad.h"
#include "imu.h"
#include "pico/stdlib.h"
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
    imu_model_t model = imu_init();
    if (model != IMU_MODEL_NONE) {
        usb_set_product(imu_model_name(model));
    }
    tusb_init();

    absolute_time_t next_retry = make_timeout_time_ms(IMU_RETRY_MS);
    imu_sample_t sample;
    bool pending = false;

    while (true) {
        tud_task();

        if (model == IMU_MODEL_NONE) {
            if (time_reached(next_retry)) {
                model = imu_init();
                if (model != IMU_MODEL_NONE) {
                    update_product_name(model);
                }
                next_retry = make_timeout_time_ms(IMU_RETRY_MS);
            }
        } else if (imu_data_ready()) {
            pending = imu_read(&sample);
        }

        if (pending && tud_mounted() && gamepad_send(&sample)) {
            pending = false;
        }

        status_led_set(current_status(model != IMU_MODEL_NONE));
    }
}
