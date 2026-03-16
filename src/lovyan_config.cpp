#include "lovyan_config.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if defined(LCD_SSD1306)

void LGFX_SSD1306::configure(int sda_pin, int scl_pin, uint8_t i2c_addr, int reset_pin) {
    {
        auto cfg = _bus_instance.config();
        cfg.i2c_port = 0;
        cfg.freq_write = 400000;
        cfg.freq_read = 400000;
        cfg.pin_sda = sda_pin;
        cfg.pin_scl = scl_pin;
        cfg.i2c_addr = i2c_addr;
        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);
    }

    {
        auto cfg = _panel_instance.config();
        cfg.pin_cs = -1;
        cfg.pin_rst = reset_pin;
        cfg.pin_busy = -1;
        cfg.panel_width = 128;
        cfg.panel_height = 64;
        cfg.offset_x = 0;
        cfg.offset_y = 0;
        cfg.offset_rotation = 0;
        cfg.dummy_read_pixel = 8;
        cfg.dummy_read_bits = 1;
        cfg.readable = false;
        cfg.invert = false;
        cfg.rgb_order = false;
        cfg.dlen_16bit = false;
        cfg.bus_shared = false;
        _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
}

#elif defined(LCD_TFT)

// --- Bit-bang SPI helpers (avoid ESP-IDF SPI driver state corruption) ---

void LGFX_TFT_Universal::_gpio_out(int pin)
{
    gpio_config_t c = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&c);
}

uint32_t LGFX_TFT_Universal::_bb_read_cmd(int sclk, int mosi, int miso, int dc, int cs,
                                            uint8_t cmd, uint8_t dummy_bits)
{
    gpio_set_level((gpio_num_t)cs, 0);
    gpio_set_level((gpio_num_t)dc, 0);  // command mode

    // Clock out command byte (MSB first)
    for (int i = 7; i >= 0; --i) {
        gpio_set_level((gpio_num_t)mosi, (cmd >> i) & 1);
        gpio_set_level((gpio_num_t)sclk, 1);
        gpio_set_level((gpio_num_t)sclk, 0);
    }

    gpio_set_level((gpio_num_t)dc, 1);  // data mode

    // Skip dummy bits
    for (uint8_t d = 0; d < dummy_bits; ++d) {
        gpio_set_level((gpio_num_t)sclk, 1);
        gpio_set_level((gpio_num_t)sclk, 0);
    }

    // Read 32 bits (4 bytes), MSB first, packed LSB-first into result
    uint32_t res = 0;
    for (int byte = 0; byte < 4; ++byte) {
        uint8_t val = 0;
        for (int bit = 7; bit >= 0; --bit) {
            gpio_set_level((gpio_num_t)sclk, 1);
            val |= (gpio_get_level((gpio_num_t)miso) << bit);
            gpio_set_level((gpio_num_t)sclk, 0);
        }
        res |= ((uint32_t)val) << (byte * 8);
    }

    gpio_set_level((gpio_num_t)cs, 1);
    return res;
}

LGFX_TFT_Universal::ProbeResult LGFX_TFT_Universal::_bb_probe(int sclk, int mosi, int miso, int dc, int cs)
{
    _gpio_out(sclk);
    _gpio_out(mosi);
    _gpio_out(dc);
    _gpio_out(cs);
    {
        gpio_config_t c = {
            .pin_bit_mask = (1ULL << miso),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&c);
    }

    gpio_set_level((gpio_num_t)cs, 1);
    gpio_set_level((gpio_num_t)sclk, 0);

    ProbeResult r;
    r.id04_d1 = _bb_read_cmd(sclk, mosi, miso, dc, cs, 0x04, 1);
    r.id04_d0 = _bb_read_cmd(sclk, mosi, miso, dc, cs, 0x04, 0);
    r.id09    = _bb_read_cmd(sclk, mosi, miso, dc, cs, 0x09, 1);
    r.idDA    = _bb_read_cmd(sclk, mosi, miso, dc, cs, 0xDA, 0);
    r.idDB    = _bb_read_cmd(sclk, mosi, miso, dc, cs, 0xDB, 0);
    r.idDC    = _bb_read_cmd(sclk, mosi, miso, dc, cs, 0xDC, 0);

    gpio_reset_pin((gpio_num_t)sclk);
    gpio_reset_pin((gpio_num_t)mosi);
    gpio_reset_pin((gpio_num_t)miso);
    gpio_reset_pin((gpio_num_t)dc);
    gpio_reset_pin((gpio_num_t)cs);

    return r;
}

// --- Panel identification and configuration ---

