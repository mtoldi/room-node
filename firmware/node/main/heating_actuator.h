#pragma once

#include <stdbool.h>

// The controller only decides "heat / don't heat". What is on the other end
// (LED simulation, relay contact, smart plug over MQTT) is an implementation
// of this interface; changing it needs no change in the control logic.
typedef struct heating_actuator
{
    void (*set)(struct heating_actuator *self, bool on);
    bool (*is_on)(const struct heating_actuator *self);
} heating_actuator_t;

// v1: simulated output. Its state is shown on the LED stick by led_view.
heating_actuator_t *sim_actuator_get(void);
