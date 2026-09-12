#pragma once

#include <stdbool.h>

// Synchronise l'heure via NTP (pool.ntp.org) et met à jour
// le RTC DS3231 avec l'heure exacte obtenue.
// À appeler une fois après wifi_sender_init().
// Retourne true si la synchronisation a réussi,
// false si le serveur NTP était inaccessible (pas de Wi-Fi,
// ou timeout) — dans ce cas le RTC conserve son heure actuelle.
bool ntp_sync(void);