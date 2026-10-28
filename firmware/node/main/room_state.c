#include "room_state.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_mutex;
static sensor_data_t s_sensors;
static rc_t s_rc;

static void lock(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_mutex);
}

void room_state_init(uint32_t min_switch_ms, bool simulation)
{
    s_mutex = xSemaphoreCreateMutex();
    configASSERT(s_mutex);
    rc_init(&s_rc, min_switch_ms, simulation, (uint32_t)(esp_timer_get_time() / 1000));
}

void room_state_set_sensors(const sensor_data_t *sensors)
{
    lock();
    s_sensors = *sensors;
    unlock();
}

// A sensor counts as valid once it has delivered at least one reading and is not faulty
static bool valid(uint32_t errors, uint32_t samples, bool fault)
{
    return !fault && errors < samples;
}

void room_state_control_step(int pot_mv, uint32_t now_ms)
{
    lock();
    rc_inputs_t in = {
        .t_tmp117 = s_sensors.t_ref,
        .tmp117_valid = valid(s_sensors.tmp117_errors, s_sensors.samples, s_sensors.tmp117_fault),
        .t_shtc3 = s_sensors.t_aux,
        .shtc3_valid = valid(s_sensors.shtc3_errors, s_sensors.samples, s_sensors.shtc3_fault),
        .pot_mv = pot_mv,
    };
    rc_step(&s_rc, &in, now_ms);
    unlock();
}

void room_state_get(room_state_t *out)
{
    lock();
    *out = (room_state_t){
        .sensors = s_sensors,
        .t_ref = rc_t_ref(&s_rc),
        .t_ref_valid = rc_t_ref_valid(&s_rc),
        .setpoint = rc_setpoint(&s_rc),
        .pot_setpoint = rc_pot_setpoint(&s_rc),
        .heating = rc_heating(&s_rc),
        .heating_priority = rc_heating_priority(&s_rc),
        .mode = rc_mode(&s_rc),
        .status = rc_status(&s_rc),
    };
    unlock();
    out->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
}

rc_result_t room_state_write_setpoint(float celsius)
{
    lock();
    rc_result_t r = rc_write_setpoint(&s_rc, celsius);
    unlock();
    return r;
}

void room_state_relinquish_setpoint(void)
{
    lock();
    rc_relinquish_setpoint(&s_rc);
    unlock();
}

rc_result_t room_state_set_mode(rc_mode_t mode)
{
    lock();
    rc_result_t r = rc_set_mode(&s_rc, mode);
    unlock();
    return r;
}
