# Station Environnementale Connectée — Journal de projet

Projet réalisé sur ESP32-C3-DevKitM-1 avec ESP-IDF v6.0.1 sous VS Code (Linux).  
Architecture logicielle en composants séparés, protocoles I2C et SPI, Wi-Fi, stockage SD.

---

## 1. Matériel

### Composants retenus

| Rôle | Référence | Interface | Tension |
|---|---|---|---|
| Microcontrôleur | ESP32-C3-DevKitM-1 | — | 3.3V (alim 5V via USB) |
| Horloge RTC | Module DS3231 (GT584) | I2C 0x68 + EEPROM 0x57 | 3.3V |
| Capteur T/P/H | Module BME280 (VMA335) | I2C 0x76 | 3.3V |
| Capteur luminosité | Module BH1750 (ADA4681) | I2C 0x23 | 3.3V |
| Afficheur | OLED 0.96" SSD1306 (WPI438) | I2C 0x3C | 3.3V |
| Stockage | Module SD (GT126) + carte 8 Go | SPI | 3.3V |

### Câblage

- **Bus I2C partagé** : SDA → GPIO4, SCL → GPIO5
- **Bus SPI (carte SD)** : MISO → GPIO2, MOSI → GPIO7, SCLK → GPIO6, CS → GPIO10
- **Pull-up I2C** : 2x 2.2kΩ entre 3V3 et SDA/SCL (une seule paire pour tout le bus)

### Matériel de prototypage

- Plaque de montage rapide 2420 contacts BB3T5D (Gotronic 12304) montée sur support
- Pack de câbles Dupont M/M et F/F codés par couleur (rouge=3V3, noir=GND, orange=SCL, vert=SDA)

---

## 2. Architecture logicielle

Projet structuré en composants ESP-IDF séparés, chacun avec son `.c`, `.h` et `CMakeLists.txt`.

```
station_env/
├── main/
│   ├── main.c              ← orchestration générale
│   └── CMakeLists.txt
├── components/
│   ├── i2c_bus/            ← bus I2C partagé (init une seule fois)
│   ├── ds3231/             ← horloge RTC
│   ├── bme280/             ← capteur T/P/H avec compensation Bosch
│   ├── bh1750/             ← capteur luminosité
│   ├── oled_ssd1306/       ← afficheur OLED avec framebuffer
│   ├── sd_card/            ← stockage CSV sur carte SD (SPI)
│   └── wifi_sender/        ← connexion Wi-Fi + envoi HTTP
├── partitions.csv          ← table de partitions personnalisée
└── CMakeLists.txt
```

**Principe clé** : le bus I2C est initialisé une seule fois dans `i2c_bus`, et son handle est passé en paramètre à chaque composant capteur via `xxx_init(bus_handle)`. La carte SD utilise son propre bus SPI, indépendant du bus I2C.

---

## 3. Problèmes rencontrés et solutions

### 3.1 Noms de composants ESP-IDF v6.0

**Problème** : ESP-IDF v6.0 a renommé plusieurs composants par rapport aux versions antérieures. Le nom `driver` utilisé dans les `CMakeLists.txt` provoquait des erreurs de compilation.

**Solution** : remplacer `driver` par `esp_driver_i2c` dans les `REQUIRES` de tous les composants utilisant le bus I2C. Pour `mdns`, absent du core en v6.0, utiliser `idf.py add-dependency "espressif/mdns"` puis déclarer `espressif__mdns` (double underscore) dans `REQUIRES`.

```cmake
# Avant (ne fonctionne pas en v6.0)
REQUIRES driver

# Après
REQUIRES esp_driver_i2c
```

---

### 3.2 Casse du fichier CMakeLists.txt

**Problème** : fichier nommé `CMakelists.txt` au lieu de `CMakeLists.txt` — ESP-IDF est sensible à la casse sur Linux, le composant était ignoré silencieusement par le système de build avec un message d'erreur trompeur ("component could not be found").

**Solution** : renommer le fichier avec la casse exacte `CMakeLists.txt`.

---

### 3.3 Mauvais contacts sur breadboard

**Problème** : le module RTC n'était pas détecté sur le scan I2C malgré un câblage apparemment correct.

**Solution** : déplacer le module sur une autre zone de la breadboard — certaines colonnes ont des contacts défaillants (ressorts usés ou mal formés). La détection est revenue immédiatement après déplacement.

**Leçon** : en cas de non-détection I2C avec alimentation confirmée, tester l'isolement du module seul sur le bus, puis changer de zone de breadboard avant de suspecter le composant lui-même.

