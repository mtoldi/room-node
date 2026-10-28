#pragma once

#include "heating_actuator.h"

// Starts the 200 ms control cycle: potentiometer, roomcore step, actuator, LED stick.
void control_task_start(heating_actuator_t *actuator);
