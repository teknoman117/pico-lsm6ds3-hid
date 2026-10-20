// SPDX-License-Identifier: MPL-2.0

#include "status_led.h"

#include "config.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"

#define WS2812_FREQ_HZ 800000

static PIO pio;
static uint sm;
static bool ready;
static int current = -1;

static void put_rgb(uint8_t r, uint8_t g, uint8_t b) {
    r = (uint8_t)((r * NEOPIXEL_BRIGHTNESS) / 255);
    g = (uint8_t)((g * NEOPIXEL_BRIGHTNESS) / 255);
    b = (uint8_t)((b * NEOPIXEL_BRIGHTNESS) / 255);
    // GRB, MSB first.
    uint32_t grb = ((uint32_t)g << 24) | ((uint32_t)r << 16) | ((uint32_t)b << 8);
    pio_sm_put(pio, sm, grb);
}

void status_led_init(void) {
    uint offset;
    ready = pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &sm, &offset,
                                                             PIN_NEOPIXEL, 1, true);
    if (ready) {
        ws2812_program_init(pio, sm, offset, PIN_NEOPIXEL, WS2812_FREQ_HZ, NEOPIXEL_RGBW);
    }
}

void status_led_set(status_t status) {
    if (!ready || (int)status == current) {
        return;
    }
    current = (int)status;

    switch (status) {
    case STATUS_IMU_ERROR:
        put_rgb(255, 0, 0);
        break;
    case STATUS_USB_WAIT:
        put_rgb(255, 160, 0);
        break;
    case STATUS_STREAMING:
        put_rgb(0, 255, 0);
        break;
    case STATUS_SUSPENDED:
        put_rgb(0, 0, 255);
        break;
    }
}
