#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "i2c_bus.h"
#include "ds3231.h"
#include "bme280.h"
#include "bh1750.h"
#include "oled_ssd1306.h"
#include "sd_card.h"
#include "wifi_sender.h"

static const char *TAG = "main";

// Construit une struct tm à partir des macros __DATE__ et __TIME__
// (remplies automatiquement par le compilateur à la compilation).
static struct tm get_compile_time(void)
{
    char compile_date[] = __DATE__;
    char compile_time[] = __TIME__;

    struct tm t = {0};
    char month_str[4];
    int day, year, hour, min, sec;

    sscanf(compile_date, "%s %d %d", month_str, &day, &year);
    sscanf(compile_time, "%d:%d:%d", &hour, &min, &sec);

    const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                             "Jul","Aug","Sep","Oct","Nov","Dec"};
    int month = 0;
    for (int i = 0; i < 12; i++) {
        if (strncmp(month_str, months[i], 3) == 0) {
            month = i + 1;
            break;
        }
    }

    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = min;
    t.tm_sec  = sec;
    t.tm_isdst = -1;

    time_t epoch = mktime(&t); // calcule notamment tm_wday automatiquement
    return *localtime(&epoch);
}

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
    

    // 3. Règle l'heure une seule fois, au premier démarrage.
    struct tm compile_time = get_compile_time();
    ds3231_set_time(&compile_time);

    vTaskDelay(pdMS_TO_TICKS(500));

     // Compteur de tours de boucle, pour déclencher l'envoi Wi-Fi
    // toutes les heures sans bloquer la boucle de mesure toutes les
    // 5 secondes. 3600 secondes / 5 secondes par tour = 720 tours.
    int loop_count = 0;
    const int LOOPS_PER_UPLOAD = 720;

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

        sd_card_log(&now, temperature, humidity, pressure, lux);

         loop_count++;
        if (loop_count >= LOOPS_PER_UPLOAD) {
            // On construit le chemin du fichier du jour à envoyer,
            // plutôt que d'utiliser un chemin fixe.
            char daily_path[48];
        sd_card_get_daily_path(&now, daily_path, sizeof(daily_path));

            ESP_LOGI(TAG, "Envoi horaire de %s...", daily_path);
            bool sent = wifi_sender_upload_csv(daily_path);
            if (sent) {
                ESP_LOGI(TAG, "Envoi reussi");
            } else {
                ESP_LOGW(TAG, "Envoi echoue, nouvelle tentative dans 1h");
            }
            loop_count = 0; // remise à zéro, qu'il ait réussi ou échoué
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
