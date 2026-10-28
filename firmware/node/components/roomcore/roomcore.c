#include "roomcore.h"

#include <math.h>

#define HYSTERESIS        0.3f
#define FROST_ON          7.0f
#define FROST_OFF         8.0f
#define MAX_RUNTIME_MS    (2u * 60 * 60 * 1000)

#define POT_MV_MIN        100.0f  // dead zone at the bottom of the slider
#define POT_MV_MAX        3000.0f // ESP32 ADC is non-linear above ~3.1 V
#define POT_FILTER_ALPHA  0.2f    // moving average, ~1 s at a 200 ms cycle
#define POT_RELEASE_DELTA 0.3f    // slider movement that ends an operator override
#define POT_DEADBAND      0.08f   // > half of the 0.1 °C step: noise cannot flip the rounding

// First-order room model for the demo: heating adds a fixed rate, losses pull
// the modelled temperature towards the measured one.
#define SIM_HEAT_RATE     0.02f // °C/s while heating
#define SIM_TAU_S         600.0f
#define SIM_MAX_DT_S      1.0f

static float round_tenth(float x)
{
    return roundf(x * 10.0f) / 10.0f;
}

float rc_pot_to_celsius(float mv)
{
    float x = (mv - POT_MV_MIN) / (POT_MV_MAX - POT_MV_MIN);
    if (x < 0.0f)
        x = 0.0f;
    if (x > 1.0f)
        x = 1.0f;
    return RC_SETPOINT_MIN + x * (RC_SETPOINT_MAX - RC_SETPOINT_MIN);
}

void rc_init(rc_t *rc, uint32_t min_switch_ms, bool simulation, uint32_t now_ms)
{
    *rc = (rc_t){
        .min_switch_ms = min_switch_ms,
        .simulation = simulation,
        .mode = RC_MODE_AUTO,
        .output_since_ms = now_ms,
        .last_step_ms = now_ms,
    };
    pa_init(&rc->heating, 0.0f);
    pa_init(&rc->setpoint, RC_SETPOINT_MIN);
    pa_write(&rc->heating, RC_PRIO_THERMOSTAT, 0.0f);
    pa_write(&rc->heating, RC_PRIO_MIN_TIME, 0.0f);
}

static void update_pot(rc_t *rc, int mv)
{
    if (!rc->pot_init)
        rc->pot_filtered_mv = (float)mv;
    else
        rc->pot_filtered_mv += POT_FILTER_ALPHA * ((float)mv - rc->pot_filtered_mv);

    float raw = rc_pot_to_celsius(rc->pot_filtered_mv);
    if (!rc->pot_init || fabsf(raw - rc->pot_setpoint) > POT_DEADBAND)
        rc->pot_setpoint = round_tenth(raw);
    rc->setpoint.relinquish_default = rc->pot_setpoint;

    if (!rc->pot_init)
    {
        rc->pot_at_override = rc->pot_setpoint;
        rc->pot_init = true;
    }
    // A hand on the slider wins over a remote override
    if (pa_is_set(&rc->setpoint, RC_PRIO_OPERATOR) &&
        fabsf(rc->pot_setpoint - rc->pot_at_override) > POT_RELEASE_DELTA)
        pa_relinquish(&rc->setpoint, RC_PRIO_OPERATOR);
}

static void update_temperature(rc_t *rc, const rc_inputs_t *in, uint32_t dt_ms)
{
    float measured;
    if (in->tmp117_valid)
        measured = in->t_tmp117;
    else if (in->shtc3_valid)
        measured = in->t_shtc3; // fallback, reported through the status bits
    else
    {
        rc->t_ref_valid = false;
        return;
    }

    if (rc->simulation)
    {
        if (!rc->model_init)
        {
            rc->t_model = measured;
            rc->model_init = true;
        }
        float dt = dt_ms / 1000.0f;
        if (dt > SIM_MAX_DT_S)
            dt = SIM_MAX_DT_S;
        float heat = rc->output ? SIM_HEAT_RATE : 0.0f;
        rc->t_model += ((measured - rc->t_model) / SIM_TAU_S + heat) * dt;
        measured = rc->t_model;
    }
    rc->t_ref = measured;
    rc->t_ref_valid = true;
}

