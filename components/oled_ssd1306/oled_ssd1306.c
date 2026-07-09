#include "oled_ssd1306.h"
#include "esp_log.h"
#include <string.h>

#define OLED_ADDR    0x3C
#define OLED_WIDTH   128
#define OLED_HEIGHT  64
// Taille du framebuffer : le SSD1306 range sa mémoire en "pages"
// de 8 pixels de hauteur chacune. 64 pixels de haut = 8 pages.
// 128 colonnes x 8 pages = 1024 octets, chaque octet représentant
// une colonne de 8 pixels verticaux (1 bit = 1 pixel).
#define OLED_PAGES   (OLED_HEIGHT / 8)
#define OLED_BUF_LEN (OLED_WIDTH * OLED_PAGES)

static const char *TAG = "oled";
static i2c_master_dev_handle_t dev_handle;
static uint8_t framebuffer[OLED_BUF_LEN];

// ---- Police de caractères minimale, 5 colonnes x 8 lignes par caractère ----
// Chaque caractère est un tableau de 5 octets. Chaque octet représente
// une colonne verticale de pixels (bit 0 = pixel du haut).
// On ne couvre ici que les caractères utiles à l'affichage de mesures :
// chiffres, lettres pour les unités, symboles ":", ".", "-", espace.
typedef struct {
    char c;
    uint8_t cols[5];
} font_glyph_t;

static const font_glyph_t font[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00}},
    {'-', {0x00,0x00,0x08,0x00,0x00}},
    {':', {0x00,0x36,0x36,0x00,0x00}},
    {'.', {0x00,0x60,0x60,0x00,0x00}},
    {'%', {0x62,0x64,0x08,0x13,0x23}},
    {0xB0, {0x06,0x09,0x09,0x06,0x00}}, // symbole degré °
    {'0', {0x3E,0x51,0x49,0x45,0x3E}},
    {'1', {0x00,0x42,0x7F,0x40,0x00}},
    {'2', {0x42,0x61,0x51,0x49,0x46}},
    {'3', {0x21,0x41,0x45,0x4B,0x31}},
    {'4', {0x18,0x14,0x12,0x7F,0x10}},
    {'5', {0x27,0x45,0x45,0x45,0x39}},
    {'6', {0x3C,0x4A,0x49,0x49,0x30}},
    {'7', {0x01,0x71,0x09,0x05,0x03}},
    {'8', {0x36,0x49,0x49,0x49,0x36}},
    {'9', {0x06,0x49,0x49,0x29,0x1E}},
    {'C', {0x3E,0x41,0x41,0x41,0x22}},
    {'H', {0x7F,0x08,0x08,0x08,0x7F}},
    {'L', {0x7F,0x40,0x40,0x40,0x40}},
    {'P', {0x7F,0x09,0x09,0x09,0x06}},
    {'T', {0x01,0x01,0x7F,0x01,0x01}},
    {'a', {0x20,0x54,0x54,0x54,0x78}},
    {'c', {0x38,0x44,0x44,0x44,0x20}},
    {'h', {0x7F,0x10,0x08,0x08,0x70}},
    {'l', {0x00,0x41,0x7F,0x40,0x00}},
    {'u', {0x3C,0x40,0x40,0x20,0x7C}},
    {'x', {0x44,0x28,0x10,0x28,0x44}},
};
#define FONT_COUNT (sizeof(font) / sizeof(font[0]))

// ---- Trouve les colonnes d'un caractère dans la police ----
static const uint8_t *find_glyph(char c)
{
    for (size_t i = 0; i < FONT_COUNT; i++) {
        if (font[i].c == c) {
            return font[i].cols;
        }
    }
    return font[0].cols; // caractère inconnu -> espace blanc, pas de plantage
}

// ---- Envoie une commande de configuration au contrôleur SSD1306 ----
static void send_command(uint8_t cmd)
{
    // 0x00 en premier octet = "ce qui suit est une commande" (convention SSD1306).
    uint8_t buf[2] = {0x00, cmd};
    ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, buf, 2, 1000));
}

void oled_init(i2c_master_bus_handle_t bus_handle)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));

    // Séquence d'initialisation standard du SSD1306 (issue de sa datasheet).
    send_command(0xAE); // affichage OFF pendant la config
    send_command(0xD5); send_command(0x80); // horloge interne
    send_command(0xA8); send_command(OLED_HEIGHT - 1); // multiplex ratio
    send_command(0xD3); send_command(0x00); // offset d'affichage
    send_command(0x40); // ligne de départ = 0
    send_command(0x8D); send_command(0x14); // active le régulateur de charge interne
    send_command(0x20); send_command(0x00); // mode d'adressage horizontal
    send_command(0xA1); // miroir horizontal (selon orientation du module)
    send_command(0xC8); // miroir vertical (selon orientation du module)
    send_command(0xDA); send_command(0x12); // configuration des pins COM
    send_command(0x81); send_command(0x7F); // contraste moyen
    send_command(0xD9); send_command(0xF1); // pré-charge
    send_command(0xDB); send_command(0x40); // niveau VCOMH
    send_command(0xA4); // affiche le contenu de la RAM (pas un test plein écran)
    send_command(0xA6); // mode normal (pas inversé)
    send_command(0xAF); // affichage ON

    oled_clear();
    oled_refresh();

    ESP_LOGI(TAG, "OLED initialise, adresse 0x%02X", OLED_ADDR);
}

void oled_clear(void)
{
    memset(framebuffer, 0x00, OLED_BUF_LEN);
}

void oled_draw_text(uint8_t col, uint8_t line, const char *text)
{
    // "line" correspond à une page de 8 pixels de haut (0 à 7 pour un écran 64px).
    uint16_t page_offset = line * OLED_WIDTH;

    while (*text != '\0') {
        const uint8_t *glyph = find_glyph(*text);

        // Copie les 5 colonnes du caractère dans le framebuffer,
        // à la bonne position horizontale (col) et verticale (page_offset).
        for (int i = 0; i < 5; i++) {
            if (col + i < OLED_WIDTH) {
                framebuffer[page_offset + col + i] = glyph[i];
            }
        }

        col += 7; // 5 pixels de caractère + 2 pixels d'espacement
        text++;
    }
}

void oled_refresh(void)
{
    // Redéfinit la zone d'écriture sur l'écran entier avant l'envoi,
    // pour être sûr que les données arrivent au bon endroit.
    send_command(0x21); send_command(0); send_command(OLED_WIDTH - 1);  // colonnes
    send_command(0x22); send_command(0); send_command(OLED_PAGES - 1);  // pages

    // L'envoi de données (par opposition aux commandes) se fait avec
    // 0x40 comme premier octet, convention du SSD1306.
    uint8_t buf[OLED_BUF_LEN + 1];
    buf[0] = 0x40;
    memcpy(&buf[1], framebuffer, OLED_BUF_LEN);

    ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, buf, sizeof(buf), 1000));
}