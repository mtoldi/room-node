#pragma once

#include "esp_err.h"

// Slider potentiometer on an ADC1 pin, powered from 3V3.
esp_err_t setpoint_pot_init(int gpio);

// Calibrated wiper voltage in mV, averaged over several samples to suppress ADC noise.
esp_err_t setpoint_pot_read_mv(int *mv);
