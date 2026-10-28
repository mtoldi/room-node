#include "setpoint_pot.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"

#define SAMPLES 16

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_channel;

esp_err_t setpoint_pot_init(int gpio)
{
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(gpio, &unit, &s_channel);
    if (err != ESP_OK)
        return err;
    if (unit != ADC_UNIT_1) // ADC2 is taken by the Wi-Fi driver
        return ESP_ERR_INVALID_ARG;

    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ADC_UNIT_1};
    err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK)
        return err;

    // 12 dB attenuation covers the full 0..3.3 V slider range (linear up to ~3.1 V)
    adc_oneshot_chan_cfg_t chan_cfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    err = adc_oneshot_config_channel(s_adc, s_channel, &chan_cfg);
    if (err != ESP_OK)
        return err;

    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .default_vref = 1100,
    };
    return adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali);
}

esp_err_t setpoint_pot_read_mv(int *mv)
{
    int sum = 0;
    for (int i = 0; i < SAMPLES; i++)
    {
        int raw;
        esp_err_t err = adc_oneshot_read(s_adc, s_channel, &raw);
        if (err != ESP_OK)
            return err;
        sum += raw;
    }
    return adc_cali_raw_to_voltage(s_cali, sum / SAMPLES, mv);
}
