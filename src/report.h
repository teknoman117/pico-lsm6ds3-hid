// SPDX-License-Identifier: MPL-2.0

#ifndef REPORT_H
#define REPORT_H

#include <stdint.h>

#include "imu.h"

#define REPORT_FLAG_SAMPLE_MISSED 0x01 // a sample gap > 1.5 periods in this window
#define REPORT_FLAG_USB_BUSY      0x02 // an earlier report was replaced before it was sent
#define REPORT_FLAG_ERROR         0x04 // sensor missing, unknown or failed read-back; data is 0

// Input report. Field order matches the HID report descriptor.
typedef struct __attribute__((packed)) {
    int32_t gyro_sum[3];  // -> ABS_RX, ABS_RY, ABS_RZ
    int32_t accel_sum[3]; // -> ABS_X, ABS_Y, ABS_Z
    uint32_t timestamp;   // window midpoint, Pico us mod 2^31 -> ABS_MISC
    uint32_t sequence;    // +1 per report built, mod 2^31 -> ABS_RUDDER
    int16_t temperature;  // OUT_TEMP raw -> ABS_THROTTLE
    uint8_t flags;        // REPORT_FLAG_* -> ABS_WHEEL
} imu_report_t;

// Length of the HID report descriptor; checked against the array in report.c.
#define REPORT_DESC_LEN 82

// Builds a report from a window. IRQ-safe. Never blocks: an unsent report is replaced.
void report_submit_window(const imu_window_t *window);

// Builds an error report (data 0, REPORT_FLAG_ERROR). Main context.
void report_submit_error(void);

// Sends the pending report when the endpoint is free. Main loop.
void report_task(void);

#endif
