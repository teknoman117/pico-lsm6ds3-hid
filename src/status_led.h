#ifndef STATUS_LED_H
#define STATUS_LED_H

typedef enum {
    STATUS_IMU_ERROR,   // red
    STATUS_USB_WAIT,    // yellow
    STATUS_STREAMING,   // green
    STATUS_SUSPENDED,   // blue
} status_t;

void status_led_init(void);

// Writes the LED only when the state changes.
void status_led_set(status_t status);

#endif
