#include <stdio.h>

#include "control_task.h"
#include "driver/i2c_master.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "heating_actuator.h"
#include "led_bar.h"
#include "modbus_task.h"
#include "mqtt_task.h"
#include "nvs_flash.h"
#include "room_state.h"
#include "sensor_task.h"
#include "setpoint_pot.h"
#include "shtc3.h"
#include "tmp117.h"
#include "wifi.h"

#define I2C_PROBE_TIMEOUT_MS 20
#define PRINT_PERIOD_MS      1000

#if CONFIG_ROOMNODE_DEMO
#define MIN_SWITCH_MS (10 * 1000)
#define SIMULATION    true
#else
#define MIN_SWITCH_MS (5 * 60 * 1000)
#define SIMULATION    false
#endif

static const char *TAG = "roomnode";

static i2c_master_bus_handle_t i2c_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_ROOMNODE_I2C_SDA,
        .scl_io_num = CONFIG_ROOMNODE_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
    return bus;
}

static void i2c_scan(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "I2C scan (SDA=IO%d, SCL=IO%d):", CONFIG_ROOMNODE_I2C_SDA, CONFIG_ROOMNODE_I2C_SCL);
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++)
    {
        if (i2c_master_probe(bus, addr, I2C_PROBE_TIMEOUT_MS) != ESP_OK)
            continue;
        const char *name = addr == SHTC3_ADDR                                          ? "SHTC3"
                           : (addr == TMP117_ADDR_DEFAULT || addr == TMP117_ADDR_ALT) ? "TMP117"
                                                                                       : "?";
        ESP_LOGI(TAG, "  0x%02X  %s", addr, name);
        found++;
    }
    if (found == 0)
        ESP_LOGW(TAG, "  nothing found, check the easyC cables");
}

// Temporary serial output until MQTT and the panel take over
static void print_state(const room_state_t *s)
{
    static const char *modes[] = {"auto", "off", "on"};
    const sensor_data_t *d = &s->sensors;

    printf("%6lus | ", (unsigned long)s->uptime_s);
    if (d->tmp117_ok)
        printf("TMP117 %6.2f | ", d->t_ref);
    else
        printf("TMP117   ---  | ");
    if (d->shtc3_ok)
        printf("SHTC3 %6.2f %5.1f%% | ", d->t_aux, d->rh);
    else
        printf("SHTC3   ---         | ");
    if (s->t_ref_valid)
        printf("t_ref %6.2f | ", s->t_ref);
    else
        printf("t_ref   ---  | ");
    printf("sp %4.1f%s | heat %-3s p%-2d | %-4s | status 0x%02X\n", s->setpoint,
           (s->status & RC_STATUS_OVERRIDE) ? "*" : " ", s->heating ? "ON" : "off", s->heating_priority,
           modes[s->mode], s->status);
}

void app_main(void)
{
    ESP_LOGI(TAG, "RoomNode starting%s", SIMULATION ? " (demo: simulated room, 10 s switching)" : "");
    room_state_init(MIN_SWITCH_MS, SIMULATION);

    i2c_master_bus_handle_t bus = i2c_init();
    i2c_scan(bus);
    sensor_task_start(bus);

    ESP_ERROR_CHECK(setpoint_pot_init(CONFIG_ROOMNODE_POT_GPIO));
    ESP_ERROR_CHECK(led_bar_init(CONFIG_ROOMNODE_LED_GPIO, CONFIG_ROOMNODE_LED_COUNT));
    control_task_start(sim_actuator_get());

    // Network last: the room is controlled locally whether or not it comes up
    esp_err_t err = nvs_flash_init(); // Wi-Fi driver keeps calibration data here
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (wifi_start())
    {
        mqtt_task_start();
        modbus_task_start();
    }

    room_state_t state;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(PRINT_PERIOD_MS));
        room_state_get(&state);
        print_state(&state);
    }
}