---

### 3.4 Inversion SDA/SCL sur le module OLED

**Problème** : après manipulation du câblage pour diagnostiquer le RTC, le module OLED n'était plus détecté sur le scan I2C, alors que son écran affichait toujours le dernier contenu (alimentation intacte).

**Diagnostic** : l'écran qui affiche toujours son contenu confirme que VCC et GND sont corrects — seule la communication I2C est rompue (SDA ou SCL). Cause identifiée : fils SDA et SCL inversés sur ce seul module.

**Solution** : remettre SDA et SCL dans le bon ordre sur le connecteur du module OLED.

---

### 3.5 Glyphe manquant dans la police OLED (caractères 'L', 'a', 'c', '°')

**Problème** : certains caractères n'apparaissaient pas à l'écran (`hPa` tronqué, `L` absent pour la luminosité). La police personnalisée implémentée dans `oled_ssd1306.c` ne couvrait pas tous les caractères nécessaires.

**Solution** : ajouter les glyphes manquants dans le tableau `font[]`. Chaque glyphe est un tableau de 5 octets représentant 5 colonnes de 8 pixels verticaux (bit 0 = pixel du haut).

```c
{'L', {0x7F,0x40,0x40,0x40,0x40}},
{'a', {0x20,0x54,0x54,0x54,0x78}},
{'c', {0x38,0x44,0x44,0x44,0x20}},
{0xB0, {0x06,0x09,0x09,0x06,0x00}}, // symbole degré
```

---

### 3.6 Séquence d'échappement hexadécimale ambiguë

**Problème** : erreur de compilation `hex escape sequence out of range` sur la ligne :
```c
snprintf(line1, sizeof(line1), "T : %.1f\xB0c", temperature);
```
Le compilateur C interprète `\xB0c` comme une seule séquence hexadécimale (`B`, `0`, `c` sont tous des chiffres hexadécimaux valides), ce qui dépasse la plage d'un octet.

**Solution** : séparer les deux littéraux de chaîne adjacents — le compilateur C les fusionne automatiquement :
```c
snprintf(line1, sizeof(line1), "T : %.1f\xB0" "c", temperature);
```

---

### 3.7 Partition trop petite pour le binaire

**Problème** : erreur `app partition is too small for binary` après ajout du composant Wi-Fi. Le binaire dépassait de ~49 Ko la partition `factory` par défaut (1 Mo).

**Solution en deux étapes** :

1. Créer un fichier `partitions.csv` personnalisé à la racine du projet avec une partition `factory` agrandie à 1.5 Mo :
```csv
# Name,   Type, SubType, Offset,   Size,  Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
factory,  app,  factory, 0x10000,  0x180000,
storage,  data, fat,     0x190000, 0x270000,
```

2. Configurer la flash size à 4 Mo et activer la table personnalisée via `idf.py menuconfig` → **Partition Table** → **Custom partition table CSV**.

**Piège** : créer le fichier CSV dans VS Code peut introduire un BOM (Byte Order Mark) provoquant une `UnicodeDecodeError` lors de la génération de la table. Créer le fichier directement depuis le terminal avec `cat > partitions.csv << 'EOF'` pour garantir un encodage ASCII pur.

---

### 3.8 NVS non initialisé avant le Wi-Fi

**Problème** : crash au démarrage avec `ESP_ERR_NVS_NOT_INITIALIZED` dès l'appel à `esp_wifi_init`. Le driver Wi-Fi d'ESP-IDF dépend implicitement du système NVS (mémoire non-volatile) sans le déclarer explicitement.

**Solution** : ajouter l'initialisation NVS au tout début de `wifi_sender_init()` :
```c
#include "nvs_flash.h"

void wifi_sender_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // ... suite de l'init Wi-Fi
```

Ajouter `nvs_flash` dans `REQUIRES` du `CMakeLists.txt` du composant.

---

### 3.9 Fichier CSV trop volumineux pour malloc

**Problème** : après plusieurs heures de fonctionnement, le fichier CSV atteignait ~545 Ko. L'ESP32-C3 ne disposant que de ~400 Ko de RAM utilisable, `malloc` échouait à allouer le buffer nécessaire pour envoyer le fichier entier.

**Solution** : streaming HTTP par blocs de 512 octets avec `Content-Length` déclaré :
1. Mesurer la taille du fichier avec `fseek`/`ftell` (sans le charger en RAM)
2. Déclarer cette taille via `esp_http_client_open(client, file_size)`
3. Lire et envoyer le fichier par blocs fixes de 512 octets avec `fread`/`esp_http_client_write`

