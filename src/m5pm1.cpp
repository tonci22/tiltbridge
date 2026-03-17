/**
 * @file m5pm1.cpp
 * @brief ESP-IDF compatible M5PM1 power management driver implementation
 *
 * Uses ESP-IDF I2C master driver (driver_ng).
 */

#include "m5pm1.h"
#include <thorlog.h>
#include <cstring>

// M5PM1 Register definitions
#define M5PM1_REG_DEVICE_ID     0x00
#define M5PM1_REG_I2C_CFG       0x09
#define M5PM1_REG_GPIO_MODE     0x10  // Bit per pin: 1=output, 0=input
#define M5PM1_REG_GPIO_OUT      0x11  // Output value, bit per pin
#define M5PM1_REG_GPIO_IN       0x12  // Input readback
#define M5PM1_REG_GPIO_FUNC0    0x16  // 2-bit fields: pin function select (GPIO0-3)

// Timeout for I2C operations
#define I2C_TIMEOUT_MS  100

M5PM1_Driver::M5PM1_Driver(uint8_t addr)
    : m_addr(addr)
    , m_bus_handle(nullptr)
    , m_dev_handle(nullptr)
    , m_initialized(false)
{
}

esp_err_t M5PM1_Driver::initI2C(int sda_pin, int scl_pin)
{
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_1;
    bus_config.sda_io_num = static_cast<gpio_num_t>(sda_pin);
    bus_config.scl_io_num = static_cast<gpio_num_t>(scl_pin);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_config, &m_bus_handle);
    if (err != ESP_OK) {
        Log.error("M5PM1: Failed to create I2C master bus: %d" CR, err);
        return err;
    }

    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = m_addr;
    dev_config.scl_speed_hz = 100000;  // 100kHz initial speed

    err = i2c_master_bus_add_device(m_bus_handle, &dev_config, &m_dev_handle);
    if (err != ESP_OK) {
        Log.error("M5PM1: Failed to add device to I2C bus: %d" CR, err);
        i2c_del_master_bus(m_bus_handle);
        m_bus_handle = nullptr;
        return err;
    }

    return ESP_OK;
}

void M5PM1_Driver::deinitI2C()
{
    if (m_dev_handle != nullptr) {
        i2c_master_bus_rm_device(m_dev_handle);
        m_dev_handle = nullptr;
    }
    if (m_bus_handle != nullptr) {
        i2c_del_master_bus(m_bus_handle);
        m_bus_handle = nullptr;
    }
}

esp_err_t M5PM1_Driver::writeRegister(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(m_dev_handle, data, 2, I2C_TIMEOUT_MS);
}

esp_err_t M5PM1_Driver::readRegister(uint8_t reg, uint8_t* value)
{
    return i2c_master_transmit_receive(m_dev_handle, &reg, 1, value, 1, I2C_TIMEOUT_MS);
}

esp_err_t M5PM1_Driver::setBits(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current;
    esp_err_t err = readRegister(reg, &current);
    if (err != ESP_OK) {
        return err;
    }

    current = (current & ~mask) | (value & mask);
    return writeRegister(reg, current);
}

bool M5PM1_Driver::detect(int sda_pin, int scl_pin)
{
    esp_err_t err = initI2C(sda_pin, scl_pin);
    if (err != ESP_OK) {
        return false;
    }

    uint8_t device_id = 0;
    err = readRegister(M5PM1_REG_DEVICE_ID, &device_id);

    deinitI2C();

    if (err == ESP_OK) {
        Log.notice("M5PM1: Detected device ID 0x%02X" CR, device_id);
        return true;
    }

    return false;
}

bool M5PM1_Driver::begin(int sda_pin, int scl_pin)
{
    esp_err_t err = initI2C(sda_pin, scl_pin);
    if (err != ESP_OK) {
        return false;
    }

    // Verify device is present
    uint8_t device_id = 0;
    err = readRegister(M5PM1_REG_DEVICE_ID, &device_id);
    if (err != ESP_OK) {
        Log.error("M5PM1: Device not responding" CR);
        deinitI2C();
        return false;
    }

    Log.notice("M5PM1: Initializing (device ID 0x%02X)" CR, device_id);

    m_initialized = true;

    // Disable I2C sleep mode
    writeRegister(M5PM1_REG_I2C_CFG, 0x00);

    Log.notice("M5PM1: Initialization complete" CR);
    return true;
}

void M5PM1_Driver::end()
{
    if (m_initialized) {
        deinitI2C();
        m_initialized = false;
    }
}

void M5PM1_Driver::gpioSetOutput(uint8_t pin, bool level)
{
    if (!m_initialized || pin > 4) return;

    uint8_t bit = (1 << pin);

    // Set function to GPIO (clear 2-bit field in FUNC register)
    if (pin <= 3) {
        uint8_t shift = pin * 2;
        uint8_t mask = 0x03 << shift;
        setBits(M5PM1_REG_GPIO_FUNC0, mask, 0x00);  // 0x00 = GPIO function
    }

    // Set pin as output
    setBits(M5PM1_REG_GPIO_MODE, bit, bit);

    // Set output level
    setBits(M5PM1_REG_GPIO_OUT, bit, level ? bit : 0);
}

void M5PM1_Driver::enableLcdPower(bool enable)
{
    gpioSetOutput(2, enable);
}
