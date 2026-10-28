#pragma once

#include <stdint.h>

#include "esp_err.h"

// WS2812B stick driven through the RMT peripheral. Colours are given at full
// scale and dimmed to LED_BAR_BRIGHTNESS so ten LEDs stay well within USB current.
#define LED_BAR_BRIGHTNESS 51 // of 255, ~20 %

esp_err_t led_bar_init(int gpio, int count);
int led_bar_count(void);

// Buffers one pixel; nothing changes on the stick until led_bar_show().
void led_bar_set(int index, uint8_t r, uint8_t g, uint8_t b);
void led_bar_clear(void);
esp_err_t led_bar_show(void);
