#include "sensor_task.h"

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "room_state.h"
#include "shtc3.h"
#include "tmp117.h"

#define PERIOD_MS              1000
#define MAX_CONSECUTIVE_ERRORS 3
#define TASK_PRIORITY          5
#define TASK_STACK             4096

static const char *TAG = "sensors";

typedef struct
{
    i2c_master_bus_handle_t bus;
    tmp117_t tmp117;
    shtc3_t shtc3;
    bool tmp117_present;
    bool shtc3_present;
    uint8_t tmp117_consecutive;
    uint8_t shtc3_consecutive;
} sensor_ctx_t;

static sensor_ctx_t s_ctx;

static void count_result(bool ok, uint32_t *errors, uint8_t *consecutive)
{
    if (ok)
    {
        *consecutive = 0;
        return;
    }
    (*errors)++;
    if (*consecutive < MAX_CONSECUTIVE_ERRORS)
        (*consecutive)++;
}

static void sample(sensor_ctx_t *ctx, sensor_data_t *data)
{
    if (!ctx->tmp117_present && tmp117_init(ctx->bus, &ctx->tmp117) == ESP_OK)
    {
        ctx->tmp117_present = true;
        ESP_LOGI(TAG, "TMP117 found at 0x%02X", ctx->tmp117.addr);
    }
    if (!ctx->shtc3_present && shtc3_init(ctx->bus, &ctx->shtc3) == ESP_OK)
    {
        ctx->shtc3_present = true;
        ESP_LOGI(TAG, "SHTC3 found at 0x%02X", SHTC3_ADDR);
    }

    float t;
    data->tmp117_ok = ctx->tmp117_present && tmp117_read_celsius(&ctx->tmp117, &t) == ESP_OK &&
                      t > -55.0f && t < 150.0f;
    if (data->tmp117_ok)
        data->t_ref = t;

    float t_aux, rh;
    data->shtc3_ok = ctx->shtc3_present && shtc3_measure(&ctx->shtc3, &t_aux, &rh) == ESP_OK;
    if (data->shtc3_ok)
    {
        data->t_aux = t_aux;
        data->rh = rh;
    }

    count_result(data->tmp117_ok, &data->tmp117_errors, &ctx->tmp117_consecutive);
    count_result(data->shtc3_ok, &data->shtc3_errors, &ctx->shtc3_consecutive);
    data->tmp117_fault = ctx->tmp117_consecutive >= MAX_CONSECUTIVE_ERRORS;
    data->shtc3_fault = ctx->shtc3_consecutive >= MAX_CONSECUTIVE_ERRORS;
    data->samples++;
}

static void sensor_task(void *arg)
{
    sensor_ctx_t *ctx = arg;
    sensor_data_t data = {0};
    TickType_t last_wake = xTaskGetTickCount();

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    for (;;)
    {
        sample(ctx, &data);
        room_state_set_sensors(&data);
        esp_task_wdt_reset();
        // Fixed period, independent of how long the reads took
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PERIOD_MS));
    }
}

void sensor_task_start(i2c_master_bus_handle_t bus)
{
    s_ctx.bus = bus;
    xTaskCreatePinnedToCore(sensor_task, "sensor_task", TASK_STACK, &s_ctx, TASK_PRIORITY, NULL, 1);
}