lgfx::Panel_LCD* LGFX_TFT_Universal::_identify_panel(const ProbeResult& r, bool& need_invert)
{
    uint8_t id_byte    = r.id04_d1 & 0xFF;
    uint8_t id_byte_d0 = r.id04_d0 & 0xFF;
    uint8_t id2 = r.idDB & 0xFF;
    uint8_t id3 = r.idDC & 0xFF;
    bool status_nonzero = (r.id09 & 0xFFFFFF) != 0;

    // ST7789 IDs vary by manufacturer: 0x85, 0x81, or shifted variants
    bool is_st7789 = (id_byte == 0x85 || id_byte_d0 == 0x85 ||
                      id_byte == 0x81 || id_byte_d0 == 0x81 ||
                      id2 == 0x85 || id2 == 0x81 || id3 == 0x52);
    // ILI9342: RDDID first byte 0xE3
    bool is_ili9342 = (id_byte == 0xE3 || id_byte_d0 == 0xE3);
    // ILI9341: RDDID byte 0x93, or zero ID with non-zero status
    bool is_ili9341 = (id_byte == 0x93 || id_byte_d0 == 0x93 ||
                       ((id_byte == 0x00) && status_nonzero));

    if (is_st7789) {
        need_invert = true;
        return &_panel_st7789;
    } else if (is_ili9342) {
        need_invert = false;
        return &_panel_ili9342;
    } else if (is_ili9341) {
        need_invert = false;
        return &_panel_ili9341;
    }
    return nullptr;
}

void LGFX_TFT_Universal::_configure_panel(lgfx::Panel_LCD* panel, int cs, int rst, bool bus_shared, bool invert)
{
    auto cfg = panel->config();
    cfg.pin_cs = cs;
    cfg.pin_rst = rst;
    cfg.pin_busy = -1;
    cfg.panel_width = 240;
    cfg.panel_height = 320;
    cfg.offset_x = 0;
    cfg.offset_y = 0;
    cfg.offset_rotation = 0;
    cfg.dummy_read_pixel = 8;
    cfg.dummy_read_bits = 1;
    cfg.readable = true;
    cfg.invert = invert;
    cfg.rgb_order = false;
    cfg.dlen_16bit = false;
    cfg.bus_shared = bus_shared;
    panel->config(cfg);
}

void LGFX_TFT_Universal::_configure_backlight(lgfx::Panel_LCD* panel, int bl_pin)
{
    auto cfg = _light_instance.config();
    cfg.pin_bl = bl_pin;
    cfg.invert = false;
    cfg.freq = 44100;
    cfg.pwm_channel = 7;
    _light_instance.config(cfg);
    panel->setLight(&_light_instance);
}

void LGFX_TFT_Universal::_configure_touch(lgfx::Panel_LCD* panel)
{
    auto cfg = _touch_instance.config();
    cfg.x_min = 0;
    cfg.x_max = 239;
    cfg.y_min = 0;
    cfg.y_max = 319;
    cfg.pin_int = 36;
    cfg.bus_shared = false;
    cfg.offset_rotation = 0;
    cfg.spi_host = VSPI_HOST;
    cfg.freq = 1000000;
    cfg.pin_sclk = 25;
    cfg.pin_mosi = 32;
    cfg.pin_miso = 39;
    cfg.pin_cs = 33;
    _touch_instance.config(cfg);
    panel->setTouch(&_touch_instance);
}

// --- Main detection and configuration entry point ---

