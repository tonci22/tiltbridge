#ifndef TILTBRIDGE_LOVYAN_CONFIG_H
#define TILTBRIDGE_LOVYAN_CONFIG_H

#if defined(LCD_SSD1306) || defined(LCD_TFT) || defined(LCD_TFT_ESPI) || defined(ESP32S3)
#include <LovyanGFX.hpp>
#endif

#if defined(LCD_SSD1306)
// Configuration class for SSD1306 OLED displays (128x64)
class LGFX_SSD1306 : public lgfx::LGFX_Device
{
    lgfx::Panel_SSD1306 _panel_instance;
    lgfx::Bus_I2C _bus_instance;

public:
    LGFX_SSD1306(void) {}  // Empty constructor - configure() does the work
    void configure(int sda_pin, int scl_pin, uint8_t i2c_addr = 0x3C, int reset_pin = -1);
};

#elif defined(LCD_TFT)
// Universal TFT configuration class for 240x320 SPI displays
// Auto-detects hardware by probing known pin configurations at runtime:
//   1. CYD variants (HSPI): ESP32-2432S024, S028 v1/v2/v3, S032
//   2. D32 Pro (VSPI): Lolin D32 Pro + ILI9341 TFT shield
class LGFX_TFT_Universal : public lgfx::LGFX_Device
{
    // Panel types — only one is used at runtime
    lgfx::Panel_ILI9341 _panel_ili9341;
    lgfx::Panel_ILI9342 _panel_ili9342;
    lgfx::Panel_ST7789  _panel_st7789;

    lgfx::Bus_SPI _bus_instance;

    lgfx::Light_PWM _light_instance;
    lgfx::Touch_XPT2046 _touch_instance;

    // Bit-bang SPI probe — reads a display register using raw GPIO.
    // This avoids the ESP-IDF SPI driver entirely, preventing any
    // driver/DMA state corruption that could break other SPI buses.
    struct ProbeResult {
        uint32_t id04_d1, id04_d0, id09, idDA, idDB, idDC;
    };

    static void _gpio_out(int pin);
    static uint32_t _bb_read_cmd(int sclk, int mosi, int miso, int dc, int cs,
                                  uint8_t cmd, uint8_t dummy_bits);
    static ProbeResult _bb_probe(int sclk, int mosi, int miso, int dc, int cs);

    lgfx::Panel_LCD* _identify_panel(const ProbeResult& r, bool& need_invert);
    static void _configure_panel(lgfx::Panel_LCD* panel, int cs, int rst, bool bus_shared, bool invert);
    void _configure_backlight(lgfx::Panel_LCD* panel, int bl_pin);
    void _configure_touch(lgfx::Panel_LCD* panel);

public:
    LGFX_TFT_Universal(void) {}
    void configure();
};

#elif defined(LCD_TFT_ESPI)
// Universal TFT configuration class for 135x240 SPI displays (all ST7789)
// Auto-detects hardware at runtime:
//   1. M5StickC Plus (AXP192 on I2C) — VSPI, backlight via AXP192
//   2. M5StickC Plus2 (no AXP192, RST pullup on GPIO 12) — HSPI, backlight GPIO 27
//   3. TTGO T-Display (fallback) — VSPI, backlight GPIO 4
class LGFX_SmallTFT_Universal : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    enum class Variant { M5Plus, M5Plus2, TTGO };

    LGFX_SmallTFT_Universal(void) {}
    void configure(Variant variant);

    // Detect which small TFT hardware is present (call before configure).
    // GPIO-only probe first, then I2C for AXP192 only if needed.
    // Returns the variant and sets out_has_axp192 for caller's power init.
    static Variant detect(bool (*axp192_probe)(int, int), bool& out_has_axp192);
};

#elif defined(ESP32S3)
// Configuration class for ESP32-S3 TDisplay
class LGFX_S3_TDisplay : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_Parallel8 _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX_S3_TDisplay(void);
};

#endif

#endif // TILTBRIDGE_LOVYAN_CONFIG_H
