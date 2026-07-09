#pragma once

#include <time.h>
#include <stdbool.h>

// Initialise le bus SPI et monte le système de fichiers FAT32.
// Ne crée plus de fichier CSV ici : chaque fichier quotidien
// est créé automatiquement à la première écriture du jour.
void sd_card_init(void);

// Construit le chemin du fichier CSV du jour dans le buffer fourni.
// Ex : "/sdcard/mesures_2026-06-27.csv"
void sd_card_get_daily_path(const struct tm *date, char *path_buf, size_t buf_len);

// Ajoute une ligne de mesures au fichier du jour. Crée le fichier
// avec son en-tête si c'est la première mesure de la journée.
void sd_card_log(const struct tm *timestamp, float temperature,
                  float humidity, float pressure, float lux);