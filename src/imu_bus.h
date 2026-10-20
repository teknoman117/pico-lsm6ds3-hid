#ifndef IMU_BUS_H
#define IMU_BUS_H

// Private to the imu module. Chip files each include only their own ST header,
// since the two drivers define incompatible stmdev_ctx_t.

#include <stdbool.h>
#include <stdint.h>

int32_t imu_bus_write(void *handle, uint8_t reg, const uint8_t *buf, uint16_t len);
int32_t imu_bus_read(void *handle, uint8_t reg, uint8_t *buf, uint16_t len);
void imu_bus_delay_ms(uint32_t ms);

// Apply config.h to a freshly reset chip. Return false on bus error.
bool imu_lsm6ds3_configure(void);
bool imu_lsm6ds3trc_configure(void);

#endif
