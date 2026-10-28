#include "tmp117.h"

#define REG_TEMPERATURE 0x00
#define REG_DEVICE_ID   0x0F
#define DEVICE_ID       0x0117
#define RESET_VALUE     ((int16_t)0x8000) // -256 °C until the first conversion is done
#define LSB_CELSIUS     0.0078125f        // 7.8125 m°C per bit
#define SCL_SPEED_HZ    100000
#define TIMEOUT_MS      50

static esp_err_t read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t *value)
{
    uint8_t buf[2];
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, buf, sizeof(buf), TIMEOUT_MS);
    if (err == ESP_OK)
        *value = ((uint16_t)buf[0] << 8) | buf[1];
    return err;
}

esp_err_t tmp117_init(i2c_master_bus_handle_t bus, tmp117_t *out)
{
    const uint8_t candidates[] = {TMP117_ADDR_DEFAULT, TMP117_ADDR_ALT};

    for (size_t i = 0; i < sizeof(candidates); i++)
    {
        uint8_t addr = candidates[i];
        if (i2c_master_probe(bus, addr, TIMEOUT_MS) != ESP_OK)
            continue;

        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = SCL_SPEED_HZ,
        };
        i2c_master_dev_handle_t dev;
        esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &dev);
        if (err != ESP_OK)
            return err;

        uint16_t id;
        if (read_reg(dev, REG_DEVICE_ID, &id) == ESP_OK && (id & 0x0FFF) == DEVICE_ID)
        {
            out->dev = dev;
            out->addr = addr;
            return ESP_OK;
        }
        i2c_master_bus_rm_device(dev); // something else lives at this address
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t tmp117_read_celsius(const tmp117_t *sensor, float *celsius)
{
    uint16_t raw;
    esp_err_t err = read_reg(sensor->dev, REG_TEMPERATURE, &raw);
    if (err != ESP_OK)
        return err;
    if ((int16_t)raw == RESET_VALUE)
        return ESP_ERR_INVALID_STATE;
    *celsius = (int16_t)raw * LSB_CELSIUS;
    return ESP_OK;
}
