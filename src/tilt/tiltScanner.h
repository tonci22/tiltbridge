//
// Created by John Beeler on 5/12/18.
//

#ifndef TILTBRIDGE_TILTSCANNER_H
#define TILTBRIDGE_TILTSCANNER_H

#include <NimBLEAdvertisedDevice.h>
#include <ArduinoJson.h>
#include <atomic>
#include <list>

#include "tiltHydrometer.h"

// Milliseconds-since-boot of the most recent Tilt advertisement, published by the BLE
// callback task. The sender health monitor reads this to answer "is BLE still alive?"
// without iterating m_tilt_devices, which the BLE task mutates concurrently.
extern std::atomic<uint32_t> g_last_tilt_advert_ms;


#define BLE_SCAN_TIME 3 * 1000  // Milliseconds to scan

class ScanCallbacks: public NimBLEScanCallbacks
{
    void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override;
};

class tiltScanner
{
public:
    tiltScanner();
    void init();
    void deinit();
    bool scan();

    bool wait_until_scan_complete();
    uint8_t load_tilt_from_advert_hex(const NimBLEAdvertisedDevice* advertisedDevice);
    JsonDocument tilt_to_json();
    void tilt_to_json_legacy(JsonDocument &doc);

    std::size_t tilt_count();
    std::list<tiltHydrometer> m_tilt_devices;

    tiltHydrometer* get_tilt(const NimBLEAddress devAddress, uint8_t color);
    tiltHydrometer* get_or_create_tilt(const NimBLEAddress devAddress, uint8_t color);

    void drop_expired_tilts();

    /**
     * @brief Stop scanning and block new scans until resumeScanning().
     *
     * BLE and WiFi share one 2.4 GHz radio. Deliberately a pause rather than the
     * deinit()/init() the upstream code left commented out around its sends: tearing the
     * NimBLE stack down and back up risks the controller-ownership problem described in
     * main.cpp, while this only halts the scan.
     */
    void pauseScanning();
    void resumeScanning();

private:
    ScanCallbacks *callbacks;
    bool shouldRun;
};

extern tiltScanner tilt_scanner;

#endif //TILTBRIDGE_TILTSCANNER_H
