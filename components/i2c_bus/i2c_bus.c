#include "i2c_bus.h"
#include "esp_log.h"

#define I2C_SDA_PIN  4
#define I2C_SCL_PIN  5
#define I2C_FREQ_HZ  100000

static const char *TAG = "i2c_bus";

// "static" : cette variable n'est visible que dans ce fichier.
// Les autres composants ne peuvent pas y accéder directement,
// ils doivent passer par i2c_bus_get_handle().
static i2c_master_bus_handle_t bus_handle = NULL;

void i2c_bus_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_PIN,
        .sda_io_num = I2C_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));
    ESP_LOGI(TAG, "Bus I2C initialise sur SDA=%d, SCL=%d", I2C_SDA_PIN, I2C_SCL_PIN);
}

i2c_master_bus_handle_t i2c_bus_get_handle(void)
{
    return bus_handle;
}
