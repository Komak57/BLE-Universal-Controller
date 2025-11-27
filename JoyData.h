#pragma once
#include <Arduino.h>
#include <cstdint>

struct TouchAxis {
  int x = 0;
  int y = 0;
  bool button = false;
};

struct Axis3 {
  double x = 0;
  double y = 0;
  double z = 0;
};

struct Orientation {
  float roll = 0, pitch = 0, yaw = 0;
};

class JoyData {
public:
  JoyData();

  // timestamps / counters
  uint32_t time_stamp = 0;
  uint32_t lastUpdated = 0;
  uint32_t updateCounts = 0;

  // motion
  uint32_t sensor_time[3];
  Axis3 gyro[3];
  Axis3 accel[3];
  
  Axis3 magno;
  TouchAxis touchpad;

  // inputs
  uint8_t temperature  = 0;
  bool triggerButton   = false;
  bool homeButton      = false;
  bool backButton      = false;
  bool volumeUpButton  = false;
  bool volumeDownButton= false;
  uint8_t battery      = 0;

  bool usePad;
  Orientation reference;
  Orientation orient;
  
  // Device-specific parsing is done by the device handler
  void Clear();
};
