#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "roomcore.h"

// Latest sensor values. A value is kept when a read fails; *_ok tells whether
// the most recent read succeeded and *_fault whether the sensor is considered
// broken (several consecutive failures).
typedef struct
{
    float t_ref; // TMP117, °C
    float t_aux; // SHTC3, °C
    float rh;    // SHTC3, %
    bool tmp117_ok;
    bool shtc3_ok;
    bool tmp117_fault;
    bool shtc3_fault;
    uint32_t tmp117_errors;
    uint32_t shtc3_errors;
    uint32_t samples;
} sensor_data_t;

// Snapshot of everything other tasks need to know about the room.
typedef struct
{
    sensor_data_t sensors;
    float t_ref; // regulated temperature: TMP117, SHTC3 fallback or room model
    bool t_ref_valid;
    float setpoint;
    float pot_setpoint;
    bool heating;
    int heating_priority;
    rc_mode_t mode;
    uint16_t status;
    uint32_t uptime_s;
} room_state_t;

// The controller (roomcore) lives here as well, so commands from any protocol
// and the control cycle are serialised by the same mutex.
void room_state_init(uint32_t min_switch_ms, bool simulation);
void room_state_set_sensors(const sensor_data_t *sensors);
void room_state_control_step(int pot_mv, uint32_t now_ms);
void room_state_get(room_state_t *out);

rc_result_t room_state_write_setpoint(float celsius);
void room_state_relinquish_setpoint(void);
rc_result_t room_state_set_mode(rc_mode_t mode);
