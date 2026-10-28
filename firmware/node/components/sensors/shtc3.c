#include "shtc3.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

// Commands, SHTC3 datasheet table 9
#define CMD_WAKEUP  0x3517
#define CMD_SLEEP   0xB098
#define CMD_ID      0xEFC8
#define CMD_MEASURE 0x7866 // normal mode, temperature first, no clock stretching

#define WAKEUP_US     240 // max wake-up time
#define MEASURE_MS    15  // max 12.1 ms + margin
#define SCL_SPEED_HZ  100000
#define TIMEOUT_MS    50

static esp_err_t command(i2c_master_dev_handle_t dev, uint16_t cmd)
{
    uint8_t buf[2] = {cmd >> 8, cmd & 0xFF};
    return i2c_master_transmit(dev, buf, sizeof(buf), TIMEOUT_MS);
}

static uint8_t crc8(const uint8_t *data)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < 2; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
    return crc;
}

// Reads n words (2 bytes + CRC each) and checks every CRC
static esp_err_t read_words(i2c_master_dev_handle_t dev, uint16_t *words, int n)
{
    uint8_t buf[6];
    esp_err_t err = i2c_master_receive(dev, buf, n * 3, TIMEOUT_MS);
    if (err != ESP_OK)
        return err;
    for (int i = 0; i < n; i++)
    {
        const uint8_t *w = &buf[i * 3];
        if (crc8(w) != w[2])
            return ESP_ERR_INVALID_CRC;
        words[i] = ((uint16_t)w[0] << 8) | w[1];
    }
    return ESP_OK;
}

static esp_err_t wakeup(i2c_master_dev_handle_t dev)
{
    esp_err_t err = command(dev, CMD_WAKEUP);
    esp_rom_delay_us(WAKEUP_US);
    return err;
}

esp_err_t shtc3_init(i2c_master_bus_handle_t bus, shtc3_t *out)
{
    if (i2c_master_probe(bus, SHTC3_ADDR, TIMEOUT_MS) != ESP_OK)
        return ESP_ERR_NOT_FOUND;

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHTC3_ADDR,
        .scl_speed_hz = SCL_SPEED_HZ,
    };
    i2c_master_dev_handle_t dev;
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &dev);
    if (err != ESP_OK)
        return err;

    uint16_t id = 0;
    err = wakeup(dev);
    if (err == ESP_OK)
        err = command(dev, CMD_ID);
    if (err == ESP_OK)
        err = read_words(dev, &id, 1);
    command(dev, CMD_SLEEP);

    if (err == ESP_OK && (id & 0x083F) != 0x0807) // SHTC3 chip signature
        err = ESP_ERR_NOT_FOUND;
    if (err != ESP_OK)
    {
        i2c_master_bus_rm_device(dev);
        return err;
    }
    out->dev = dev;
    return ESP_OK;
}

esp_err_t shtc3_measure(const shtc3_t *sensor, float *celsius, float *humidity)
{
    uint16_t raw[2];
    esp_err_t err = wakeup(sensor->dev);
    if (err == ESP_OK)
        err = command(sensor->dev, CMD_MEASURE);
    if (err == ESP_OK)
    {
        vTaskDelay(pdMS_TO_TICKS(MEASURE_MS));
        err = read_words(sensor->dev, raw, 2);
    }
    command(sensor->dev, CMD_SLEEP);
    if (err != ESP_OK)
        return err;

    *celsius = -45.0f + 175.0f * raw[0] / 65536.0f;
    *humidity = 100.0f * raw[1] / 65536.0f;
    return ESP_OK;
}