void LGFX_TFT_Universal::configure()
{
    lgfx::Panel_LCD* panel = nullptr;
    bool need_invert = false;

    // ---- Probe CYD pin configuration via bit-bang ----
    // We use raw GPIO bit-bang instead of the ESP-IDF SPI driver because
    // bus->init()/release() on HSPI corrupts state that prevents the
    // D32 Pro's VSPI bus from initializing properly afterward.
    // CYD pins: SCLK=14, MOSI=13, MISO=12, DC=2, CS=15
    ProbeResult cyd_ids = _bb_probe(14, 13, 12, 2, 15);

    ESP_LOGI("TFT", "CYD probe: ID04(d1)=0x%08X ID04(d0)=0x%08X ID09=0x%08X",
             (unsigned)cyd_ids.id04_d1, (unsigned)cyd_ids.id04_d0, (unsigned)cyd_ids.id09);
    ESP_LOGI("TFT", "CYD probe: DA=0x%02X DB=0x%02X DC=0x%02X",
             (unsigned)(cyd_ids.idDA & 0xFF), (unsigned)(cyd_ids.idDB & 0xFF), (unsigned)(cyd_ids.idDC & 0xFF));

    panel = _identify_panel(cyd_ids, need_invert);

    if (panel) {
        int bl_pin = need_invert ? 27 : 21;  // ST7789 uses GPIO 27, others GPIO 21

        ESP_LOGI("TFT", "CYD detected: %s, backlight=GPIO%d",
                 (panel == &_panel_st7789) ? "ST7789" :
                 (panel == &_panel_ili9342) ? "ILI9342" : "ILI9341", bl_pin);

        // Configure the real HSPI bus (bit-bang was only for detection)
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = HSPI_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = true;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 14;
            cfg.pin_mosi = 13;
            cfg.pin_miso = 12;
            cfg.pin_dc = 2;
            _bus_instance.config(cfg);
        }

        panel->setBus(&_bus_instance);
        _configure_panel(panel, /*cs=*/15, /*rst=*/-1, /*bus_shared=*/false, need_invert);
        _configure_backlight(panel, bl_pin);
        _configure_touch(panel);
        setPanel(panel);
        return;
    }

    // ---- Probe D32 Pro pin configuration via bit-bang ----
    // D32 Pro has a hardware reset (GPIO 33) — pulse it so the display
    // is awake before we bit-bang probe on VSPI pins.
    {
        _gpio_out(33);
        gpio_set_level(GPIO_NUM_33, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(GPIO_NUM_33, 1);
        vTaskDelay(pdMS_TO_TICKS(150));  // ILI9341 needs up to 120ms after reset
        gpio_reset_pin(GPIO_NUM_33);     // Release before bb_probe
    }

    // D32 Pro pins: SCLK=18, MOSI=23, MISO=19, DC=27, CS=14
    ProbeResult d32_ids = _bb_probe(18, 23, 19, 27, 14);

    ESP_LOGI("TFT", "D32 probe: ID04(d1)=0x%08X ID04(d0)=0x%08X ID09=0x%08X",
             (unsigned)d32_ids.id04_d1, (unsigned)d32_ids.id04_d0, (unsigned)d32_ids.id09);
    ESP_LOGI("TFT", "D32 probe: DA=0x%02X DB=0x%02X DC=0x%02X",
             (unsigned)(d32_ids.idDA & 0xFF), (unsigned)(d32_ids.idDB & 0xFF), (unsigned)(d32_ids.idDC & 0xFF));

    panel = _identify_panel(d32_ids, need_invert);

    if (panel) {
        ESP_LOGI("TFT", "D32 Pro detected: %s",
                 (panel == &_panel_st7789) ? "ST7789" :
                 (panel == &_panel_ili9342) ? "ILI9342" : "ILI9341");
    } else {
        // Fall back to ILI9341 even if ID reads returned zeros
        ESP_LOGI("TFT", "No D32 display ID detected, defaulting to ILI9341");
        panel = &_panel_ili9341;
        need_invert = false;
    }

    // Configure D32 Pro VSPI bus (first init — no prior bus->init() to corrupt state)
    {
        auto cfg = _bus_instance.config();
        cfg.spi_host = VSPI_HOST;
        cfg.spi_mode = 0;
        cfg.freq_write = 40000000;
        cfg.freq_read = 16000000;
        cfg.spi_3wire = true;
        cfg.use_lock = true;
        cfg.dma_channel = SPI_DMA_CH_AUTO;
        cfg.pin_sclk = 18;
        cfg.pin_mosi = 23;
        cfg.pin_miso = 19;
        cfg.pin_dc = 27;
        _bus_instance.config(cfg);
        panel->setBus(&_bus_instance);
    }

    _configure_panel(panel, /*cs=*/14, /*rst=*/33, /*bus_shared=*/true, need_invert);
    // D32 Pro: no backlight binding (TFT_BL handled externally), no touch
    setPanel(panel);
}

#elif defined(LCD_TFT_ESPI)

// --- Small TFT hardware detection ---

LGFX_SmallTFT_Universal::Variant LGFX_SmallTFT_Universal::detect(
    bool (*axp192_probe)(int, int), bool& out_has_axp192)
{
    out_has_axp192 = false;

    // 1. Check GPIO 37 FIRST (pure GPIO, no I2C risk).
    //    Both M5StickC Plus and Plus2 have button A on GPIO 37 with an external
    //    pullup to VCC. TTGO T-Display does not use GPIO 37 (it floats LOW).
    //    GPIO 37 is input-only (34-39), no internal pull available.
    {
        gpio_config_t c = {
            .pin_bit_mask = (1ULL << 37),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&c);
        vTaskDelay(pdMS_TO_TICKS(2));

        int level = gpio_get_level(GPIO_NUM_37);
        ESP_LOGI("TFT", "GPIO 37 (M5 button A) level: %d", level);

        if (level) {
            // GPIO 37 pulled HIGH → M5StickC board. Now distinguish Plus vs Plus2.
            if (axp192_probe && axp192_probe(21, 22)) {
                ESP_LOGI("TFT", "M5StickC Plus detected (AXP192 + button A)");
                out_has_axp192 = true;
                return Variant::M5Plus;
            }
            ESP_LOGI("TFT", "M5StickC Plus2 detected (button A, no AXP192)");
            return Variant::M5Plus2;
        }
    }

    ESP_LOGI("TFT", "No M5StickC detected — TTGO T-Display");
    return Variant::TTGO;
}

