#pragma once

#include <time.h>
#include "driver/i2c_master.h"

// Ajoute le DS3231 sur le bus I2C donné. À appeler une fois,
// après i2c_bus_init(), en lui passant i2c_bus_get_handle().
void ds3231_init(i2c_master_bus_handle_t bus_handle);

// Règle l'heure du RTC à partir d'une structure tm standard du C.
void ds3231_set_time(const struct tm *time_to_set);

// Lit l'heure actuelle du RTC et la stocke dans la structure fournie.
void ds3231_get_time(struct tm *time_out);
