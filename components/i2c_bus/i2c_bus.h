#pragma once

#include "driver/i2c_master.h"

// Initialise le bus I2C partagé (à appeler une seule fois dans app_main).
void i2c_bus_init(void);

// Retourne le handle du bus, pour que chaque composant capteur
// puisse y ajouter son propre périphérique.
i2c_master_bus_handle_t i2c_bus_get_handle(void);
