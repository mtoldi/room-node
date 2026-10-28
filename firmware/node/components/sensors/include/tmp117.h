#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#define TMP117_ADDR_DEFAULT 0x49 // Soldered breakout, factory setting
#define TMP117_ADDR_ALT     0x48 // address jumper changed

typedef struct
{
    i2c_master_dev_handle_t dev;
    uint8_t addr;
} tmp117_t;

// Probes the default and the alternative address and checks the device ID (0x117).
// The sensor's factory configuration (continuous conversion, 1 s cycle, 8x averaging)
// is used as is, so nothing is ever written to its EEPROM.
esp_err_t tmp117_init(i2c_master_bus_handle_t bus, tmp117_t *out);

// Reads the last conversion result. Returns ESP_ERR_INVALID_STATE while the first
// conversion after power-up is not finished yet.
esp_err_t tmp117_read_celsius(const tmp117_t *sensor, float *celsius);
