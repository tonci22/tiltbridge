#include <cstdio>
#include <cstring>
#include <esp_timer.h>
#include <esp_log.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <thorlog.h>
#include <thorlog_espidf.h>

#include "serialhandler.h"

/*
 * Why this file does not just use EspIdfPrint directly.
 *
 * ThorLog emits a message as one printf() per character (thorlog.h print()), and
 * every task shares the single process-wide, line-buffered stdout. newlib locks
 * that FILE for the duration of one printf - i.e. one character - so between any
 * two characters of a log line another task can append its own and flush. The old
 * printTimestamp() made it certain rather than merely likely, because it called
 * fflush(stdout) on every line from every task, draining whatever half-written
 * line everyone else had accumulated. The result was output like:
 *
 *   mDNS responder started, hostn      7986 aIm: eHTTP server started. Open: ht:t
 *
 * The fix is to make a message atomic without touching the library: buffer every
 * character ThorLog emits, and write the whole line in one fwrite. ThorLog brackets
 * each message with the prefix and suffix callbacks (thorlog.h printLevel), so those
 * are exact begin/end hooks - the suffix runs whether or not the format string ends
 * in a newline, which is what makes this deadlock-free.
 */
class AtomicLinePrint : public ThorPrint {
public:
    // Called from the prefix hook: take the line lock and start a fresh line.
    void beginLine() {
        if (m_lock) xSemaphoreTakeRecursive(m_lock, portMAX_DELAY);
        m_len = 0;
    }

    // Called from the suffix hook: emit the whole line as a single write.
    void endLine() {
        flush();
        if (m_lock) xSemaphoreGiveRecursive(m_lock);
    }

    size_t print(char c) override { return append(&c, 1); }

    size_t print(const char* str) override {
        if (str == nullptr) return 0;
        return append(str, strlen(str));
    }

    size_t print(int num, int base = THORLOG_DEC) override    { return num_(static_cast<long>(num), base, false); }
    size_t print(long num, int base = THORLOG_DEC) override   { return num_(num, base, false); }
    size_t print(unsigned int num, int base = THORLOG_DEC) override  { return num_(static_cast<long>(static_cast<unsigned long>(num)), base, true); }
    size_t print(unsigned long num, int base = THORLOG_DEC) override { return num_(static_cast<long>(num), base, true); }

    size_t print(double num) override {
        char tmp[32];
        int n = snprintf(tmp, sizeof(tmp), "%.2f", num);
        return (n > 0) ? append(tmp, (size_t)n) : 0;
    }

    void createLock() {
        if (m_lock == nullptr) m_lock = xSemaphoreCreateRecursiveMutex();
    }

private:
    size_t append(const char* src, size_t n) {
        for (size_t i = 0; i < n; i++) {
            // An oversized message (e.g. a dumped HTTP response body) is written in
            // whole chunks rather than truncated. Only such messages can interleave,
            // and they never lose characters.
            if (m_len == sizeof(m_buf)) flush();
            m_buf[m_len++] = src[i];
        }
        return n;
    }

    size_t num_(long v, int base, bool isUnsigned) {
        char tmp[40];
        int n;
        if (base == THORLOG_HEX) {
            n = snprintf(tmp, sizeof(tmp), "%lx", (unsigned long)v);
        } else if (base == THORLOG_BIN) {
            unsigned long u = (unsigned long)v;
            n = 0;
            if (u == 0) { tmp[n++] = '0'; }
            else {
                char rev[33]; int r = 0;
                while (u > 0 && r < 32) { rev[r++] = (u & 1) ? '1' : '0'; u >>= 1; }
                while (r > 0) tmp[n++] = rev[--r];
            }
        } else {
            n = isUnsigned ? snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)v)
                           : snprintf(tmp, sizeof(tmp), "%ld", v);
        }
        return (n > 0) ? append(tmp, (size_t)n) : 0;
    }

    void flush() {
        if (m_len == 0) return;
        fwrite(m_buf, 1, m_len, stdout);
        m_len = 0;
    }

    char m_buf[512];
    size_t m_len = 0;
    SemaphoreHandle_t m_lock = nullptr;
};

static AtomicLinePrint logAdapter;

// Get milliseconds since boot using ESP-IDF timer
static uint32_t millis_espidf() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void printTimestamp(ThorPrint *_logOutput)
{
    // 10 digits + space + NUL is exactly 12; 16 leaves room if the width ever grows.
    char c[16];
    snprintf(c, sizeof(c), "%10lu ", (unsigned long)millis_espidf());
    _logOutput->print(c);
}

void printPrefix(ThorPrint* _logOutput, int logLevel) {
    logAdapter.beginLine();
    printTimestamp(_logOutput);
}

void printSuffix(ThorPrint* _logOutput, int logLevel) {
    logAdapter.endLine();
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
    // ESP-IDF auto-configures UART0 for console output via menuconfig
    // (CONFIG_ESP_CONSOLE_UART), so explicit UART init is not needed.
    // Just initialize the logging system.

    printf("\n");
    fflush(stdout);

    // The lock must exist before the first log call can take it.
    logAdapter.createLock();

    Log.begin(ARDUINO_LOG_LEVEL, &logAdapter, true);
    Log.setPrefix(printPrefix);
    Log.setSuffix(printSuffix);   // emits the buffered line as one write
    Log.notice("Serial logging started at %d.\r\n", BAUD);

    debug();
}

size_t printDot()
{
    return printDot(false);
}

size_t printDot(bool safe)
{
#ifdef ARDUINO_LOG_LEVEL
    int result = printf(".");
    return (result > 0) ? static_cast<size_t>(result) : 0;
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
    int result = printf("%s\n", chr);
    return (result > 0) ? static_cast<size_t>(result) : 0;
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
    int result = printf("\n");
    return (result > 0) ? static_cast<size_t>(result) : 0;
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
    // TODO(ESP-IDF): Can optionally use uart_wait_tx_done(UART_NUM_0, portMAX_DELAY)
    // for guaranteed flush, but fflush(stdout) should work in most cases.
    fflush(stdout);
}
