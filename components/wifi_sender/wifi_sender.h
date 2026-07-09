#pragma once

#include <stdbool.h>

// Initialise le Wi-Fi et se connecte au réseau configuré dans le .c.
// Bloque jusqu'à connexion réussie (ou réessaie indéfiniment).
void wifi_sender_init(void);

// Lit le fichier CSV sur la carte SD et l'envoie en HTTP POST vers
// le serveur configuré (résolu via mDNS). Retourne true si l'envoi
// a réussi, false sinon (le programme principal peut alors décider
// de réessayer plus tard sans bloquer toute la station).
bool wifi_sender_upload_csv(const char *filepath);