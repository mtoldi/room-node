idf_component_register(
    SRCS "setpoint_pot.c" "led_bar.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_adc
)