// --- Small TFT configuration ---

void LGFX_SmallTFT_Universal::configure(Variant variant)
{
    // SPI bus configuration — pins differ per variant
    {
        auto cfg = _bus_instance.config();
        cfg.spi_mode = 0;
        cfg.spi_3wire = true;
        cfg.use_lock = true;
        cfg.dma_channel = SPI_DMA_CH_AUTO;

        switch (variant) {
        case Variant::M5Plus:
            cfg.spi_host = VSPI_HOST;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.pin_sclk = 13;
            cfg.pin_mosi = 15;
            cfg.pin_miso = -1;
            cfg.pin_dc = 23;
            break;
        case Variant::M5Plus2:
            cfg.spi_host = HSPI_HOST;
            cfg.freq_write = 40000000;
            cfg.freq_read = 15000000;
            cfg.pin_sclk = 13;
            cfg.pin_mosi = 15;
            cfg.pin_miso = -1;
            cfg.pin_dc = 14;
            break;
        case Variant::TTGO:
            cfg.spi_host = VSPI_HOST;
            cfg.freq_write = 40000000;
            cfg.freq_read = 16000000;
            cfg.pin_sclk = 18;
            cfg.pin_mosi = 19;
            cfg.pin_miso = -1;
            cfg.pin_dc = 16;
            break;
        }

        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);
    }

    // Panel configuration — all variants use ST7789 135x240
    {
        auto cfg = _panel_instance.config();
        cfg.pin_cs = 5;
        cfg.pin_busy = -1;
        cfg.panel_width = 135;
        cfg.panel_height = 240;
        cfg.offset_x = 52;
        cfg.offset_y = 40;
        cfg.offset_rotation = 0;
        cfg.dummy_read_pixel = 8;
        cfg.dummy_read_bits = 1;
        cfg.readable = true;
        cfg.invert = true;
        cfg.rgb_order = false;
        cfg.dlen_16bit = false;
        cfg.bus_shared = true;

        switch (variant) {
        case Variant::M5Plus:  cfg.pin_rst = 18; break;
        case Variant::M5Plus2: cfg.pin_rst = 12; break;
        case Variant::TTGO:    cfg.pin_rst = 23; break;
        }

        _panel_instance.config(cfg);
    }

    // Backlight — M5Plus uses AXP192 (no LovyanGFX binding), others use PWM
    if (variant != Variant::M5Plus) {
        auto cfg = _light_instance.config();
        cfg.invert = false;
        cfg.pwm_channel = 7;

        if (variant == Variant::M5Plus2) {
            cfg.pin_bl = 27;
            cfg.freq = 256;
        } else {  // TTGO
            cfg.pin_bl = 4;
            cfg.freq = 44100;
        }

        _light_instance.config(cfg);
        _panel_instance.setLight(&_light_instance);
    }

    setPanel(&_panel_instance);
}

#elif defined(ESP32S3)

LGFX_S3_TDisplay::LGFX_S3_TDisplay(void)
{
    {
        auto cfg = _bus_instance.config();
        cfg.freq_write = 20000000;
        cfg.pin_wr = 8;
        cfg.pin_rd = 9;
        cfg.pin_rs = 7;
        cfg.pin_d0 = 39;
        cfg.pin_d1 = 40;
        cfg.pin_d2 = 41;
        cfg.pin_d3 = 42;
        cfg.pin_d4 = 45;
        cfg.pin_d5 = 46;
        cfg.pin_d6 = 47;
        cfg.pin_d7 = 48;
        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);
    }

    {
        auto cfg = _panel_instance.config();
        cfg.pin_cs = 6;
        cfg.pin_rst = 5;
        cfg.pin_busy = -1;
        cfg.panel_width = 170;
        cfg.panel_height = 320;
        cfg.offset_x = 35;
        cfg.offset_y = 0;
        cfg.offset_rotation = 0;
        cfg.dummy_read_pixel = 8;
        cfg.dummy_read_bits = 1;
        cfg.readable = true;
        cfg.invert = true;
        cfg.rgb_order = false;
        cfg.dlen_16bit = false;
        cfg.bus_shared = true;
        _panel_instance.config(cfg);
    }

    {
        auto cfg = _light_instance.config();
        cfg.pin_bl = 38;
        cfg.invert = false;
        cfg.freq = 44100;
        cfg.pwm_channel = 7;
        _light_instance.config(cfg);
        _panel_instance.setLight(&_light_instance);
    }

    setPanel(&_panel_instance);
}

#endif
