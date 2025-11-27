#include "BLEManager.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "Helper.h"

extern SystemState sys;

// static
BLEManager* BLEManager::active_ = nullptr;

class BLEManager::SecurityCallback : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { BLELOG("PassKeyRequest\n"); return 123456; }
  void onPassKeyNotify(uint32_t pass_key) override { BLELOG("Passkey: %u\n", pass_key); }
  bool onConfirmPIN(uint32_t pass_key) override { BLELOG("Confirm PIN: %u\n", pass_key); return true; }
  bool onSecurityRequest() override { BLELOG("Security Request\n"); return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
    if (cmpl.success) BLELOG("Pairing Successful!\n");
    else BLELOG("Pairing Failed, Reason: %d\n", cmpl.fail_reason);
  }
};

class BLEManager::ScanResult : public BLEAdvertisedDeviceCallbacks {
public:
  explicit ScanResult(BLEManager* mgr) : mgr_(mgr) {}
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    // Iterate registered handlers to find a match
    for (size_t i=0; i<mgr_->handlerCount_; ++i) {
      BLEDeviceHandler* h = mgr_->handlers_[i];
      if (!h) continue;
      // getName() is non-const; advertisedDevice cannot be const here
      if (h->matchesAdvertisement(advertisedDevice)) {
        BLELOG("Target matched by handler. Stopping scan...\n");
        BLEDevice::getScan()->stop();
        if (mgr_->foundDev_) delete mgr_->foundDev_;
        mgr_->foundDev_ = new BLEAdvertisedDevice(advertisedDevice);
        mgr_->activeHandler_ = h;
        mgr_->doConnect_ = true;
        mgr_->doScan_ = true;
        return;
      }
    }
  }
private:
  BLEManager* mgr_;
};

class BLEManager::ClientCallback : public BLEClientCallbacks {
public:
  explicit ClientCallback(BLEManager* mgr) : mgr_(mgr) {}
  void onConnect(BLEClient* pClient) override {
    sys = SystemState::Connected;
    BLELOG("onConnect()\n");
    // user-level onConnect is owned by handler->onConnected after discovery
  }
  void onDisconnect(BLEClient* pClient) override {
    BLELOG("onDisconnect()\n");
    mgr_->connected_ = false;
    if (mgr_->activeHandler_) mgr_->activeHandler_->onDisconnected();
    BLELOG("Idle...\n");
    sys = SystemState::Idle;
  }
private:
  BLEManager* mgr_;
};

// notify trampoline
void BLEManager::notifyTrampoline(BLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool isNotify) {
  if (!active_ || !active_->activeHandler_) return;
  // pass immediately to device handler (it may queue BLE replies for next frame)
  active_->activeHandler_->onNotify(chr, data, len, isNotify);
}

BLEManager::BLEManager() {}

void BLEManager::registerHandler(BLEDeviceHandler* handler) {
  if (handlerCount_ < kMaxHandlers) handlers_[handlerCount_++] = handler;
}

void BLEManager::init() {
  active_ = this;

  BLEDevice::init("");
  BLEDevice::setMTU(517);
  BLELOG("MTU set to %i\n", BLEDevice::getMTU());

  BLEDevice::setSecurityCallbacks(new SecurityCallback());

  auto *sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  sec->setCapability(ESP_IO_CAP_IO);
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  BLEDevice::setPower(ESP_PWR_LVL_P9);

  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanResult(this));
  scan->setInterval(1349);
  scan->setWindow(449);
  scan->setActiveScan(true);
  scan->start(0, false);
  sys = SystemState::Scanning;
  BLELOG("Scanning...\n");
}

bool BLEManager::connectToServer() {
  if (!foundDev_ || !activeHandler_) return false;

  String addr = foundDev_->getAddress().toString().c_str();
  BLELOG("Connecting to %s\n", addr.c_str());

  client_ = BLEDevice::createClient();
  client_->setClientCallbacks(new ClientCallback(this));

  if (!client_->connect(foundDev_)) {
    BLELOG(" - Unable to connect\n");
    return false;
  }

  // Discover service/characteristics via handler UUIDs
  BLERemoteService* svc = client_->getService(activeHandler_->serviceUuid());
  if (!svc) {
    BLELOG("Failed to find service\n");
    client_->disconnect();
    return false;
  }

  write_  = svc->getCharacteristic(activeHandler_->writeCharUuid());
  notify_ = svc->getCharacteristic(activeHandler_->notifyCharUuid());
  if (!write_ || !write_->canWrite())  { BLELOG("Invalid write characteristic\n"); client_->disconnect(); return false; }
  if (!notify_ || !notify_->canNotify()){ BLELOG("Invalid notify characteristic\n"); client_->disconnect(); return false; }

  // Subscribe
  notify_->registerForNotify(&BLEManager::notifyTrampoline);
  enableNotifications();
  BLELOG("1\n");
  // Inform handler (it can queue initial command)
  activeHandler_->onConnected(client_, svc, write_, notify_);
  BLELOG("2\n");
  connected_ = true;
  return true;
}

void BLEManager::enableNotifications() {
  // Write CCCD = 0x0001 (notifications enable)
  BLERemoteDescriptor* d = notify_->getDescriptor(activeHandler_->notifyDescriptorUuid());
  if (!d) d = notify_->getDescriptor(BLEUUID((uint16_t)0x2902));
  if (d) {
    uint8_t enable[2] = {0x01, 0x00};
    d->writeValue(enable, 2, true);
    BLELOG("CCCD written: notifications enabled\n");
  } else {
    BLELOG("No CCCD found; relying on registerForNotify only\n");
  }
}

void BLEManager::update(uint32_t tick) {
    BLELOG("Update...\n");
  Serial.println("BLEManager Update");
  // connect step
  if (doConnect_) {
    if (connectToServer()) BLELOG("Connected to server.\n");
    else BLELOG("Failed to connect.\n");
    doConnect_ = false;
  }
  BLELOG("3\n");
  // next-frame BLE writes
  if (connected_ && activeHandler_) {
    activeHandler_->tick(tick);              // allow device timers
    if (activeHandler_->hasPending()) {
      activeHandler_->trySendPending(write_);  // next-frame BLE write
    }
  }
  BLELOG("4\n");

  // re-scan if not connected
  if (!connected_ && doScan_) {
    BLELOG("Scanning...\n");
    sys = SystemState::Scanning;
    BLEDevice::getScan()->start(0);
  }

  (void)tick;
}
