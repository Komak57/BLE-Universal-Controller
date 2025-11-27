#pragma once
#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include "BLEDevice.h"
#include "BLEDeviceHandler.h"

#ifndef BLE_DEBUG
#define BLE_DEBUG 1
#endif
#if BLE_DEBUG
  #define BLELOG(...)  do { Serial.printf(__VA_ARGS__); } while(0)
#else
  #define BLELOG(...)  do {} while(0)
#endif

class BLEManager
{
public:
    BLEManager();

    void init();
    void update(uint32_t tick);

    // Register up to N handlers; the first matching advertisement wins
    void registerHandler(BLEDeviceHandler *handler);

private:
    // scan/connect state
    bool doConnect_ = false;
    bool connected_ = false;
    bool doScan_ = false;

    BLEClient *client_ = nullptr;
    BLERemoteCharacteristic *notify_ = nullptr;
    BLERemoteCharacteristic *write_ = nullptr;
    BLEAdvertisedDevice *foundDev_ = nullptr;

    // handlers
    static constexpr size_t kMaxHandlers = 4;
    BLEDeviceHandler *handlers_[kMaxHandlers] = {nullptr, nullptr, nullptr, nullptr};
    size_t handlerCount_ = 0;
    BLEDeviceHandler *activeHandler_ = nullptr;

    // callbacks classes
    class SecurityCallback;
    class ScanResult;
    class ClientCallback;

    static BLEManager *active_; // for trampoline
    static void notifyTrampoline(BLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool isNotify);

    bool connectToServer();
    void enableNotifications();
};

#endif // BLE_MANAGER_H
