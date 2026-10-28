#include "mqtt_task.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "points.h"
#include "room_state.h"

#define PUBLISH_PERIOD_MS 10000
#define CHECK_PERIOD_MS   500
#define KEEPALIVE_S       15 // the broker publishes the Last Will after 1.5x this
#define TASK_PRIORITY     3
#define TASK_STACK        4096
#define TOPIC_PREFIX      "roomnode/" CONFIG_ROOMNODE_ID

static const char *TAG = "mqtt";

static const char TOPIC_STATE[] = TOPIC_PREFIX "/state";
static const char TOPIC_STATUS[] = TOPIC_PREFIX "/status";
static const char TOPIC_CMD_SETPOINT[] = TOPIC_PREFIX "/cmd/setpoint";
static const char TOPIC_CMD_MODE[] = TOPIC_PREFIX "/cmd/mode";

static esp_mqtt_client_handle_t s_client;
static volatile bool s_connected;
static volatile bool s_publish_now;

static bool topic_is(const esp_mqtt_event_t *e, const char *topic)
{
    return e->topic_len == (int)strlen(topic) && memcmp(e->topic, topic, e->topic_len) == 0;
}

static void handle_setpoint(const char *data, int len)
{
    float celsius;
    switch (rc_parse_setpoint(data, len, &celsius))
    {
    case RC_CMD_VALUE:
        if (room_state_write_setpoint(celsius) == RC_OK)
            ESP_LOGI(TAG, "setpoint override %.2f", celsius);
        else
            ESP_LOGW(TAG, "setpoint %.2f rejected, valid range %.0f..%.0f", celsius, RC_SETPOINT_MIN, RC_SETPOINT_MAX);
        break;
    case RC_CMD_RELINQUISH:
        room_state_relinquish_setpoint();
        ESP_LOGI(TAG, "setpoint override relinquished");
        break;
    default:
        ESP_LOGW(TAG, "invalid setpoint payload \"%.*s\"", len, data);
    }
}

static void handle_mode(const char *data, int len)
{
    rc_mode_t mode;
    if (rc_parse_mode(data, len, &mode) == RC_CMD_VALUE && room_state_set_mode(mode) == RC_OK)
        ESP_LOGI(TAG, "mode %s", rc_mode_name(mode));
    else
        ESP_LOGW(TAG, "invalid mode payload \"%.*s\"", len, data);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t e = data;
    switch ((esp_mqtt_event_id_t)id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to %s", CONFIG_ROOMNODE_MQTT_URI);
        esp_mqtt_client_publish(s_client, TOPIC_STATUS, "online", 0, 1, 1);
        esp_mqtt_client_subscribe_single(s_client, TOPIC_CMD_SETPOINT, 1);
        esp_mqtt_client_subscribe_single(s_client, TOPIC_CMD_MODE, 1);
        s_connected = true;
        s_publish_now = true;
        room_state_set_mqtt_online(true);
        break;
    case MQTT_EVENT_DISCONNECTED:
        if (s_connected)
            ESP_LOGW(TAG, "disconnected");
        s_connected = false;
        room_state_set_mqtt_online(false);
        break;
    case MQTT_EVENT_DATA:
        if (e->data_len != e->total_data_len) // commands are tiny; ignore fragments
            break;
        if (topic_is(e, TOPIC_CMD_SETPOINT))
            handle_setpoint(e->data, e->data_len);
        else if (topic_is(e, TOPIC_CMD_MODE))
            handle_mode(e->data, e->data_len);
        s_publish_now = true;
        break;
    default:
        break;
    }
}

static void mqtt_task(void *arg)
{
    (void)arg;
    rc_points_t last = {0};
    int64_t last_publish_us = 0;
    char json[256];
    room_state_t s;

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(CHECK_PERIOD_MS));
        esp_task_wdt_reset();
        if (!s_connected)
            continue;

        room_state_get(&s);
        rc_points_t p;
        room_state_points(&s, &p);
        int64_t now_us = esp_timer_get_time();
        bool due = now_us - last_publish_us >= (int64_t)PUBLISH_PERIOD_MS * 1000;
        if (!s_publish_now && !due && !rc_points_changed(&p, &last))
            continue;

        int len = rc_points_to_json(&p, json, sizeof(json));
        if (len > 0 && esp_mqtt_client_publish(s_client, TOPIC_STATE, json, len, 0, 1) >= 0)
        {
            last = p;
            last_publish_us = now_us;
            s_publish_now = false;
        }
    }
}

void mqtt_task_start(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_ROOMNODE_MQTT_URI,
        .credentials.client_id = "roomnode-" CONFIG_ROOMNODE_ID,
        .credentials.username = strlen(CONFIG_ROOMNODE_MQTT_USERNAME) ? CONFIG_ROOMNODE_MQTT_USERNAME : NULL,
        .credentials.authentication.password =
            strlen(CONFIG_ROOMNODE_MQTT_PASSWORD) ? CONFIG_ROOMNODE_MQTT_PASSWORD : NULL,
        .session.last_will = {.topic = TOPIC_STATUS, .msg = "offline", .qos = 1, .retain = 1},
        .session.keepalive = KEEPALIVE_S,
        .network.reconnect_timeout_ms = 5000,
    };
    s_client = esp_mqtt_client_init(&cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));
    xTaskCreatePinnedToCore(mqtt_task, "mqtt_task", TASK_STACK, NULL, TASK_PRIORITY, NULL, 1);
}