La RAM utilisée reste constante à 512 octets quelle que soit la taille du fichier.

**Piège** : utiliser `-1` comme taille dans `esp_http_client_open` active le chunked transfer encoding, que Flask/Werkzeug refuse avec `OSError: Invalid chunk header`. Il faut impérativement déclarer la taille exacte.

---

### 3.10 Envoi chunked rejeté par Flask

**Problème** : après correction du malloc, Flask retournait une erreur 500 avec `OSError: Invalid chunk header`. L'implémentation chunked d'esp_http_client n'est pas compatible avec Werkzeug.

**Solution** : voir 3.9 — déclarer `Content-Length` explicitement plutôt que d'utiliser le mode chunked.

---

## 4. Points techniques notables

### Bus I2C partagé
Un seul bus I2C peut alimenter plusieurs périphériques simultanément. Une seule paire de résistances de pull-up (2.2kΩ vers 3V3) suffit pour tout le bus.

### Broche CSB du BME280
La pin CSB dispose d'une résistance de pull-up interne qui la maintient au niveau haut par défaut, ce qui force le mode I2C sans qu'aucune connexion externe soit nécessaire. La laisser non connectée est donc le comportement normal et suffisant.

### GPIO2 sur ESP32-C3
GPIO2 est une broche de strapping sur ESP32-C3 (comme GPIO8 et GPIO9). Son rôle est moins critique que GPIO8/GPIO9 pour le mode de boot, mais elle peut être sensible aux perturbations électriques transitoires au moment du reset. Dans ce projet, elle est utilisée pour MISO (SPI) sans problème constaté.

### Calibration BME280
Le BME280 grave des coefficients de calibration uniques en usine dans ses registres internes (0x88 à 0xA1 et 0xE1 à 0xE7). Ces coefficients doivent être lus au démarrage et appliqués aux mesures brutes via les formules de compensation officielles Bosch pour obtenir des valeurs exploitables. La température doit être compensée en premier car son résultat intermédiaire (`t_fine`) est réutilisé par les formules de pression et d'humidité.

### Encodage BCD du DS3231
Le DS3231 stocke les valeurs de temps en BCD (Binary Coded Decimal) plutôt qu'en binaire classique. Les conversions `dec_to_bcd` et `bcd_to_dec` sont indispensables pour lire et écrire l'heure correctement.

### Heure de compilation
Les macros `__DATE__` et `__TIME__` sont remplacées par le compilateur au moment de la compilation par la date et l'heure exactes. Elles permettent de régler automatiquement le RTC sans saisie manuelle à chaque flash.

### Framebuffer OLED
Le SSD1306 organise sa mémoire en pages de 8 pixels de haut. L'approche framebuffer (dessiner en RAM puis envoyer tout l'écran d'un coup) évite un affichage qui scintille ligne par ligne et réduit le nombre de transactions I2C.

---

## 5. Stratégie de données

### Fichier CSV quotidien
Un fichier par jour nommé `mesures_YYYY-MM-DD.csv`, créé automatiquement à la première mesure de chaque journée. La rotation est implicite : à minuit, la date change, `sd_card_get_daily_path()` retourne un nouveau nom de fichier.

### Format CSV
```
timestamp,temperature,humidity,pressure,lux
2026-06-27 14:32:10,23.45,42.10,1013.25,150.00
```

### Envoi Wi-Fi
- Connexion au réseau domestique en mode station (STA)
- Résolution du serveur Python via mDNS (`station-receiver.local`)
- Envoi HTTP POST toutes les heures en streaming par blocs de 512 octets
- Le serveur Python (Flask + zeroconf) sauvegarde chaque réception avec un horodatage

---

## 6. Environnement de développement

- **OS** : Linux (Zorin OS)
- **IDE** : VS Code + extension ESP-IDF
- **Framework** : ESP-IDF v6.0.1
- **Langage** : C (ESP32) + Python 3.12 (serveur de réception)
- **Dépendances Python** : Flask, zeroconf
- **Cible** : esp32c3 (architecture RISC-V)
- **Flash** : 4 Mo

---

## 7. Évolutions prévues

- Traitement et visualisation des données CSV en Python (Pandas, Matplotlib)
- Migration vers PCB définitif avec puces nues (ajout pull-up I2C, condensateurs de découplage 100nF par puce)
- Automatisation du serveur Python au démarrage via service systemd
- Hébergement potentiel du serveur sur Raspberry Pi 5 pour fonctionnement permanent
