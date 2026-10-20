#ifndef USB_DESCRIPTORS_H
#define USB_DESCRIPTORS_H

// Product string; takes effect on the next enumeration. name must stay valid.
void usb_set_product(const char *name);
const char *usb_product(void);

#endif
