#pragma once
#include <Arduino.h>
#include "BLEDevice.h"

// Base class for any BLE controller handled by BLEManager.
class BLEDeviceHandler
{
public:
    virtual ~BLEDeviceHandler() {}

    // Called during scan; return true if this advertisement belongs to this device
    // NOTE: BLEAdvertisedDevice::getName() is non-const, so arg can't be const here.
    virtual bool matchesAdvertisement(BLEAdvertisedDevice &dev) = 0;

    // UUIDs needed to discover the device's service/characteristics
    virtual BLEUUID serviceUuid() const = 0;
    virtual BLEUUID writeCharUuid() const = 0;
    virtual BLEUUID notifyCharUuid() const = 0;

    // Optional: CCCD UUID if special (otherwise 0x2902 is used)
    virtual BLEUUID notifyDescriptorUuid() const { return BLEUUID((uint16_t)0x2902); }

    // Connection lifecycle
    virtual bool onConnected(BLEClient *client_) = 0;
    virtual void onDisconnected() = 0;

    // Next-frame send mechanics
    virtual void update(uint32_t tick) = 0;
};
