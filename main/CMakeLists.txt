idf_component_register(SRCS "main.c"
                        INCLUDE_DIRS "."
                        REQUIRES i2c_bus ds3231 bme280 bh1750 oled_ssd1306 sd_card wifi_sender)
