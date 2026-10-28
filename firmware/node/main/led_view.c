#include "led_view.h"

#include <math.h>

#include "led_bar.h"

#define CELSIUS_PER_LED 0.5f
#define BLINK_HALF_MS   500

void led_view_render(const room_state_t *s, uint32_t now_ms)
{
    int count = led_bar_count();
    bool blink_on = (now_ms / BLINK_HALF_MS) % 2 == 0;

    led_bar_clear();
    if (!s->t_ref_valid)
    {
        if (blink_on)
            for (int i = 0; i < count; i++)
                led_bar_set(i, 255, 0, 0);
        led_bar_show();
        return;
    }

    if (s->heating)
    {
        int lit = (int)ceilf((s->setpoint - s->t_ref) / CELSIUS_PER_LED);
        if (lit < 1)
            lit = 1;
        if (lit > count)
            lit = count;

        uint8_t r = 255, g = 100, b = 0; // thermostat
        if (s->status & RC_STATUS_FROST)
            g = b = 255;
        else if (s->mode == RC_MODE_ON)
            r = 160, g = 0, b = 255;
        for (int i = 0; i < lit; i++)
            led_bar_set(i, r, g, b);
    }
    else
        led_bar_set(0, 0, 60, 255);

    if ((s->status & (RC_STATUS_TMP117_FAULT | RC_STATUS_SHTC3_FAULT)) && blink_on)
        led_bar_set(count - 1, 255, 0, 0);
    led_bar_show();
}
