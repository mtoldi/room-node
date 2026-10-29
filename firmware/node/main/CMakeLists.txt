idf_component_register(
    SRCS "main.c" "room_state.c" "sensor_task.c" "control_task.c" "led_view.c" "sim_actuator.c"
         "wifi.c" "mqtt_task.c" "modbus_task.c" "bacnet_task.c"
    INCLUDE_DIRS "."
    REQUIRES roomcore bacnet sensors peripherals esp_driver_i2c esp_timer esp_wifi esp_netif esp_event nvs_flash lwip
)
