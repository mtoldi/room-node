#pragma once

// MQTT link (topics under roomnode/<id>/):
//   state         retained JSON, every 10 s and right after a relevant change
//   status        retained "online" / "offline" (Last Will)
//   cmd/setpoint  number in °C, or "null" to relinquish the override
//   cmd/mode      "auto" / "off" / "on"
// Reconnects on its own; the MQTT offline status bit tracks the connection.
void mqtt_task_start(void);
