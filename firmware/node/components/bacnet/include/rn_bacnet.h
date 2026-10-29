#pragma once

// BACnet/IP server for the room controller. Protocol encoding, services and
// BVLC come from bacnet-stack; the Device object, the room's objects and the
// UDP datalink are implemented here.
//
// Objects
//   Device <instance>     RoomNode-<id>
//   AI 1  Room temperature   (regulated temperature, °C)
//   AI 2  Aux temperature    (SHTC3, °C)
//   AI 3  Relative humidity  (%)
//   AV 1  Setpoint           commandable, priority array; relinquish default = slider
//   BO 1  Heating            commandable, priority array; minimum on/off time
//   MSV 1 Mode               1 Auto, 2 Off, 3 On
// External writes are accepted at priority 8 only; levels 1-6 and 16 belong
// to the controller's own safety logic and thermostat.

#include <stdbool.h>
#include <stdint.h>

#include "points.h"
#include "priority_array.h"

#define RN_BACNET_PORT 47808
#define RN_BACNET_WRITE_PRIORITY 8

typedef struct
{
    // Consistent snapshot of the room and both priority arrays
    void (*read)(rc_points_t *points, priority_array_t *heating, priority_array_t *setpoint);
    rc_result_t (*write_setpoint)(float celsius);
    void (*relinquish_setpoint)(void);
    rc_result_t (*set_mode)(rc_mode_t mode);
} rn_bacnet_access_t;

typedef struct
{
    uint32_t device_instance;
    const char *device_name;
    uint32_t min_switch_s; // reported as Minimum_On_Time / Minimum_Off_Time of BO 1
} rn_bacnet_config_t;

// Opens the UDP socket on the Wi-Fi interface and announces the device (I-Am).
// Call once the interface has an IP address.
bool rn_bacnet_start(const rn_bacnet_config_t *config, const rn_bacnet_access_t *access);

// Receives and handles at most one request, waiting up to timeout_ms.
void rn_bacnet_poll(unsigned timeout_ms);
