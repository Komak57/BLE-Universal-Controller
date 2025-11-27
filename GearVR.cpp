#include "GearVR.h"
#include "HID.h"
#include "USBHIDMouse.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"

// Simple immediate USB mapping; adjust to taste

extern USBHIDMouse Mouse;
extern USBHIDKeyboard Keyboard;
extern USBHIDConsumerControl ConsumerControl;

// UUIDs
BLEUUID GearVR::sService = BLEUUID("4f63756c-7573-2054-6872-65656d6f7465");
BLEUUID GearVR::sWrite = BLEUUID("c8c51726-81bc-483b-a052-f7a14ea3d282");
BLEUUID GearVR::sNotify = BLEUUID("c8c51726-81bc-483b-a052-f7a14ea3d281");
BLEUUID GearVR::sCCCD = BLEUUID("00002902-0000-1000-8000-00805f9b34fb");

// Commands
const uint8_t GearVR::kOff[2] = {0x00, 0x00};
const uint8_t GearVR::kSensor[2] = {0x01, 0x00};
const uint8_t GearVR::kFwUp[2] = {0x02, 0x00};
const uint8_t GearVR::kCal[2] = {0x03, 0x00};
const uint8_t GearVR::kKeep[2] = {0x04, 0x00};
const uint8_t GearVR::kUnk[2] = {0x05, 0x00};
const uint8_t GearVR::kLpmEn[2] = {0x06, 0x00};
const uint8_t GearVR::kLpmDis[2] = {0x07, 0x00};
const uint8_t GearVR::kVr[2] = {0x08, 0x00};

GearVR::GearVR()
{
    joy.Clear();
    lastjoy.Clear();
    config = PointerConfig();
}

bool GearVR::matchesAdvertisement(BLEAdvertisedDevice &dev)
{
    // Arduino String API (getName() is non-const)
    String name = dev.getName();
    if (name.length() == 0)
        return false;
    return name.startsWith("Gear VR Controller");
}

BLEUUID GearVR::serviceUuid() const { return sService; }
BLEUUID GearVR::writeCharUuid() const { return sWrite; }
BLEUUID GearVR::notifyCharUuid() const { return sNotify; }
BLEUUID GearVR::notifyDescriptorUuid() const { return sCCCD; }

bool GearVR::onConnected(BLEClient *client_)
{
    // Discover service/characteristics via handler UUIDs
    BLERemoteService *svc = client_->getService(serviceUuid());
    if (!svc)
    {
        GVLOG("Failed to find service\n");
        client_->disconnect();
        return false;
    }

    write_ = svc->getCharacteristic(writeCharUuid());
    notify_ = svc->getCharacteristic(notifyCharUuid());
    if (!write_ || !write_->canWrite())
    {
        GVLOG("Invalid write characteristic\n");
        client_->disconnect();
        return false;
    }
    if (!notify_ || !notify_->canNotify())
    {
        GVLOG("Invalid notify characteristic\n");
        client_->disconnect();
        return false;
    }
    
    client_->setMTU(63);
    delay(50);
    GVLOG("GearVR connected (MTU now=%u)\n", client_->getMTU());

    // Enable CCCD first
    notify_->registerForNotify(
        [this](BLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool isNotify) {
            this->onNotify(chr, data, len, isNotify);
        });
    BLERemoteDescriptor *d = notify_->getDescriptor(notifyDescriptorUuid());
    if (d)
    {
        queueCmd(kSensor);
        GVLOG("CCCD written: notifications enabled\n");
    }

    // queue Sensor first, not VR
    handshakeStage_ = 0; // new member to track progress
    handshakeTimer_ = 0;
}

