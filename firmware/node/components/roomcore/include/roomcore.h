#pragma once

// Room controller logic: setpoint handling, thermostat, safety rules and the
// optional room simulation. Plain C without ESP-IDF or FreeRTOS dependencies;
// time is passed in, so the same code runs on the node and in host tests.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "priority_array.h"

#define RC_SETPOINT_MIN 16.0f
#define RC_SETPOINT_MAX 26.0f

// Heating output priorities, following the BACnet convention
#define RC_PRIO_FAILSAFE   1  // no valid temperature: off
#define RC_PRIO_FROST      5  // frost protection: on
#define RC_PRIO_MIN_TIME   6  // minimum on/off time after every change
#define RC_PRIO_OPERATOR   8  // mode on/off from BACnet, Modbus or MQTT
#define RC_PRIO_THERMOSTAT 16 // hysteresis controller

// Status bits, identical to Modbus input register 5
#define RC_STATUS_TMP117_FAULT (1u << 0)
#define RC_STATUS_SHTC3_FAULT  (1u << 1)
#define RC_STATUS_MQTT_OFFLINE (1u << 2) // set by the network layer, never by roomcore
#define RC_STATUS_SIMULATION   (1u << 3)
#define RC_STATUS_FROST        (1u << 4)
#define RC_STATUS_MAX_RUNTIME  (1u << 5)
#define RC_STATUS_OVERRIDE     (1u << 6)

typedef enum
{
    RC_MODE_AUTO = 0,
    RC_MODE_OFF = 1,
    RC_MODE_ON = 2,
} rc_mode_t;

typedef enum
{
    RC_OK = 0,
    RC_ERR_RANGE = 1,
} rc_result_t;

typedef struct
{
    float t_tmp117;
    bool tmp117_valid; // false while the sensor is faulty or has not been read yet
    float t_shtc3;
    bool shtc3_valid;
    int pot_mv;
} rc_inputs_t;

typedef struct
{
    uint32_t min_switch_ms;
    bool simulation;

    priority_array_t heating;  // 0 = off, 1 = on
    priority_array_t setpoint; // relinquish default follows the potentiometer
    rc_mode_t mode;

    bool output;
    uint32_t output_since_ms;
    uint32_t last_step_ms;

    bool pot_init;
    float pot_filtered_mv;
    float pot_setpoint;
    float pot_at_override;

    float t_ref;
    bool t_ref_valid;
    bool model_init;
    float t_model;

    uint16_t status;
} rc_t;

// min_switch_ms is the minimum on and off time (5 min normally, 10 s in demo builds).
// The output starts off and is held off for min_switch_ms, so a node stuck in
// a reset loop cannot switch the heating rapidly.
void rc_init(rc_t *rc, uint32_t min_switch_ms, bool simulation, uint32_t now_ms);

// One control cycle. now_ms may wrap around; only differences are used.
void rc_step(rc_t *rc, const rc_inputs_t *in, uint32_t now_ms);

// Operator setpoint at priority 8. Values outside 16..26 °C are rejected.
// The override ends with rc_relinquish_setpoint() or when the potentiometer
// moves by more than 0.3 °C.
rc_result_t rc_write_setpoint(rc_t *rc, float celsius);
void rc_relinquish_setpoint(rc_t *rc);

rc_result_t rc_set_mode(rc_t *rc, rc_mode_t mode);

bool rc_heating(const rc_t *rc);
int rc_heating_priority(const rc_t *rc);
float rc_setpoint(const rc_t *rc);
float rc_pot_setpoint(const rc_t *rc);
float rc_t_ref(const rc_t *rc);
bool rc_t_ref_valid(const rc_t *rc);
rc_mode_t rc_mode(const rc_t *rc);
uint16_t rc_status(const rc_t *rc);

// Potentiometer voltage to setpoint, with dead zones at both ends of the slider.
float rc_pot_to_celsius(float mv);

// For host tests that allocate rc_t without knowing its layout
size_t rc_size(void);
