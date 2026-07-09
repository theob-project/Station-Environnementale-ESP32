#include "ds3231.h"
#include "esp_log.h"

#define DS3231_ADDR  0x68

static const char *TAG = "ds3231";
static i2c_master_dev_handle_t dev_handle;

static uint8_t dec_to_bcd(int val)
{
    return (uint8_t)(((val / 10) << 4) | (val % 10));
}

static int bcd_to_dec(uint8_t val)
{
    return (val & 0x0F) + ((val >> 4) * 10);
}

void ds3231_init(i2c_master_bus_handle_t bus_handle)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));
    ESP_LOGI(TAG, "DS3231 ajoute sur le bus, adresse 0x%02X", DS3231_ADDR);
}

void ds3231_set_time(const struct tm *t)
{
    int wday = (t->tm_wday == 0) ? 7 : t->tm_wday; // dimanche=0 en C -> 7 pour le DS3231

    uint8_t data[8];
    data[0] = 0x00; // registre de départ : secondes
    data[1] = dec_to_bcd(t->tm_sec);
    data[2] = dec_to_bcd(t->tm_min);
    data[3] = dec_to_bcd(t->tm_hour);
    data[4] = dec_to_bcd(wday);
    data[5] = dec_to_bcd(t->tm_mday);
    data[6] = dec_to_bcd(t->tm_mon + 1);          // tm_mon va de 0 à 11 en C, le DS3231 attend 1 à 12
    data[7] = dec_to_bcd((t->tm_year + 1900) - 2000); // tm_year = années depuis 1900

    ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, data, sizeof(data), 1000));
    ESP_LOGI(TAG, "Heure ecrite dans le RTC");
}

void ds3231_get_time(struct tm *t)
{
    uint8_t reg = 0x00;
    uint8_t data[7];

    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev_handle, &reg, 1, data, sizeof(data), 1000));

    t->tm_sec  = bcd_to_dec(data[0]);
    t->tm_min  = bcd_to_dec(data[1]);
    t->tm_hour = bcd_to_dec(data[2] & 0x3F);     // masque les bits de mode 12h/24h si présents
    t->tm_wday = bcd_to_dec(data[3]) % 7;        // remet 7 -> 0 pour respecter la convention C
    t->tm_mday = bcd_to_dec(data[4]);
    t->tm_mon  = bcd_to_dec(data[5] & 0x1F) - 1; // remet 1-12 -> 0-11 pour la convention C
    t->tm_year = (2000 + bcd_to_dec(data[6])) - 1900;
}