static void update_heating(rc_t *rc, uint32_t now_ms)
{
    priority_array_t *h = &rc->heating;
    float t = rc->t_ref;

    if (!rc->t_ref_valid)
        pa_write(h, RC_PRIO_FAILSAFE, 0.0f);
    else
    {
        pa_relinquish(h, RC_PRIO_FAILSAFE);

        if (t < FROST_ON)
            pa_write(h, RC_PRIO_FROST, 1.0f);
        else if (t > FROST_OFF)
            pa_relinquish(h, RC_PRIO_FROST);

        float sp = pa_value(&rc->setpoint);
        if (t < sp - HYSTERESIS)
            pa_write(h, RC_PRIO_THERMOSTAT, 1.0f);
        else if (t > sp + HYSTERESIS)
            pa_write(h, RC_PRIO_THERMOSTAT, 0.0f);
    }

    // Minimum on/off time as BACnet defines it: every change of the output is
    // latched at priority 6 for the minimum time. Levels 1-5 still override it.
    if (pa_is_set(h, RC_PRIO_MIN_TIME) && now_ms - rc->output_since_ms >= rc->min_switch_ms)
        pa_relinquish(h, RC_PRIO_MIN_TIME);

    bool out = pa_value(h) > 0.5f;
    if (out != rc->output)
    {
        rc->output = out;
        rc->output_since_ms = now_ms;
        pa_write(h, RC_PRIO_MIN_TIME, out ? 1.0f : 0.0f);
    }
}

static void update_status(rc_t *rc, const rc_inputs_t *in, uint32_t now_ms)
{
    uint16_t s = 0;
    if (!in->tmp117_valid)
        s |= RC_STATUS_TMP117_FAULT;
    if (!in->shtc3_valid)
        s |= RC_STATUS_SHTC3_FAULT;
    if (rc->simulation)
        s |= RC_STATUS_SIMULATION;
    if (pa_is_set(&rc->heating, RC_PRIO_FROST))
        s |= RC_STATUS_FROST;
    if (rc->output && now_ms - rc->output_since_ms >= MAX_RUNTIME_MS)
        s |= RC_STATUS_MAX_RUNTIME; // alarm only, heating continues
    if (pa_is_set(&rc->setpoint, RC_PRIO_OPERATOR))
        s |= RC_STATUS_OVERRIDE;
    rc->status = s;
}

void rc_step(rc_t *rc, const rc_inputs_t *in, uint32_t now_ms)
{
    uint32_t dt_ms = now_ms - rc->last_step_ms;
    rc->last_step_ms = now_ms;

    update_pot(rc, in->pot_mv);
    update_temperature(rc, in, dt_ms);
    update_heating(rc, now_ms);
    update_status(rc, in, now_ms);
}

rc_result_t rc_write_setpoint(rc_t *rc, float celsius)
{
    // Written this way so that NaN is rejected too
    if (!(celsius >= RC_SETPOINT_MIN && celsius <= RC_SETPOINT_MAX))
        return RC_ERR_RANGE;
    pa_write(&rc->setpoint, RC_PRIO_OPERATOR, celsius);
    rc->pot_at_override = rc->pot_setpoint;
    rc->status |= RC_STATUS_OVERRIDE;
    return RC_OK;
}

void rc_relinquish_setpoint(rc_t *rc)
{
    pa_relinquish(&rc->setpoint, RC_PRIO_OPERATOR);
    rc->status &= ~RC_STATUS_OVERRIDE;
}

rc_result_t rc_set_mode(rc_t *rc, rc_mode_t mode)
{
    switch (mode)
    {
    case RC_MODE_AUTO:
        pa_relinquish(&rc->heating, RC_PRIO_OPERATOR);
        break;
    case RC_MODE_OFF:
        pa_write(&rc->heating, RC_PRIO_OPERATOR, 0.0f);
        break;
    case RC_MODE_ON:
        pa_write(&rc->heating, RC_PRIO_OPERATOR, 1.0f);
        break;
    default:
        return RC_ERR_RANGE;
    }
    rc->mode = mode;
    return RC_OK;
}

bool rc_heating(const rc_t *rc)
{
    return rc->output;
}

int rc_heating_priority(const rc_t *rc)
{
    return pa_active_priority(&rc->heating);
}

float rc_setpoint(const rc_t *rc)
{
    return pa_value(&rc->setpoint);
}

float rc_pot_setpoint(const rc_t *rc)
{
    return rc->pot_setpoint;
}

float rc_t_ref(const rc_t *rc)
{
    return rc->t_ref;
}

bool rc_t_ref_valid(const rc_t *rc)
{
    return rc->t_ref_valid;
}

rc_mode_t rc_mode(const rc_t *rc)
{
    return rc->mode;
}

uint16_t rc_status(const rc_t *rc)
{
    return rc->status;
}

size_t rc_size(void)
{
    return sizeof(rc_t);
}
