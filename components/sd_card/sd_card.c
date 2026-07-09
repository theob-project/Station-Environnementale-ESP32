#include "sd_card.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include <stdio.h>

#define PIN_MISO  2
#define PIN_MOSI  7
#define PIN_SCLK  6
#define PIN_CS    10

// Point de montage : une fois monté, le système de fichiers FAT32
// de la carte SD devient accessible comme un dossier classique,
// exactement comme /home ou /tmp sur un PC Linux.
#define MOUNT_POINT "/sdcard"
#define CSV_PATH    MOUNT_POINT "/mesures.csv"

static const char *TAG = "sd_card";
static sdmmc_card_t *card; // représente la carte SD une fois montée

void sd_card_init(void)
{
    esp_err_t ret;

    // ---- Étape 1 : configuration du bus SPI dédié à la carte SD ----
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1, // -1 = non utilisé, la carte SD basique n'en a pas besoin
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    // SPI2_HOST est le deuxième contrôleur SPI matériel de l'ESP32-C3
    // (le premier, SPI1, est généralement réservé à la mémoire flash interne).
    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Echec d'initialisation du bus SPI: %s", esp_err_to_name(ret));
        return;
    }

    // ---- Étape 2 : configuration de la carte SD elle-même ----
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = PIN_CS;
    slot_cfg.host_id = SPI2_HOST;

    // ---- Étape 3 : montage du système de fichiers FAT32 ----
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false, // sécurité : ne formate pas une carte
                                          // qui contiendrait déjà des données
                                          // importantes par accident
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Montage echoue. La carte est-elle formatee en FAT32 ?");
        } else {
            ESP_LOGE(TAG, "Erreur carte SD (%s). Verifiez le cablage.", esp_err_to_name(ret));
        }
        return;
    }

    ESP_LOGI(TAG, "Carte SD montee avec succes sur %s", MOUNT_POINT);
}

void sd_card_get_daily_path(const struct tm *date, char *path_buf, size_t buf_len)
{
    snprintf(path_buf, buf_len, "%s/mesures_%04d-%02d-%02d.csv",
             MOUNT_POINT,
             date->tm_year + 1900,
             date->tm_mon + 1,
             date->tm_mday);
}

void sd_card_log(const struct tm *t, float temperature, float humidity,
                  float pressure, float lux)
{
    char filepath[48];
    sd_card_get_daily_path(t, filepath, sizeof(filepath));

    // Vérifie si le fichier existe déjà pour décider d'écrire
    // l'en-tête ou non — même technique qu'avant avec fopen "r".
    bool needs_header = false;
    FILE *check = fopen(filepath, "r");
    if (check == NULL) {
        needs_header = true;
    } else {
        fclose(check);
    }

    FILE *f = fopen(filepath, "a");
    if (f == NULL) {
        ESP_LOGE(TAG, "Impossible d'ouvrir %s", filepath);
        return;
    }

    if (needs_header) {
        fprintf(f, "timestamp,temperature,humidity,pressure,lux\n");
        ESP_LOGI(TAG, "Nouveau fichier quotidien : %s", filepath);
    }

    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d,%.2f,%.2f,%.2f,%.2f\n",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec,
            temperature, humidity, pressure, lux);

    fclose(f);
}