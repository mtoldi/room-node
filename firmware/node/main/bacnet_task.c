#include "bacnet_task.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rn_bacnet.h"
#include "room_state.h"

#define POLL_TIMEOUT_MS 1000
#define TASK_PRIORITY   4
#define TASK_STACK      6144 // RPM responses are assembled on the stack

static const char *TAG = "bacnet";

static rn_bacnet_config_t s_config = {
    .device_instance = CONFIG_ROOMNODE_BACNET_INSTANCE,
    .device_name = "RoomNode-" CONFIG_ROOMNODE_ID,
};

static void read_state(rc_points_t *points, priority_array_t *heating, priority_array_t *setpoint)
{
    room_state_t s;
    room_state_get_commandable(&s, heating, setpoint);
    room_state_points(&s, points);
}

static rc_result_t write_setpoint(float celsius)
{
    rc_result_t r = room_state_write_setpoint(celsius);
    ESP_LOGI(TAG, "setpoint %.2f %s", celsius, r == RC_OK ? "written at priority 8" : "rejected");
    return r;
}

static void relinquish_setpoint(void)
{
    ESP_LOGI(TAG, "setpoint relinquished at priority 8");
    room_state_relinquish_setpoint();
}

static rc_result_t set_mode(rc_mode_t mode)
{
    ESP_LOGI(TAG, "mode %s", rc_mode_name(mode));
    return room_state_set_mode(mode);
}

static const rn_bacnet_access_t s_access = {
    .read = read_state,
    .write_setpoint = write_setpoint,
    .relinquish_setpoint = relinquish_setpoint,
    .set_mode = set_mode,
};

static bool has_ip(void)
{
    esp_netif_ip_info_t ip;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    return netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

static void bacnet_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    while (!has_ip() || !rn_bacnet_start(&s_config, &s_access))
    {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "device %lu \"%s\" ready", (unsigned long)s_config.device_instance, s_config.device_name);

    for (;;)
    {
        rn_bacnet_poll(POLL_TIMEOUT_MS);
        esp_task_wdt_reset();
    }
}

void bacnet_task_start(uint32_t min_switch_ms)
{
    s_config.min_switch_s = min_switch_ms / 1000;
    xTaskCreatePinnedToCore(bacnet_task, "bacnet_task", TASK_STACK, NULL, TASK_PRIORITY, NULL, 1);
}
