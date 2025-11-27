#include <Arduino.h>
#include <USB.h>
#include <USBHID.h>
#include <USBHIDMouse.h>
#include <USBHIDKeyboard.h>
#include <USBHIDConsumerControl.h>
#include "Helper.h"

#include "BLEManager.h"
#include "GearVR.h"

#define RGB_BRIGHTNESS 16

USBHID HID;
USBHIDMouse Mouse;
USBHIDKeyboard Keyboard;
USBHIDConsumerControl ConsumerControl;

BLEManager bt;
GearVR gear;

SystemState sys = SystemState::Idle;

uint32_t lastMs = 0, blink = 0;

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("ESP32-S3 Universal Controller Adapter");

    pinMode(RGB_BUILTIN, OUTPUT);
    neopixelWrite(RGB_BUILTIN, 0, 0, 0);

    HID.begin();
    USB.productName("Universal HID Adapter");
    USB.manufacturerName("Espressif");
    USB.serialNumber("0001");
    Mouse.begin();           // Mouse emulation
    Keyboard.begin();        // Keyboard keys (Alt-Tab, arrows, etc.)
    ConsumerControl.begin(); // Media keys (volume, etc.)
    USB.begin();
    Serial.println("USB HID Ready");
    bt = BLEManager();
    // Register exactly one device type for now (can add more later)
    bt.registerHandler(&gear);

    bt.init();
}

void loop()
{
    uint32_t now = millis();
    uint32_t tick = (lastMs == 0) ? 0 : (now - lastMs);
    lastMs = now;

    bt.update(tick);

    // Blink while not connected
    if (sys == SystemState::Connected)
    {
        neopixelWrite(RGB_BUILTIN, 0, RGB_BRIGHTNESS, 0);
    }
    else if (sys == SystemState::Scanning)
    {
        blink += tick;
        if (blink >= 500)
        {
            blink = 0;
            neopixelWrite(RGB_BUILTIN, 0, 0, RGB_BRIGHTNESS);
            delay(15);
            neopixelWrite(RGB_BUILTIN, 0, 0, 0);
        }
    }
    else
    {
        neopixelWrite(RGB_BUILTIN, 0, 0, 0);
    }

    delay(1);
}
