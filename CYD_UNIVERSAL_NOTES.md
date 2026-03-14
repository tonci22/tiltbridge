# CYD Universal Display Auto-Detection — Entwicklungsnotizen

## Ziel

Ein einzelnes Binary (`env:cyd`) soll alle CYD (Cheap Yellow Display) Varianten unterstützen:
- ESP32-2432S024 (2.4")
- ESP32-2432S028 v1/v2/v3 (2.8")
- ESP32-2432S032 (3.2")

Die Displays unterscheiden sich im verwendeten Display-Treiber (ILI9341, ILI9342, ST7789) und teilweise im Backlight-Pin (GPIO 21 vs GPIO 27). Der Touch-Controller (XPT2046) und die SPI-Pinbelegung sind bei allen Varianten identisch.

## Bekannte CYD-Varianten

| Board | Display | Treiber | Backlight | Touch |
|-------|---------|---------|-----------|-------|
| ESP32-2432S028 v1 | 2.8" | ILI9341 | GPIO 21 | XPT2046 (R) |
| ESP32-2432S028 v2 | 2.8" | ILI9342 | GPIO 21 | XPT2046 (R) |
| ESP32-2432S028 v3 | 2.8" | ST7789 | GPIO 27 | XPT2046 (R) |
| ESP32-2432S032 | 3.2" | ST7789 | GPIO 27 | XPT2046 (R) |
| ESP32-2432S024 | 2.4" | ILI9341 oder ST7789 | GPIO 27 | XPT2046 (R) |

Quelle: ESP32-Marauder CYD Projekt, diverse Community-Dokumentationen.

## Geänderte Dateien (basierend auf `origin/next`)

### 1. `platformio.ini`
- **Platform-Upgrade**: `espressif32` (unversioniert, löste zu 6.3.1 / ESP-IDF 5.0.2 auf) → `espressif32@6.13.0` (ESP-IDF 5.5.3)
- Grund: Die Codebasis verwendet APIs die erst ab ESP-IDF 5.2+ verfügbar sind (`httpd_req_async_handler_begin`, `driver/i2c_master.h`, `littlefs` Partitionstyp)
- Das offizielle Projekt verwendet ESP-IDF 5.5.1 für Releases

### 2. `src/lovyan_config.h`
- Die bestehende `LGFX_CYD` Klasse wurde durch eine universelle Version ersetzt
- Enthält alle drei Panel-Typen als Member (`Panel_ILI9341`, `Panel_ILI9342`, `Panel_ST7789`)
- Neue `configure()` Methode mit Runtime-Detection statt Konstruktor-Initialisierung
- `_read_cmd()` Hilfsfunktion zum Lesen von Display-Registern via SPI

### 3. `src/bridge_lcd_impl.cpp`
- CYD-Initialisierung geändert: `new LGFX_CYD()` → `new LGFX_CYD(); cyd_tft->configure();`
- Gleicher Ansatz wie bei `LGFX_M5StickC` (die ebenfalls `configure()` nutzt)

## Ansätze zur Display-Erkennung

### Ansatz 1: Compile-time `#ifdef` (erster Versuch, funktioniert)
- Separates `env:cyd32` mit eigenem Build-Flag `-D CYD32=1`
- Separate `LGFX_CYD32` Klasse mit `Panel_ST7789` und `pin_bl = 27`
- **Vorteil**: Einfach, zuverlässig
- **Nachteil**: Braucht ein separates Binary pro CYD-Variante
- **Status**: Funktioniert, wurde im `next` Branch gemerged

### Ansatz 2: Runtime Display-ID via SPI Command 0x04 (aktueller Ansatz)

#### Versuch 2a: ID lesen mit `spi_3wire = true` (gescheitert)
- Alle Register gaben `0x00000000` zurück
- **Ursache**: Im 3-Wire SPI Modus wird MOSI bidirektional genutzt, aber der ESP32 SPI-Treiber unterstützt kein Half-Duplex Read auf der MOSI-Leitung. Der MISO-Pin (GPIO 12) wird im 3-Wire Modus ignoriert.
- **Erkenntnis**: `spi_3wire = true` verhindert SPI-Reads komplett

#### Versuch 2b: ID lesen mit `spi_3wire = false` für den Probe (funktioniert)
- Temporär `spi_3wire = false` setzen, Bus initialisieren, IDs lesen, Bus freigeben, `spi_3wire = true` zurücksetzen
- **Ergebnis auf ESP32-2432S032 (3.2" ST7789)**:
  ```
  ID04(d1)=0xD9818181  ID04(d0)=0xFFD9C040  ID09=0x00610000
  IDDA=0x81  IDDB=0x81  IDDC=0xB3
  ```
- Der ST7789 gibt `0x81` als ID zurück (nicht den Standardwert `0x85` aus der Dokumentation)
- **Erkenntnis**: Display-IDs variieren je nach Hersteller/Charge

#### Versuch 2c: Fallback-Strategie bei unbekannter ID
- Da IDs herstellerabhängig variieren, wurde eine mehrstufige Erkennung implementiert:
  1. Mehrere SPI-Kommandos lesen (0x04, 0x09, 0xDA, 0xDB, 0xDC)
  2. Mit verschiedenen Dummy-Bit-Werten (0 und 1)
  3. Bekannte IDs matchen (ST7789: 0x85/0x81, ILI9342: 0xE3, ILI9341: 0x93)
  4. Bei unbekannter ID: Default auf ST7789 mit GPIO 27 (häufigstes neueres Board)

### Verworfene Ansätze

- **LovyanGFX Auto-Detect**: LovyanGFX hat ein eingebautes Auto-Detection-System in `LGFX_AutoDetect_ESP32_all.hpp`, kennt aber die CYD-Boards nicht. Es verwendet dieselbe `_read_panel_id()` Methode, die wir nachgebaut haben.
- **Backlight-Pin als Erkennungsmerkmal**: GPIO 21 vs 27 kann nicht zuverlässig getestet werden, da ein offener GPIO-Pin kein eindeutiges Signal liefert.

## Wichtiger Fehler: Display bleibt dunkel

Beim ersten Versuch mit dem 3.2" CYD blieb das Display dunkel. Ursache: Der **Backlight-Pin** ist GPIO 27 (nicht GPIO 21 wie beim 2.8"). Gefunden durch Recherche im [ESP32-Marauder CYD Projekt](https://github.com/Fr4nkFletcher/ESP32-Marauder-Cheap-Yellow-Display).

## Aktueller Stand

- ✅ Build kompiliert erfolgreich (`env:cyd`)
- ✅ Flash-Verbrauch: 50.2% (nur +2.5 KB gegenüber Single-Display-Version)
- ✅ **ESP32-2432S032 (3.2" ST7789)**: Display funktioniert, erkannt via ID `0x81`
- ✅ Touch wurde nicht explizit getestet, aber der Code ist identisch zur funktionierenden `env:cyd` Version

## Offene Tests

### Boards die noch getestet werden müssen:
- [ ] **ESP32-2432S028 v1 (2.8" ILI9341)** — Wird die ID korrekt als ILI9341 erkannt? Backlight GPIO 21?
- [ ] **ESP32-2432S028 v2 (2.8" ILI9342)** — Wird `0xE3` gelesen?
- [ ] **ESP32-2432S028 v3 (2.8" ST7789)** — Wird `0x85` oder `0x81` gelesen? Backlight GPIO 27?
- [ ] **ESP32-2432S024 (2.4")** — Welche IDs gibt dieses Board zurück?

### Testprozedur:
1. Binary flashen: `pio run -e cyd -t upload --upload-port /dev/cu.usbserial-XX`
2. Serial Monitor öffnen (115200 Baud)
3. Reset-Taste drücken
4. Nach `W (xxx) CYD:` Zeilen suchen — diese zeigen die gelesenen IDs und den erkannten Display-Typ
5. Prüfen ob Display leuchtet und Inhalt korrekt angezeigt wird

### Was bei unbekannten IDs zu tun ist:
- Die Serial-Log-Zeilen `ID04(d1)=...`, `IDDA=...` etc. notieren
- Falls das Display nicht erkannt wird: Die neuen ID-Werte in die `is_st7789`, `is_ili9342` oder `is_ili9341` Bedingungen in `lovyan_config.h` eintragen

## Debug-Logging

Aktuell sind `ESP_LOGW("CYD", ...)` Logs aktiv auf Level WARNING. Diese geben aus:
- Alle gelesenen Register-Werte (ID04, ID09, IDDA, IDDB, IDDC)
- Den erkannten Display-Typ und Backlight-Pin

Die Logs sollten nach Abschluss aller Tests auf `ESP_LOGI` (Info) herabgestuft oder entfernt werden.

## Abhängigkeiten

- **PlatformIO**: `espressif32@6.13.0` (ESP-IDF 5.5.3)
- **LovyanGFX**: Git-Commit `2e0dc974a9b6521bf155afb53a9a92a623a60803`
- **Python**: `intelhex` Modul muss ggf. installiert werden: `~/.platformio/penv/bin/pip install intelhex`
