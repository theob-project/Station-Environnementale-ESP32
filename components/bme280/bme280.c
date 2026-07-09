#include "bme280.h"
#include "esp_log.h"
#include <math.h>

#define BME280_ADDR 0x76

static const char *TAG = "bme280";
static i2c_master_dev_handle_t dev_handle;

// ---- Coefficients de calibration lus depuis le capteur ----
// Bosch grave ces valeurs en usine, propres à chaque exemplaire.
// On les stocke dans une structure pour les garder groupées et
// les passer facilement aux fonctions de calcul plus bas.
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4, dig_H5;
    int8_t   dig_H6;
} bme280_calib_t;

static bme280_calib_t calib;
static int32_t t_fine; // valeur intermédiaire partagée entre compensation T et P

// ---- Écrit un octet dans un registre du capteur ----
static void write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, data, 2, 1000));
}

// ---- Lit plusieurs octets à partir d'un registre de départ ----
static void read_regs(uint8_t start_reg, uint8_t *buffer, size_t len)
{
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev_handle, &start_reg, 1, buffer, len, 1000));
}

// ---- Lit les coefficients de calibration internes du capteur ----
static void read_calibration(void)
{
    uint8_t buf1[26]; // registres 0x88 à 0xA1, calibration T et P
    read_regs(0x88, buf1, 26);

    // Chaque coefficient occupe 2 octets consécutifs (little-endian :
    // l'octet de poids faible arrive en premier). On reconstitue
    // le nombre 16 bits avec un décalage de bits (<<8) plus un OU binaire.
    calib.dig_T1 = (uint16_t)(buf1[1] << 8 | buf1[0]);
    calib.dig_T2 = (int16_t)(buf1[3] << 8 | buf1[2]);
    calib.dig_T3 = (int16_t)(buf1[5] << 8 | buf1[4]);

    calib.dig_P1 = (uint16_t)(buf1[7] << 8 | buf1[6]);
    calib.dig_P2 = (int16_t)(buf1[9] << 8 | buf1[8]);
    calib.dig_P3 = (int16_t)(buf1[11] << 8 | buf1[10]);
    calib.dig_P4 = (int16_t)(buf1[13] << 8 | buf1[12]);
    calib.dig_P5 = (int16_t)(buf1[15] << 8 | buf1[14]);
    calib.dig_P6 = (int16_t)(buf1[17] << 8 | buf1[16]);
    calib.dig_P7 = (int16_t)(buf1[19] << 8 | buf1[18]);
    calib.dig_P8 = (int16_t)(buf1[21] << 8 | buf1[20]);
    calib.dig_P9 = (int16_t)(buf1[23] << 8 | buf1[22]);

    calib.dig_H1 = buf1[25]; // un seul octet, pas de combinaison nécessaire

    uint8_t buf2[7]; // registres 0xE1 à 0xE7, calibration H restante
    read_regs(0xE1, buf2, 7);

    calib.dig_H2 = (int16_t)(buf2[1] << 8 | buf2[0]);
    calib.dig_H3 = buf2[2];
    // dig_H4 et dig_H5 sont encodés sur 12 bits chacun, répartis de
    // façon imbriquée sur 3 octets : c'est une particularité propre
    // au BME280, documentée précisément dans sa datasheet officielle.
    calib.dig_H4 = (int16_t)((buf2[3] << 4) | (buf2[4] & 0x0F));
    calib.dig_H5 = (int16_t)((buf2[5] << 4) | (buf2[4] >> 4));
    calib.dig_H6 = (int8_t)buf2[6];

    ESP_LOGI(TAG, "Coefficients de calibration lus");
}