void GearVR::onNotify(BLERemoteCharacteristic *chr, uint8_t *pData, size_t length, bool isNotify)
{
    if (!isNotify)
        return;

    // Short “command request” frames
    if (length < 60)
    {
        if (length >= 2)
        {
            uint8_t c0 = pData[0], c1 = pData[1];
            (void)c1;
            if (c0 == kOff[0])
            {
                GVLOG("Req: Off\n");
            }
            else if (c0 == kSensor[0])
            {
                GVLOG("Req: Sensor\n");
            }
            else if (c0 == kFwUp[0])
            {
                GVLOG("Req: FW Update\n");
            }
            else if (c0 == kCal[0])
            {
                GVLOG("Req: Calibrate\n");
            }
            else if (c0 == kKeep[0])
            {
                GVLOG("Req: KeepAlive\n");
            }
            else if (c0 == kUnk[0])
            {
                GVLOG("Req: Unknown\n");
            }
            else if (c0 == kLpmEn[0])
            {
                GVLOG("Req: LPM Enable → queue VR\n");
                queueCmd(kVr);
            }
            else if (c0 == kLpmDis[0])
            {
                GVLOG("Req: LPM Disable\n");
            }
            else if (c0 == kVr[0])
            {
                GVLOG("Req: VR Mode → queue Sensor\n");
                queueCmd(kSensor);
                // Re-enable CCCD after VR mode (device resets its internal stack)
                if (notify_)
                {
                    BLERemoteDescriptor *d = notify_->getDescriptor(sCCCD);
                    if (!d)
                        d = notify_->getDescriptor(BLEUUID((uint16_t)0x2902));
                    if (d)
                    {
                        uint8_t enable[2] = {0x01, 0x00};
                        d->writeValue(enable, 2, /*response=*/true);
                        GVLOG("Rewrote CCCD after VR mode\n");
                    }
                }
                // Try MTU upgrade again
                if (client_)
                {
                    client_->setMTU(63);
                    GVLOG("MTU after VR request: %u\n", client_->getMTU());
                }

                // Defer Sensor request until next loop
                queueCmd(kSensor);
                return;
            }
            if (length >= 4)
            {
                // Tick print
                uint32_t tick = (uint32_t)pData[3] << 24 | (uint32_t)pData[2] << 16 | (uint32_t)pData[1] << 8 | (uint32_t)pData[0];
                GVLOG("Tick: %u\n", tick);
            }
        }
        return; // BLE reply, if any, will be sent next frame
    }

    // Full controller packet
    if (!receiving_)
    {
        GVLOG("Receiving controller stream (len=%u)\n", (unsigned)length);
        receiving_ = true;
        streaming_ = true;
        keepaliveMs_ = 0;
    }

    lastjoy = joy;
    parseFullPacket(pData, length);
    emitUSB(joy, lastjoy); // USB actions can fire immediately
}

void GearVR::onDisconnected()
{
    GVLOG("GearVR disconnected\n");
    client_ = nullptr;
    write_ = nullptr;
    notify_ = nullptr;
    pending_ = false;
    pendingCmd_[0] = pendingCmd_[1] = 0x00;
    receiving_ = false;
    mode_ = 0x00;
}

void GearVR::queueCmd(const uint8_t cmd[2])
{
    pendingCmd_[0] = cmd[0];
    pendingCmd_[1] = cmd[1];
    pending_ = true; // will send on next manager update()
}

bool GearVR::hasPending() const { return pending_; }

