#ifndef TILTBRIDGE_LOVYAN_CONFIG_H
#define TILTBRIDGE_LOVYAN_CONFIG_H

#include <LovyanGFX.hpp>

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

#elif defined(LCD_TFT_M5STICKC)
// Configuration class for M5StickC Plus and Plus2
class LGFX_M5StickC : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX_M5StickC(void) {}
    void configure(bool isPlus2);
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

#else
// Configuration class for TFT_ESPI displays (ST7789)
class LGFX_TFT_ESPI : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX_TFT_ESPI(void);
};
#endif

#endif // TILTBRIDGE_LOVYAN_CONFIG_H
