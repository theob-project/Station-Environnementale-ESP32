idf_component_register(SRCS "NTP_Sync.c"
                        INCLUDE_DIRS "."
                        REQUIRES esp_netif lwip ds3231)