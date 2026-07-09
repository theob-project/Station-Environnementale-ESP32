#include "bh1750.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Adresse I2C confirmée sur ton schéma : ADDR relié à GND.
#define BH1750_ADDR 0x23

// Commande "One Time H-Resolution Mode" : déclenche une mesure
// unique, haute résolution (1 lux de précision), puis le capteur
// repasse automatiquement en veille (économie d'énergie).
#define BH1750_CMD_ONE_TIME_HIGH_RES 0x20

static const char *TAG = "bh1750";
static i2c_master_dev_handle_t dev_handle;

void bh1750_init(i2c_master_bus_handle_t bus_handle)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BH1750_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));
    ESP_LOGI(TAG, "BH1750 initialise, adresse 0x%02X", BH1750_ADDR);
}

float bh1750_read_lux(void)
{
    // Le BH1750 n'a pas de "registre" comme le BME280 : on lui envoie
    // directement une commande d'un seul octet, sans adresse de registre.
    uint8_t cmd = BH1750_CMD_ONE_TIME_HIGH_RES;
    ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, &cmd, 1, 1000));

    // Le mode haute résolution demande jusqu'à 180ms pour terminer
    // sa mesure interne avant qu'on puisse la lire. Sans cette pause,
    // on lirait une valeur incomplète ou laissée par la mesure précédente.
    vTaskDelay(pdMS_TO_TICKS(180));

    uint8_t data[2];
    ESP_ERROR_CHECK(i2c_master_receive(dev_handle, data, 2, 1000));

    // Le résultat brut est sur 16 bits, à diviser par 1.2 selon la
    // datasheet Bosch/ROHM pour obtenir une valeur en lux exacte.
    uint16_t raw = (data[0] << 8) | data[1];
    float lux = raw / 1.2f;

    return lux;
}