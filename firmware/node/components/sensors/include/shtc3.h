#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#define SHTC3_ADDR 0x70

typedef struct
{
    i2c_master_dev_handle_t dev;
} shtc3_t;

// Checks the chip signature in the ID register and puts the sensor to sleep.
esp_err_t shtc3_init(i2c_master_bus_handle_t bus, shtc3_t *out);

// Wakes the sensor, runs one normal-mode measurement and puts it back to sleep.
// Uses the command without clock stretching: the sensor would otherwise hold SCL
// for up to 12 ms, which is at the edge of the ESP32 I2C timeout. The calling
// task sleeps for the conversion time instead, so the bus stays free.
esp_err_t shtc3_measure(const shtc3_t *sensor, float *celsius, float *humidity);
