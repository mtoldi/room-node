#include "wifi.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#define BACKOFF_MIN_MS 1000
#define BACKOFF_MAX_MS 60000

static const char *TAG = "wifi";

static esp_timer_handle_t s_retry_timer;
static uint32_t s_backoff_ms = BACKOFF_MIN_MS;

static void retry(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
        esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        const wifi_event_sta_disconnected_t *e = data;
        ESP_LOGW(TAG, "disconnected (reason %d), retrying in %lu s", e->reason,
                 (unsigned long)(s_backoff_ms / 1000));
        esp_timer_start_once(s_retry_timer, (uint64_t)s_backoff_ms * 1000);
        s_backoff_ms = s_backoff_ms * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : s_backoff_ms * 2;
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        const ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "connected, IP " IPSTR, IP2STR(&e->ip_info.ip));
        s_backoff_ms = BACKOFF_MIN_MS;
    }
}

bool wifi_start(void)
{
    if (strlen(CONFIG_ROOMNODE_WIFI_SSID) == 0)
    {
        ESP_LOGW(TAG, "no SSID configured, running without network (see sdkconfig.defaults.local.example)");
        return false;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, "roomnode-" CONFIG_ROOMNODE_ID);

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));

    esp_timer_create_args_t timer_args = {.callback = retry, .name = "wifi_retry"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_retry_timer));

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, CONFIG_ROOMNODE_WIFI_SSID, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, CONFIG_ROOMNODE_WIFI_PASSWORD, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = strlen(CONFIG_ROOMNODE_WIFI_PASSWORD) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "connecting to \"%s\"", CONFIG_ROOMNODE_WIFI_SSID);
    return true;
}
