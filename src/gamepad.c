#include "gamepad.h"

#include "tusb.h"

#define HID_USAGE_PAGE_SENSOR      0x20
#define HID_USAGE_SENSOR_TIMESTAMP 0x0529

// Multi-axis Controller application with a Game Pad physical collection: hid-input maps
// Button 1/2 to BTN_SOUTH/BTN_EAST (via the physical usage) and X..Rz to ABS_X..ABS_RZ,
// without the fuzz/flat it applies to Game Pad applications. Sensor Time Stamp maps to ABS_MISC.
static const uint8_t gamepad_report_desc[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_MULTI_AXIS_CONTROLLER),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
        HID_USAGE(HID_USAGE_DESKTOP_GAMEPAD),
        HID_COLLECTION(HID_COLLECTION_PHYSICAL),
            HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
            HID_USAGE(HID_USAGE_DESKTOP_X),
            HID_USAGE(HID_USAGE_DESKTOP_Y),
            HID_USAGE(HID_USAGE_DESKTOP_Z),
            HID_USAGE(HID_USAGE_DESKTOP_RX),
            HID_USAGE(HID_USAGE_DESKTOP_RY),
            HID_USAGE(HID_USAGE_DESKTOP_RZ),
            HID_LOGICAL_MIN_N(INT16_MIN, 2),
            HID_LOGICAL_MAX_N(INT16_MAX, 2),
            HID_REPORT_SIZE(16),
            HID_REPORT_COUNT(6),
            HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

            HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),
            HID_USAGE_MIN(1),
            HID_USAGE_MAX(2),
            HID_LOGICAL_MIN(0),
            HID_LOGICAL_MAX(1),
            HID_REPORT_SIZE(1),
            HID_REPORT_COUNT(2),
            HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
            HID_REPORT_SIZE(6),
            HID_REPORT_COUNT(1),
            HID_INPUT(HID_CONSTANT),
        HID_COLLECTION_END,

        // Sensor page Time Stamp. No collection opens on this page, so the device
        // is not grouped as a sensor hub.
        HID_USAGE_PAGE(HID_USAGE_PAGE_SENSOR),
        HID_USAGE_N(HID_USAGE_SENSOR_TIMESTAMP, 2),
        HID_LOGICAL_MIN(0),
        HID_LOGICAL_MAX_N(IMU_TIMESTAMP_MASK, 3),
        HID_REPORT_SIZE(32),
        HID_REPORT_COUNT(1),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,
};

static_assert(sizeof(gamepad_report_desc) == GAMEPAD_REPORT_DESC_LEN, "update GAMEPAD_REPORT_DESC_LEN");
static_assert(sizeof(gamepad_report_t) == 17, "report layout must match descriptor");

bool gamepad_send(const imu_sample_t *s) {
    if (!tud_hid_ready()) {
        return false;
    }
    gamepad_report_t r = {
        .x = s->accel[0],
        .y = s->accel[1],
        .z = s->accel[2],
        .rx = s->gyro[0],
        .ry = s->gyro[1],
        .rz = s->gyro[2],
        .buttons = 0,
        .timestamp = s->timestamp,
    };
    return tud_hid_report(0, &r, sizeof(r));
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return gamepad_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}
