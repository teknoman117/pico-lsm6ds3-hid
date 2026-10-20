#include "report.h"

#include <assert.h>

#include "hardware/sync.h"
#include "pico/time.h"
#include "tusb.h"

#define HID_USAGE_PAGE_SENSOR      0x20
#define HID_USAGE_SENSOR_TIMESTAMP 0x0529
#define MOD_2_31                   0x7FFFFFFFu

// Multi-axis Controller, not Game Pad/Joystick: hid-input gives those axes fuzz = range/256.
// hid-input ignores duplicate codes, so each field uses a usage with a distinct mapping.
static const uint8_t report_desc[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_MULTI_AXIS_CONTROLLER),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
        HID_USAGE(HID_USAGE_DESKTOP_RX),
        HID_USAGE(HID_USAGE_DESKTOP_RY),
        HID_USAGE(HID_USAGE_DESKTOP_RZ),
        HID_USAGE(HID_USAGE_DESKTOP_X),
        HID_USAGE(HID_USAGE_DESKTOP_Y),
        HID_USAGE(HID_USAGE_DESKTOP_Z),
        HID_LOGICAL_MIN_N(INT32_MIN, 3),
        HID_LOGICAL_MAX_N(INT32_MAX, 3),
        HID_REPORT_SIZE(32),
        HID_REPORT_COUNT(6),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

        // No collection opens on the Sensor page, so hid-core does not treat this as a sensor hub.
        HID_USAGE_PAGE(HID_USAGE_PAGE_SENSOR),
        HID_USAGE_N(HID_USAGE_SENSOR_TIMESTAMP, 2), // -> ABS_MISC
        HID_LOGICAL_MIN(0),
        HID_LOGICAL_MAX_N(MOD_2_31, 3),
        HID_REPORT_SIZE(32),
        HID_REPORT_COUNT(1),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

        HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
        HID_USAGE(HID_USAGE_DESKTOP_DIAL), // sequence -> ABS_RUDDER
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

        HID_USAGE(HID_USAGE_DESKTOP_SLIDER), // temperature -> ABS_THROTTLE
        HID_LOGICAL_MIN_N(INT16_MIN, 2),
        HID_LOGICAL_MAX_N(INT16_MAX, 2),
        HID_REPORT_SIZE(16),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),

        HID_USAGE(HID_USAGE_DESKTOP_WHEEL), // flags -> ABS_WHEEL
        HID_LOGICAL_MIN(0),
        HID_LOGICAL_MAX_N(0xFF, 2),
        HID_REPORT_SIZE(8),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,
};

static_assert(sizeof(report_desc) == REPORT_DESC_LEN, "update REPORT_DESC_LEN");
static_assert(sizeof(imu_report_t) == 35, "report layout must match descriptor");
static_assert(sizeof(imu_report_t) <= CFG_TUD_HID_EP_BUFSIZE, "report exceeds endpoint");

// Single slot shared by IRQ (windows) and main (error reports, sending).
static imu_report_t pending;
static bool pending_valid;
static uint32_t sequence;

static void submit(imu_report_t *r) {
    uint32_t irq = save_and_disable_interrupts();
    r->sequence = sequence++ & MOD_2_31;
    if (pending_valid) {
        r->flags |= REPORT_FLAG_USB_BUSY;
    }
    pending = *r;
    pending_valid = true;
    restore_interrupts(irq);
}

void report_submit_window(const imu_window_t *w) {
    imu_report_t r = {
        .timestamp = (uint32_t)w->time_us & MOD_2_31,
        .temperature = w->temperature,
        .flags = w->sample_missed ? REPORT_FLAG_SAMPLE_MISSED : 0,
    };
    for (int i = 0; i < 3; i++) {
        r.gyro_sum[i] = w->gyro_sum[i];
        r.accel_sum[i] = w->accel_sum[i];
    }
    submit(&r);
}

void report_submit_error(void) {
    imu_report_t r = {
        .timestamp = (uint32_t)time_us_64() & MOD_2_31,
        .flags = REPORT_FLAG_ERROR,
    };
    submit(&r);
}

void report_task(void) {
    if (!pending_valid || !tud_hid_ready()) {
        return;
    }
    uint32_t irq = save_and_disable_interrupts();
    imu_report_t r = pending;
    pending_valid = false;
    restore_interrupts(irq);
    tud_hid_report(0, &r, sizeof(r));
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return report_desc;
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
