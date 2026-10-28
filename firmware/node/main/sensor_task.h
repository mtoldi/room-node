#pragma once

#include "driver/i2c_master.h"

// Starts the task that owns the I2C bus and samples both sensors once per second.
// A sensor missing at boot is probed again every cycle, so it can be plugged in later.
void sensor_task_start(i2c_master_bus_handle_t bus);