void bme280_init(i2c_master_bus_handle_t bus_handle)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BME280_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));

    read_calibration();

    // Registre 0xF2 (ctrl_hum) : sur-échantillonnage humidité x1.
    // Doit être écrit AVANT 0xF4 pour être pris en compte (exigence Bosch).
    write_reg(0xF2, 0x01);

    // Registre 0xF4 (ctrl_meas) : sur-échantillonnage T x1, P x1, mode normal.
    write_reg(0xF4, 0x27);

    // Registre 0xF5 (config) : filtre désactivé, t_standby par défaut.
    write_reg(0xF5, 0x00);

    ESP_LOGI(TAG, "BME280 initialise, adresse 0x%02X", BME280_ADDR);
}

// ---- Compense la température brute en degrés Celsius ----
// Formule officielle Bosch, adaptée du C vers calcul flottant.
static float compensate_temperature(int32_t adc_T)
{
    double var1, var2, T;
    var1 = (((double)adc_T) / 16384.0 - ((double)calib.dig_T1) / 1024.0);
    var1 = var1 * ((double)calib.dig_T2);
    var2 = ((((double)adc_T) / 131072.0 - ((double)calib.dig_T1) / 8192.0) *
            (((double)adc_T) / 131072.0 - ((double)calib.dig_T1) / 8192.0)) *
           ((double)calib.dig_T3);
    t_fine = (int32_t)(var1 + var2); // sauvegardé pour la fonction pression
    T = (var1 + var2) / 5120.0;
    return (float)T;
}

// ---- Compense la pression brute en hPa ----
static float compensate_pressure(int32_t adc_P)
{
    double var1, var2, p;
    var1 = ((double)t_fine / 2.0) - 64000.0;
    var2 = var1 * var1 * ((double)calib.dig_P6) / 32768.0;
    var2 = var2 + var1 * ((double)calib.dig_P5) * 2.0;
    var2 = (var2 / 4.0) + (((double)calib.dig_P4) * 65536.0);
    var1 = (((double)calib.dig_P3) * var1 * var1 / 524288.0 +
            ((double)calib.dig_P2) * var1) / 524288.0;
    var1 = (1.0 + var1 / 32768.0) * ((double)calib.dig_P1);

    if (var1 == 0.0) {
        return 0; // évite une division par zéro si la calibration P1 est invalide
    }

    p = 1048576.0 - (double)adc_P;
    p = (p - (var2 / 4096.0)) * 6250.0 / var1;
    var1 = ((double)calib.dig_P9) * p * p / 2147483648.0;
    var2 = p * ((double)calib.dig_P8) / 32768.0;
    p = p + (var1 + var2 + ((double)calib.dig_P7)) / 16.0;

    return (float)(p / 100.0); // conversion Pa -> hPa
}

// ---- Compense l'humidité brute en %RH ----
static float compensate_humidity(int32_t adc_H)
{
    double var_H;
    var_H = (((double)t_fine) - 76800.0);
    var_H = (adc_H - (((double)calib.dig_H4) * 64.0 +
             ((double)calib.dig_H5) / 16384.0 * var_H)) *
            (((double)calib.dig_H2) / 65536.0 *
             (1.0 + ((double)calib.dig_H6) / 67108864.0 * var_H *
              (1.0 + ((double)calib.dig_H3) / 67108864.0 * var_H)));
    var_H = var_H * (1.0 - ((double)calib.dig_H1) * var_H / 524288.0);

    if (var_H > 100.0) var_H = 100.0;
    if (var_H < 0.0)   var_H = 0.0;

    return (float)var_H;
}

void bme280_read(float *temperature, float *humidity, float *pressure)
{
    uint8_t raw[8];
    read_regs(0xF7, raw, 8); // 0xF7 à 0xFE : pression, température, humidité brutes

    int32_t adc_P = (raw[0] << 12) | (raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_T = (raw[3] << 12) | (raw[4] << 4) | (raw[5] >> 4);
    int32_t adc_H = (raw[6] << 8)  |  raw[7];

    // L'ordre est important : la température doit être compensée
    // EN PREMIER, car elle remplit t_fine, utilisé ensuite par
    // les deux autres fonctions de compensation.
    *temperature = compensate_temperature(adc_T);
    *pressure    = compensate_pressure(adc_P);
    *humidity    = compensate_humidity(adc_H);
}