void GearVR::trySendPending(BLERemoteCharacteristic *writeChr)
{
    if (!pending_ || !writeChr)
        return;

    // NO-RESPONSE write (false)
    bool resp = (pendingCmd_[0] == kSensor[0]);
    writeChr->writeValue((uint8_t *)pendingCmd_, 2, resp);

    GVLOG("GearVR: sent cmd 0x%02X 0x%02X%s\n",
          pendingCmd_[0], pendingCmd_[1],
          resp ? " (rsp)" : " (no-rsp)");

    mode_ = pendingCmd_[0];
    pending_ = false;
    pendingCmd_[0] = pendingCmd_[1] = 0;
}
void GearVR::parseFullPacket(uint8_t *p, size_t len)
{
    if (len < 60)
        return;
    //  20:47:06.013 -> === GearVR Raw Packet (60 bytes) ===
    //  20:47:06.013 ->
    //  20:47:06.013 -> 00: DC 6F 63 00 11 00 EC 01 10 08 FD FF 23 00 F5 FF
    //  20:47:06.013 -> 16: 83 82 63 00 0E 00 D5 01 FF 07 F9 FF 26 00 F3 FF
    //  20:47:06.013 -> 32: 11 95 63 00 01 00 DB 01 05 08 FC FF 1F 00 F5 FF
    //  20:47:06.013 -> 48: 21 F7 7C 05 B6 F8 20 00 00 17 40 43
    //  20:47:06.013 -> ===================================

    //  Serial.println("=== GearVR Raw Packet (60 bytes) ===");
    //  for (int i = 0; i < len; i++) {
    //    if (i % 16 == 0) Serial.printf("\n%02d: ", i);
    //    Serial.printf("%02X ", p[i]);
    //  }
    //  Serial.println("\n===================================");

    constexpr float ACC_LSB_TO_MS2 = 9.80665f / 2048.0f;
    for (int t = 0; t < 3; t++)
    {
        int base = t * 16;

        joy.sensor_time[t] = ((uint32_t)p[base + 3] << 24) |
                             ((uint32_t)p[base + 2] << 16) |
                             ((uint32_t)p[base + 1] << 8) |
                             ((uint32_t)p[base + 0]);

        // Accel: bytes 4..9  (little-endian)
        int16_t ax = (int16_t)(p[base + 4] | (p[base + 5] << 8));
        int16_t ay = (int16_t)(p[base + 6] | (p[base + 7] << 8));
        int16_t az = (int16_t)(p[base + 8] | (p[base + 9] << 8));

        // Gyro: bytes 10..15 (little-endian)
        int16_t gx = (int16_t)(p[base + 10] | (p[base + 11] << 8));
        int16_t gy = (int16_t)(p[base + 12] | (p[base + 13] << 8));
        int16_t gz = (int16_t)(p[base + 14] | (p[base + 15] << 8));

        joy.accel[t].x = ax * ACC_SCALE;
        joy.accel[t].y = ay * ACC_SCALE;
        joy.accel[t].z = az * ACC_SCALE;

        joy.gyro[t].x = gx * GYR_SCALE;
        joy.gyro[t].y = gy * GYR_SCALE;
        joy.gyro[t].z = gz * GYR_SCALE;
    }

    joy.magno.x = (((int16_t)p[48] << 8) | p[49]);
    joy.magno.y = (((int16_t)p[50] << 8) | p[51]);
    joy.magno.z = (((int16_t)p[52] << 8) | p[53]);

    joy.touchpad.x = (((p[54] & 0xF) << 6) | ((p[55] & 0xFC) >> 2)) & 0x3FF;
    joy.touchpad.y = (((p[55] & 0x3) << 8) | ((p[56] & 0xFF) >> 0)) & 0x3FF;

    joy.temperature = p[57];
    joy.triggerButton = (p[58] & 0x01) != 0;
    joy.homeButton = (p[58] & 0x02) != 0;
    joy.backButton = (p[58] & 0x04) != 0;
    joy.touchpad.button = (p[58] & 0x08) != 0;
    joy.volumeUpButton = (p[58] & 0x10) != 0;
    joy.volumeDownButton = (p[58] & 0x20) != 0;
    joy.battery = p[59];

    joy.updateCounts++;
    joy.lastUpdated = millis();

    // Weighted average of last 3 gyro frames for smoothness
    Axis3 gyro = Axis3();
    gyro.x = (joy.gyro[2].x * 0.6 + joy.gyro[1].x * 0.3 + joy.gyro[0].x * 0.1);
    gyro.y = (joy.gyro[2].y * 0.6 + joy.gyro[1].y * 0.3 + joy.gyro[0].y * 0.1);
    gyro.z = (joy.gyro[2].z * 0.6 + joy.gyro[1].z * 0.3 + joy.gyro[0].z * 0.1);

    Axis3 accel = Axis3();
    accel.x = (joy.accel[2].x * 0.6 + joy.accel[1].x * 0.3 + joy.accel[0].x * 0.1);
    accel.y = (joy.accel[2].y * 0.6 + joy.accel[1].y * 0.3 + joy.accel[0].y * 0.1);
    accel.z = (joy.accel[2].z * 0.6 + joy.accel[1].z * 0.3 + joy.accel[0].z * 0.1);
    // Calculate Pointer on a Simulated Screen

    // Δt in seconds
    float dt;
    if (joy.lastUpdated > lastjoy.lastUpdated)
        dt = (joy.lastUpdated - lastjoy.lastUpdated) * 1e-3f; // assuming counter units ≈ µs
    else
        dt = 1.0f / 120.0f; // fallback ≈120 Hz

    //  GVLOG("GearVR: gyro.x=%f, gyro.y=%f, gyro.z=%f\n", gyro.x, gyro.y, gyro.z);
    //  GVLOG("GearVR: accel.x=%f, accel.y=%f, accel.z=%f\n", accel.x, accel.y, accel.z);
    //  GVLOG("GearVR: magno.x=%f, magnoyro.y=%f, magno.z=%f\n", joy.magno.x, joy.magno.y, joy.magno.z);

    // Integrate gyro for fast changes
    joy.orient.roll += gyro.x * dt;
    joy.orient.pitch += gyro.y * dt;
    joy.orient.yaw += gyro.z * dt;

    // Compute accelerometer-based tilt (gravity)
    float accRoll = atan2(accel.y, accel.z);
    float accPitch = atan2(-accel.x, sqrt(accel.y * accel.y + accel.z * accel.z));

    // Complementary filter blend
    const float alpha = 0.98f;
    joy.orient.roll = alpha * joy.orient.roll + (1 - alpha) * accRoll;
    joy.orient.pitch = alpha * joy.orient.pitch + (1 - alpha) * accPitch;

    //  float heading = atan2(joy.magno.y, joy.magno.x);
    //  joy.orient.yaw = alpha * joy.orient.yaw + (1 - alpha) * heading;
}

