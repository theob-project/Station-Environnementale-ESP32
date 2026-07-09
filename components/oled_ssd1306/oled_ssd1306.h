#pragma once

#include "driver/i2c_master.h"

// Ajoute l'écran OLED sur le bus I2C donné et l'initialise.
void oled_init(i2c_master_bus_handle_t bus_handle);

// Efface le contenu du framebuffer interne (ne touche pas encore
// l'écran physique, juste la mémoire tampon en RAM).
void oled_clear(void);

// Écrit du texte dans le framebuffer à la position (colonne en
// pixels, ligne de texte de 0 à 7 pour un écran de 64 pixels de haut).
void oled_draw_text(uint8_t col, uint8_t line, const char *text);

// Envoie le framebuffer entier vers l'écran physique. À appeler
// après un ou plusieurs oled_draw_text(), pour rendre les
// changements visibles d'un coup (évite un affichage qui clignote
// ligne par ligne).
void oled_refresh(void);