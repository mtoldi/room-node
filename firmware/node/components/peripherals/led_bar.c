#include "led_bar.h"

#include "led_strip.h"

static led_strip_handle_t s_strip;
static int s_count;
static bool s_mirrored;

esp_err_t led_bar_init(int gpio, int count, bool mirrored)
{
    s_mirrored = mirrored;
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = gpio,
        .max_leds = count,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK)
        return err;
    s_count = count;
    led_bar_clear();
    return led_bar_show();
}

int led_bar_count(void)
{
    return s_count;
}

static uint8_t dim(uint8_t c)
{
    return (uint16_t)c * LED_BAR_BRIGHTNESS / 255;
}

void led_bar_set(int index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index >= 0 && index < s_count)
        led_strip_set_pixel(s_strip, s_mirrored ? s_count - 1 - index : index, dim(r), dim(g), dim(b));
}

void led_bar_clear(void)
{
    for (int i = 0; i < s_count; i++)
        led_strip_set_pixel(s_strip, i, 0, 0, 0);
}

esp_err_t led_bar_show(void)
{
    return led_strip_refresh(s_strip);
}
