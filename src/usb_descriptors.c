#include <string.h>

#include "config.h"
#include "gamepad.h"
#include "pico/unique_id.h"
#include "pico/usb_reset.h"
#include "tusb.h"

enum {
    ITF_NUM_HID,
    ITF_NUM_RESET,
    ITF_NUM_TOTAL,
};

enum {
    STR_LANGID,
    STR_PRODUCT,
    STR_SERIAL,
    STR_RESET,
    STR_COUNT,
};

#define EPNUM_HID_IN   0x81
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_RPI_RESET_DESC_LEN)

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 0, // none, so the kernel names the device just USB_PRODUCT
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

static const uint8_t desc_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, GAMEPAD_REPORT_DESC_LEN,
                       EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, USB_HID_POLL_MS),
    TUD_RPI_RESET_DESCRIPTOR(ITF_NUM_RESET, STR_RESET),
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_config;
}

static const char *const strings[STR_COUNT] = {
    [STR_PRODUCT] = USB_PRODUCT,
    [STR_RESET] = "Reset",
};

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t desc[1 + 32];
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    const char *str;
    size_t len;

    if (index == STR_LANGID) {
        desc[1] = 0x0409;
        len = 1;
    } else {
        if (index == STR_SERIAL) {
            pico_get_unique_board_id_string(serial, sizeof(serial));
            str = serial;
        } else if (index < STR_COUNT && strings[index]) {
            str = strings[index];
        } else {
            return NULL;
        }
        len = strlen(str);
        if (len > 32) {
            len = 32;
        }
        for (size_t i = 0; i < len; i++) {
            desc[1 + i] = (uint8_t)str[i];
        }
    }
    desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return desc;
}
