#include "control_task.h"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_view.h"
#include "room_state.h"
#include "setpoint_pot.h"

#define PERIOD_MS     200
#define TASK_PRIORITY 6
#define TASK_STACK    4096

static const char *TAG = "control";

static void control_task(void *arg)
{
    heating_actuator_t *actuator = arg;
    room_state_t s;
    int pot_mv = 0;
    TickType_t last_wake = xTaskGetTickCount();

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    for (;;)
    {
        setpoint_pot_read_mv(&pot_mv); // on a failed read the last value is reused
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        room_state_control_step(pot_mv, now_ms);
        room_state_get(&s);

        if (s.heating != actuator->is_on(actuator))
        {
            actuator->set(actuator, s.heating);
            ESP_LOGI(TAG, "heating %s (priority %d, t_ref %.2f, setpoint %.1f)", s.heating ? "ON" : "OFF",
                     s.heating_priority, s.t_ref, s.setpoint);
        }
        led_view_render(&s, now_ms);

        esp_task_wdt_reset();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PERIOD_MS));
    }
}

void control_task_start(heating_actuator_t *actuator)
{
    xTaskCreatePinnedToCore(control_task, "control_task", TASK_STACK, actuator, TASK_PRIORITY, NULL, 1);
}
