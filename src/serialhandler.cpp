#include <thorlog.h>
#include <Arduino.h>

#include "serialhandler.h"


// Wrapper class to adapt Arduino's Serial to ThorPrint
class SerialPrintAdapter : public ThorPrint {
public:
    size_t print(char c) override { return Serial.print(c); }
    size_t print(const char* str) override { return Serial.print(str); }
    size_t print(int num, int base = 10) override { return Serial.print(num, base); }
    size_t print(unsigned int num, int base = 10) override { return Serial.print(num, base); }
    size_t print(long num, int base = 10) override { return Serial.print(num, base); }
    size_t print(unsigned long num, int base = 10) override { return Serial.print(num, base); }
    size_t print(double num) override { return Serial.print(num); }
};

// Global adapter instance
SerialPrintAdapter serialAdapter;

void printTimestamp(ThorPrint *_logOutput)
{
    char c[12];
    sprintf(c, "%10lu ", millis());
    _logOutput->print(c);
    Serial.flush();
}

void printPrefix(ThorPrint* _logOutput, int logLevel) {
    printTimestamp(_logOutput);
}

void debug() {
    #if defined(LOG_LOCAL_LEVEL) && !defined(DISABLE_LOGGING)
    esp_log_level_set("*", ESP_LOG_WARN);

    esp_log_level_set("FreeRTOS", ESP_LOG_WARN);
    esp_log_level_set("NimBLE", ESP_LOG_WARN);
    esp_log_level_set("NIMBLE_NVS", ESP_LOG_WARN);
    esp_log_level_set("NimBLEAddress", ESP_LOG_WARN);
    esp_log_level_set("NimBLEAdvertisedDevice", ESP_LOG_WARN);
    esp_log_level_set("NimBLEAdvertising", ESP_LOG_WARN);
    esp_log_level_set("NimBLEAdvertisingReport", ESP_LOG_WARN);
    esp_log_level_set("NimBLEBeacon", ESP_LOG_WARN);
    esp_log_level_set("NimBLECharacteristic", ESP_LOG_WARN);
    esp_log_level_set("NimBLEClient", ESP_LOG_WARN);
    esp_log_level_set("NimBLEDescriptor", ESP_LOG_WARN);
    esp_log_level_set("NimBLEDevice", ESP_LOG_WARN);
    esp_log_level_set("NimBLEEddystoneTLM", ESP_LOG_WARN);
    esp_log_level_set("NimBLEEddystoneURL", ESP_LOG_WARN);
    esp_log_level_set("NimBLERemoteCharacteristic", ESP_LOG_WARN);
    esp_log_level_set("NimBLERemoteDescriptor", ESP_LOG_WARN);
    esp_log_level_set("NimBLERemoteService", ESP_LOG_WARN);
    esp_log_level_set("NimBLEScan", ESP_LOG_WARN);
    esp_log_level_set("NimBLEServer", ESP_LOG_WARN);
    esp_log_level_set("NimBLEService", ESP_LOG_WARN);
    esp_log_level_set("NimBLEUtils", ESP_LOG_WARN);
    esp_log_level_set("NimBLEUUID", ESP_LOG_WARN);
    
    esp_log_level_set("wifi", ESP_LOG_WARN);      // Enable WARN logs from WiFi stack
    esp_log_level_set("dhcpc", ESP_LOG_WARN);
#endif
}

void serial()
{
    Serial.begin(BAUD);
    Serial.setDebugOutput(true);
    Serial.println();
    Serial.flush();
    Log.begin(ARDUINO_LOG_LEVEL, &serialAdapter, true);  // Use adapter
    Log.setPrefix(printPrefix);
    Log.notice("Serial logging started at %l.\r\n", BAUD);

    debug();
}

size_t printDot()
{
    return printDot(false);
}

size_t printDot(bool safe)
{
#ifdef ARDUINO_LOG_LEVEL
    return Serial.print(F("."));
#else
    return 0;
#endif
}

size_t printChar(const char *chr)
{
    return printChar(false, chr);
}

size_t printChar(bool safe, const char *chr)
{
#ifdef ARDUINO_LOG_LEVEL
    return Serial.println(chr);
#else
    return 0;
#endif
}

size_t printCR()
{
    return printCR(false);
}

size_t printCR(bool safe)
{
#ifdef ARDUINO_LOG_LEVEL
    return Serial.println();
#else
    return 0;
#endif
}

void flush()
{
    flush(false);
}

void flush(bool safe)
{
    Serial.flush();
}
