#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "esp_timer.h"

#include "i2c_bus.h"
#include "ds3231.h"
#include "bme280.h"
#include "bh1750.h"
#include "oled_ssd1306.h"
#include "sd_card.h"
#include "wifi_sender.h"
#include "NTP_Sync.h"


static const char *TAG = "main";

// Constantes de timing
#define SLEEP_DURATION_US    (5ULL * 60 * 1000000)  // 5 minutes en microsecondes
#define OLED_TIMEOUT_S        60                    // Extinction Oled au bout d'une minute
#define UPLOAD_INTERVAL_CYCLE 12                    // Envoi wifi toutes les heures

void app_main(void)
{
    // 1. Bus I2C partagé, une seule fois pour tout le projet.
    i2c_bus_init();
    i2c_master_bus_handle_t bus = i2c_bus_get_handle();

    // 2. Chaque module s'ajoute sur ce même bus.
    ds3231_init(bus);
    bme280_init(bus);
    bh1750_init(bus);
    oled_init(bus);
    sd_card_init();
    wifi_sender_init();
    

  if (!ntp_sync()) {
    ESP_LOGW(TAG, "Heure RTC conservee sans recalage NTP");
}
    int loop_count = 0;

    // 4. Boucle principale : relit l'heure toutes les 5 secondes.
    while (1) {
        struct tm now;
        ds3231_get_time(&now);

        ESP_LOGI(TAG, "Heure actuelle : %04d-%02d-%02d %02d:%02d:%02d",
                 now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
                 now.tm_hour, now.tm_min, now.tm_sec);
        
        float temperature, humidity, pressure, lux;
        bme280_read(&temperature, &humidity, &pressure);
        lux = bh1750_read_lux();
        ESP_LOGI(TAG, "T=%.2f C  H=%.2f %%  P=%.2f hPa L=%.2f lux",
                 temperature, humidity, pressure, lux);

        // ---- OLED ON ----
        oled_power_on();
        ESP_LOGI(TAG, "OLED initialise");
        
        char line1[24], line2[24], line3[24], line4[24];

        snprintf(line1, sizeof(line1), "T : %.1f\xB0" "c", temperature);
        snprintf(line2, sizeof(line2), "H : %.1f%%", humidity);
        snprintf(line3, sizeof(line3), "P : %.1f hPa", pressure);
        snprintf(line4, sizeof(line4), "L : %.1f lux", lux);

        oled_clear();
        oled_draw_text(0, 0, line1);
        oled_draw_text(0, 2, line2);
        oled_draw_text(0, 4, line3);
        oled_draw_text(0, 6, line4);
        oled_refresh();

        // Keep OLED on for 60 seconds
        vTaskDelay(pdMS_TO_TICKS(60000));

        // ---- OLED OFF ----
        oled_power_off();
        ESP_LOGI(TAG, "OLED eteint après 60 secondes");

        sd_card_log(&now, temperature, humidity, pressure, lux);

         loop_count++;
        if (loop_count >= UPLOAD_INTERVAL_CYCLE) {
            // On construit le chemin du fichier du jour à envoyer,
            // plutôt que d'utiliser un chemin fixe.
            char daily_path[48];
        sd_card_get_daily_path(&now, daily_path, sizeof(daily_path));
        
        esp_wifi_start();
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "Envoi horaire de %s...", daily_path);
        bool sent = wifi_sender_upload_csv(daily_path);
                        
        if (sent) {
                ESP_LOGI(TAG, "Envoi reussi");
        } else {
            ESP_LOGW(TAG, "Envoi echoue, nouvelle tentative dans 1h");
        }

        esp_wifi_stop();
        loop_count = 0;
        
        }

       // ---- Light Sleep 5 minutes ----
        // esp_sleep_enable_timer_wakeup configure le timer de réveil.
        // esp_light_sleep_start() met le CPU en veille immédiatement
        // et retourne automatiquement quand le timer expire —
        // le code reprend exactement ici, au tour de boucle suivant,
        // sans réinitialisation.
        esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
        ESP_LOGI(TAG, "Light Sleep 5 minutes...");
        esp_light_sleep_start();
        // ← Le programme reprend ici après 5 minutes
    }
}
