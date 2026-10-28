#pragma once

#include <stdint.h>

#include "room_state.h"

// Draws the room state on the LED stick:
//   heating        one LED per 0.5 °C below the setpoint; orange = thermostat,
//                  purple = operator (mode on), white = frost protection
//   idle           first LED blue
//   no temperature whole stick blinks red (fail-safe)
//   one sensor bad last LED blinks red
void led_view_render(const room_state_t *s, uint32_t now_ms);
