#pragma once

#include "driver/i2c_master.h"

// Ajoute le BME280 sur le bus I2C donné et lit ses coefficients
// de calibration internes. À appeler une fois, après i2c_bus_init().
void bme280_init(i2c_master_bus_handle_t bus_handle);
 
// Lit une mesure complète et remplit les trois valeurs pointées.
// Les pointeurs permettent de "retourner" 3 valeurs en une seule fois,
// ce qu'une fonction C classique ne peut pas faire (un return = une seule valeur).
void bme280_read(float *temperature, float *humidity, float *pressure);