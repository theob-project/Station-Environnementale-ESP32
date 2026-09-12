#include "NTP_Sync.h"
#include "ds3231.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>

static const char *TAG = "ntp_sync";

// Fuseau horaire France métropolitaine (UTC+1 en hiver, UTC+2 en été).
// La chaîne "CET-1CEST,M3.5.0,M10.5.0/3" est le format POSIX TZ :
// CET  = Central European Time (nom hiver)
// -1   = offset UTC+1 (le signe est inversé en POSIX : -1 = +1h)
// CEST = Central European Summer Time (nom été)
// M3.5.0 = passage à l'heure d'été : mois 3 (mars), semaine 5
//           (dernière), jour 0 (dimanche)
// M10.5.0/3 = retour heure hiver : mois 10, dernière semaine,
//             dimanche à 3h du matin
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"

bool ntp_sync(void)
{
    ESP_LOGI(TAG, "Synchronisation NTP en cours...");

    // Configuration du fuseau horaire local.
    // setenv/tzset sont des fonctions C standard qui définissent
    // la variable d'environnement TZ utilisée par localtime()
    // pour convertir le timestamp UTC reçu de NTP en heure locale.
    // Sans ça, l'heure serait en UTC et décalée d'1h ou 2h selon
    // la saison.
    setenv("TZ", TIMEZONE, 1);
    tzset();

    // Initialisation du client SNTP d'ESP-IDF.
    // SNTP (Simple Network Time Protocol) est la version allégée
    // de NTP utilisée par les systèmes embarqués — même protocole,
    // implémentation plus légère.
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    // pool.ntp.org est un cluster de milliers de serveurs NTP
    // publics répartis dans le monde, gérés bénévolement.
    // fr.pool.ntp.org sélectionne automatiquement les serveurs
    // les plus proches géographiquement de la France.
    esp_sntp_setservername(0, "fr.pool.ntp.org");
    esp_sntp_setservername(1, "pool.ntp.org"); // serveur de secours
    esp_sntp_init();

    // Attente de la synchronisation, avec timeout de 30 secondes.
    // On vérifie toutes les 2 secondes si l'année est devenue
    // cohérente — c'est le signe que NTP a répondu et que
    // l'horloge système de l'ESP32 a été mise à jour.
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int MAX_RETRY = 15; // 15 x 2s = 30s maximum d'attente

    while (timeinfo.tm_year < (2024 - 1900) && retry < MAX_RETRY) {
        ESP_LOGI(TAG, "Attente reponse NTP... (%d/%d)", retry + 1, MAX_RETRY);
        vTaskDelay(pdMS_TO_TICKS(2000));

        time(&now);
        // localtime_r est la version thread-safe de localtime :
        // elle écrit le résultat dans le buffer "timeinfo" fourni
        // plutôt que dans un buffer statique partagé, ce qui est
        // indispensable dans un environnement FreeRTOS multitâche.
        localtime_r(&now, &timeinfo);
        retry++;
    }

    esp_sntp_stop();

    if (timeinfo.tm_year < (2024 - 1900)) {
        ESP_LOGW(TAG, "Synchronisation NTP echouee (timeout), "
                      "heure RTC conservee");
        return false;
    }

    // NTP a réussi : on écrit l'heure locale exacte dans le RTC.
    // Le RTC prendra ensuite le relais pour maintenir l'heure
    // précisément grâce à son oscillateur et sa pile CR2032,
    // même quand l'ESP32 est hors tension ou sans Wi-Fi.
    ds3231_set_time(&timeinfo);

    ESP_LOGI(TAG, "RTC synchronise : %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min,
             timeinfo.tm_sec);

    return true;
}