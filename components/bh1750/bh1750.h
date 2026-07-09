#pragma once

#include "driver/i2c_master.h"

// Ajoute le BH1750 sur le bus I2C donné. À appeler une fois,
// après i2c_bus_init().
void bh1750_init(i2c_master_bus_handle_t bus_handle);

// Lit le niveau de luminosité actuel, en lux.
float bh1750_read_lux(void);