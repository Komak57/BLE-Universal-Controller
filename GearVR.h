#pragma once
#ifndef GEARVR_H
#define GEARVR_H

#include <Arduino.h>
#include "BLEDevice.h"
#include "BLEDeviceHandler.h"
#include "JoyData.h"

// Debug gate
#ifndef GEARVR_DEBUG
#define GEARVR_DEBUG 1
#endif
#if GEARVR_DEBUG
  #define GVLOG(...)  do { Serial.printf(__VA_ARGS__); } while(0)
#else
  #define GVLOG(...)  do {} while(0)
#endif

struct PointerConfig {
  float screenDistance = 0.5f;   // meters
  float screenWidth = 0.6f;      // meters
  float screenHeight = 0.35f;    // meters
  float smoothing = 0.15f;       // 0..1 for low-pass filter
};

constexpr float ACC_LSB_PER_G   = 2048.0f;
constexpr float GYR_LSB_PER_DPS = 14.285f;
constexpr float G_TO_MS2  = 9.80665f;
constexpr float DEG2RAD   = 0.017453292519943295f;
constexpr float ACC_SCALE = G_TO_MS2 / ACC_LSB_PER_G;   // 0.004788 m/s² per LSB
constexpr float GYR_SCALE = DEG2RAD / GYR_LSB_PER_DPS;  // 0.00122 rad/s per LSB

class GearVR : public BLEDeviceHandler {
public:
  GearVR();
  PointerConfig config;

  // BLEDeviceHandler overrides
  bool   matchesAdvertisement(BLEAdvertisedDevice& dev) override;
  BLEUUID serviceUuid() const override;
  BLEUUID writeCharUuid() const override;
  BLEUUID notifyCharUuid() const override;
  BLEUUID notifyDescriptorUuid() const override;

  void onConnected(BLEClient* client,
                   BLERemoteService* service,
                   BLERemoteCharacteristic* writeChr,
                   BLERemoteCharacteristic* notifyChr) override;
  void onDisconnected() override;

  void onNotify(BLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool isNotify) override;

  bool hasPending() const override;
  void trySendPending(BLERemoteCharacteristic* writeChr) override;

  // Public state for main/UI if needed
  JoyData joy, lastjoy;

private:
  // UUIDs
  static BLEUUID sService;
  static BLEUUID sWrite;
  static BLEUUID sNotify;
  static BLEUUID sCCCD;

  // Commands
  static const uint8_t kOff[2];
  static const uint8_t kSensor[2];
  static const uint8_t kFwUp[2];
  static const uint8_t kCal[2];
  static const uint8_t kKeep[2];
  static const uint8_t kUnk[2];
  static const uint8_t kLpmEn[2];
  static const uint8_t kLpmDis[2];
  static const uint8_t kVr[2];

  // local connection context (not owned)
  BLEClient*               client_ = nullptr;
  BLERemoteCharacteristic* write_  = nullptr;
  BLERemoteCharacteristic* notify_ = nullptr;

  // send-next-frame queue (exactly one short command)
  volatile bool   pending_ = false;
  uint8_t         pendingCmd_[2] = {0x00,0x00};
  uint8_t         mode_ = 0x00;
  bool            receiving_ = false;
  bool            streaming_ = false;
  uint32_t        keepaliveMs_ = 0;
  uint8_t         handshakeStage_ = 0;
  uint32_t        handshakeTimer_ = 0;
  

  // device-specific constants (were in JoyData before)
  static constexpr int    kMaxRadius   = 315;
  static constexpr double kRadius      = kMaxRadius / 2.0;
  static constexpr double kGyroFactor  = 10000.0 * 0.017453292 / 14.285;
  static constexpr double kAccelFactor = 10000.0 * 9.80665 / 2048.0;
  static constexpr double kMagnoFactor = 0.06;

  void queueCmd(const uint8_t cmd[2]);
  void parseFullPacket(uint8_t* p, size_t len);
  void tick(uint32_t ms) override;

  // (Optional) emit USB HID actions immediately here if you want device-owned mapping
  void emitUSB(const JoyData& now, const JoyData& prev);
};

#endif // GEARVR_H
