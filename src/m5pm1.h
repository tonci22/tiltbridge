/**
 * @file m5pm1.h
 * @brief ESP-IDF compatible M5PM1 power management driver
 *
 * Minimal driver for M5PM1 PMIC used in M5StickC S3.
 * Uses ESP-IDF I2C master driver directly.
 */

#ifndef TILTBRIDGE_M5PM1_H
#define TILTBRIDGE_M5PM1_H

#include <driver/i2c_master.h>
#include <esp_err.h>

#define M5PM1_DEFAULT_ADDRESS  0x6E

/**
 * @brief M5PM1 power management class using ESP-IDF I2C master driver
 */
class M5PM1_Driver {
public:
    explicit M5PM1_Driver(uint8_t addr = M5PM1_DEFAULT_ADDRESS);

    /**
     * @brief Initialize I2C and configure M5PM1
     * @param sda_pin SDA GPIO pin
     * @param scl_pin SCL GPIO pin
     * @return true if successful
     */
    bool begin(int sda_pin, int scl_pin);

    /**
     * @brief Check if M5PM1 is present on I2C bus
     * @param sda_pin SDA GPIO pin
     * @param scl_pin SCL GPIO pin
     * @return true if device responds
     */
    bool detect(int sda_pin, int scl_pin);

    /**
     * @brief Clean up I2C driver
     */
    void end();

    /**
     * @brief Enable/disable LCD power (M5PM1 GPIO2)
     */
    void enableLcdPower(bool enable);

    /**
     * @brief Set a GPIO pin as output with given level
     * @param pin M5PM1 GPIO pin number (0-4)
     * @param level true for HIGH, false for LOW
     */
    void gpioSetOutput(uint8_t pin, bool level);

private:
    uint8_t m_addr;
    i2c_master_bus_handle_t m_bus_handle;
    i2c_master_dev_handle_t m_dev_handle;
    bool m_initialized;

    esp_err_t initI2C(int sda_pin, int scl_pin);
    void deinitI2C();
    esp_err_t writeRegister(uint8_t reg, uint8_t value);
    esp_err_t readRegister(uint8_t reg, uint8_t* value);
    esp_err_t setBits(uint8_t reg, uint8_t mask, uint8_t value);
};

#endif // TILTBRIDGE_M5PM1_H
