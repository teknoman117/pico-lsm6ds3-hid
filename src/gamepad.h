#ifndef GAMEPAD_H
#define GAMEPAD_H

#include <stdbool.h>
#include <stdint.h>
#include "imu.h"

// Input report. Field order matches gamepad_report_desc.
typedef struct __attribute__((packed)) {
    int16_t x, y, z;    // accel  -> ABS_X, ABS_Y, ABS_Z
    int16_t rx, ry, rz; // gyro   -> ABS_RX, ABS_RY, ABS_RZ
    uint8_t buttons;    // bit0 BTN_SOUTH, bit1 BTN_EAST
    uint32_t timestamp; // sensor timer, 24-bit, 25 us/LSB -> ABS_MISC
} gamepad_report_t;

// Length of the HID report descriptor; checked against the array in gamepad.c.
#define GAMEPAD_REPORT_DESC_LEN 78

// Queues a report if the endpoint is free. Returns false if busy.
bool gamepad_send(const imu_sample_t *sample);

#endif
