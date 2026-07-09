#include "wifi_sender.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

// ---- Identifiants Wi-Fi en dur, comme convenu pour le prototypage ----
// À remplacer par les vrais identifiants de ton réseau domestique.
#define WIFI_SSID     "Bbox-63D53C88"
#define WIFI_PASSWORD "RCRcHnWF46akT9DN9p"

// Nom mDNS du serveur Python, doit correspondre exactement au
// SERVICE_NAME défini dans recevoir_mesures.py.
#define SERVER_HOSTNAME "station-receiver.local"
#define SERVER_PORT     5000
#define SERVER_PATH     "/upload"

static const char *TAG = "wifi_sender";

// Un "event group" FreeRTOS : une variable spéciale qui permet à une
// tâche d'attendre qu'un événement précis se produise dans une autre
// tâche, sans avoir à vérifier en boucle (polling). Ici, on l'utilise
// pour bloquer wifi_sender_init() jusqu'à ce que la connexion Wi-Fi
// soit réellement établie.
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

// ---- Fonction appelée automatiquement par le système à chaque
//      événement Wi-Fi ou réseau (connexion, déconnexion, IP obtenue) ----
static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // La couche Wi-Fi est prête : on lance la tentative de connexion.
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Connexion perdue : on retente automatiquement, indéfiniment.
        ESP_LOGW(TAG, "Wi-Fi deconnecte, nouvelle tentative...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        // Une adresse IP a été obtenue : la connexion est pleinement
        // fonctionnelle, on débloque wifi_sender_init().
        ESP_LOGI(TAG, "Wi-Fi connecte, IP obtenue");
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_sender_init(void)
{
    
    // NVS doit être initialisé avant esp_wifi_init, c'est une
    // dépendance implicite du driver Wi-Fi d'ESP-IDF.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // La partition NVS est corrompue ou d'une ancienne version :
        // on l'efface et on réinitialise proprement.
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_event_group = xEventGroupCreate();

    // ---- Initialisation de la pile réseau ESP-IDF ----
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));

    // Abonnement de notre event_handler à tous les événements Wi-Fi
    // et aux événements IP (obtention d'adresse).
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); // STA = station,
                                                        // l'ESP32 rejoint un
                                                        // réseau existant
                                                        // (par opposition au
                                                        // mode AP, où il en
                                                        // créerait un lui-même)
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connexion au Wi-Fi '%s' en cours...", WIFI_SSID);

    // Bloque ici jusqu'à ce que WIFI_CONNECTED_BIT soit positionné
    // par event_handler (donc jusqu'à connexion + IP obtenue).
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                         pdFALSE, pdTRUE, portMAX_DELAY);

    // ---- Initialisation mDNS côté ESP32, pour pouvoir RESOUDRE
    //      le nom du serveur Python (pas pour s'annoncer lui-même) ----
    ESP_ERROR_CHECK(mdns_init());
}

bool wifi_sender_upload_csv(const char *filepath)
{
    // ---- Étape 1 : résolution du nom mDNS du serveur en adresse IP ----
    // "station-receiver" sans le suffixe ".local", c'est l'API mdns qui
    // l'ajoute implicitement dans sa recherche.
    esp_ip4_addr_t target_ip;
    esp_err_t err = mdns_query_a("station-receiver", 3000, &target_ip);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Impossible de resoudre %s (le PC est-il allume et sur le meme reseau ?)",
                 SERVER_HOSTNAME);
        return false;
    }

    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&target_ip));
    ESP_LOGI(TAG, "Serveur trouve : %s -> %s", SERVER_HOSTNAME, ip_str);

    // ---- Étape 2 : lecture du fichier CSV depuis la carte SD ----
    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Impossible d'ouvrir %s", filepath);
        return false;
    }

    // On mesure la taille sans charger le fichier en RAM,
    // pour pouvoir la déclarer dans Content-Length.
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET); // retour au début pour la lecture

// ---- Envoi HTTP en streaming par blocs de 512 octets ----
    // On n'alloue qu'un petit tampon fixe, indépendamment
    // de la taille totale du fichier.
    char url[64];
    snprintf(url, sizeof(url), "http://%s:%d%s", ip_str, SERVER_PORT, SERVER_PATH);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000, // délai plus long pour les gros fichiers
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "text/csv");

// On déclare la taille exacte : Flask recevra un Content-Length
    // normal et ne tentera pas de décoder du chunked transfer.

    err = esp_http_client_open(client, file_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Echec ouverture connexion HTTP: %s", esp_err_to_name(err));
        fclose(f);
        esp_http_client_cleanup(client);
        return false;
    }

    // Tampon fixe de 512 octets : seule cette quantité de RAM
    // est utilisée, quelle que soit la taille du fichier.
    char chunk[512];
    size_t bytes_sent = 0;
    size_t bytes_read;

    while ((bytes_read = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        int written = esp_http_client_write(client, chunk, bytes_read);
        if (written < 0) {
            ESP_LOGE(TAG, "Erreur d'ecriture HTTP apres %d octets", bytes_sent);
            fclose(f);
            esp_http_client_cleanup(client);
            return false;
        }
        bytes_sent += written;
    }

    fclose(f);

    // Finalise la requête et récupère la réponse du serveur.
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Envoi termine : %d octets, code HTTP %d", bytes_sent, status);

    esp_http_client_cleanup(client);
    return (status == 200);
}