void GearVR::emitUSB(const JoyData &now, const JoyData &prev)
{
    // ===== Mouse movement via touchpad =====
    // Detect first touch contact to sync baseline
    if (lastjoy.touchpad.x == 0 && lastjoy.touchpad.y == 0)
    {
        lastjoy.touchpad.x = joy.touchpad.x;
        lastjoy.touchpad.y = joy.touchpad.y;
    }
    if (joy.touchpad.x > 0 && joy.touchpad.y > 0)
    {
        int dx = joy.touchpad.x - lastjoy.touchpad.x;
        int dy = joy.touchpad.y - lastjoy.touchpad.y;
        if (abs(dx) > 1 || abs(dy) > 1)
        {
            if (joy.usePad)
                Mouse.move(dx, dy, 0);
        }
    }
    if (!joy.usePad)
    {
        // Calculate mouse position on simulated screen
        float dYaw = joy.orient.yaw - joy.reference.yaw;
        float dPitch = joy.orient.pitch - joy.reference.pitch;
        float screenX = tan(dYaw) * config.screenDistance;
        float screenY = tan(dPitch) * config.screenDistance;
        // Normalize
        float normX = (screenX / (config.screenWidth / 2.0f));
        float normY = (screenY / (config.screenHeight / 2.0f));
        normX = constrain(normX, -1.0f, 1.0f);
        normY = constrain(normY, -1.0f, 1.0f);
        // Calculate Mouse Movement
        int mouseX = (int)(normX * 500); // adjust scaling for pixel speed
        int mouseY = (int)(-normY * 500);
        Mouse.move(mouseX, mouseY, 0);
    }
    // ===== Trigger = Mouse Left =====
    if (joy.triggerButton && !lastjoy.triggerButton)
    {
        Mouse.press(MOUSE_LEFT);
    }
    if (!joy.triggerButton && lastjoy.triggerButton)
    {
        Mouse.release(MOUSE_LEFT);
    }

    // ===== Touch button alone = directional hotkeys =====
    if (joy.touchpad.button && !joy.triggerButton && !lastjoy.touchpad.button)
    {
        // Center reference is around 160,160 (10-bit, 0–315 range)
        int dx = joy.touchpad.x - 160;
        int dy = joy.touchpad.y - 160;

        if (abs(dx) < 60 && abs(dy) < 60)
        {
            // Center tap could be ignored or used for future
            joy.reference = joy.orient;
            joy.usePad = !joy.usePad;
            if (!joy.usePad)
                Serial.println("Using Gyro");
            else
                Serial.println("Using TouchPad");
        }
        else if (abs(dx) > abs(dy))
        {
            if (dx > 0)
            {
                GVLOG("Touch Right → Skip Forward\n");
                ConsumerControl.press(MEDIA_FORWARD);
            }
            else
            {
                GVLOG("Touch Left → Skip Back\n");
                ConsumerControl.press(MEDIA_BACKWARD);
            }
        }
        else
        {
            if (dy > 0)
            {
                GVLOG("Touch Down → Play/Pause\n");
                ConsumerControl.press(MEDIA_PLAY_PAUSE);
                ;
            }
            else
            {
                GVLOG("Touch Up → Alt+Tab\n");
                Keyboard.press(KEYCODE_LEFT_ALT);
                delay(10);
                Keyboard.press(KEYCODE_TAB);
            }
        }
    }

    // Release any held directional or media key when touch released
    if (!joy.touchpad.button && lastjoy.touchpad.button)
    {
        ConsumerControl.release();
        Keyboard.releaseAll();
    }

    // ===== Volume and Home/Back =====
    if (joy.volumeUpButton && !lastjoy.volumeUpButton)
        ConsumerControl.press(MEDIA_VOLUME_UP);
    if (!joy.volumeUpButton && lastjoy.volumeUpButton)
        ConsumerControl.release();

    if (joy.volumeDownButton && !lastjoy.volumeDownButton)
        ConsumerControl.press(MEDIA_VOLUME_DOWN);
    if (!joy.volumeDownButton && lastjoy.volumeDownButton)
        ConsumerControl.release();

    if (joy.homeButton && !lastjoy.homeButton)
        ConsumerControl.press(MEDIA_HOME);
    if (!joy.homeButton && lastjoy.homeButton)
        ConsumerControl.release();

    if (joy.backButton && !lastjoy.backButton)
        ConsumerControl.press(MEDIA_BACK);
    if (!joy.backButton && lastjoy.backButton)
        ConsumerControl.release();
}

void GearVR::update(uint32_t tick)
{
    GVLOG("8\n");
    if (handshakeStage_ == 0)
    {
        handshakeTimer_ += tick;
        if (handshakeStage_ == 0 && handshakeTimer_ >= 300)
        {
            // after 150 ms of Sensor request
            GVLOG("No stream → try VR mode\n");
            queueCmd(kVr);
            handshakeStage_ = 1;
            handshakeTimer_ = 0;
        }
    }
    //  // existing keepalive...
    //  if (!streaming_) {
    //    keepaliveMs_ += tick;
    //    if (keepaliveMs_ >= 5000) {
    //      keepaliveMs_ = 0;
    //      queueCmd(kKeep);
    //      GVLOG("KA\n");
    //    }
    //  }
    if (hasPending())
    {
        trySendPending(write_); // next-frame BLE write
    }
}
