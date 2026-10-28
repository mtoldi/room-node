#pragma once

// The room's data points in protocol-neutral form, and their text encodings
// for MQTT. Every protocol reads the same snapshot, so values are consistent
// across MQTT, Modbus and BACnet.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "roomcore.h"

typedef struct
{
    float t_ref;
    bool t_ref_valid;
    float t_aux;
    bool t_aux_valid;
    float rh;
    float setpoint;
    bool heating;
    int priority; // active heating priority, 0 = relinquish default
    rc_mode_t mode;
    uint16_t status;
    uint32_t uptime_s;
} rc_points_t;

// MQTT state message, e.g.
// {"t_ref":21.84,"t_aux":22.10,"rh":47.2,"setpoint":22.0,"heating":true,
//  "priority":16,"mode":"auto","status":0,"uptime":86400}
// Missing measurements are encoded as null. Returns the length written, or -1
// if the buffer is too small.
int rc_points_to_json(const rc_points_t *p, char *buf, size_t len);

// True when the change is worth an immediate MQTT publish instead of waiting
// for the next periodic one (switching, setpoint, mode, status). Temperature
// drift alone is not.
bool rc_points_changed(const rc_points_t *a, const rc_points_t *b);

const char *rc_mode_name(rc_mode_t mode);

// Command payloads (not NUL-terminated, as MQTT delivers them).
// Setpoint: a number in °C, or "null" to relinquish the operator override.
// Mode: "auto", "off" or "on". Surrounding whitespace is ignored.
typedef enum
{
    RC_CMD_INVALID = 0,
    RC_CMD_VALUE,
    RC_CMD_RELINQUISH,
} rc_cmd_t;

rc_cmd_t rc_parse_setpoint(const char *payload, size_t len, float *celsius);
rc_cmd_t rc_parse_mode(const char *payload, size_t len, rc_mode_t *mode);
