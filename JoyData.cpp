#include "JoyData.h"
#include <cstring>

JoyData::JoyData()
{
    Clear();
}

void JoyData::Clear()
{
    time_stamp = millis();
    lastUpdated = 0;
    updateCounts = 0;

    memset(sensor_time, 0, sizeof(sensor_time));
    gyro[0] = Axis3{};
    gyro[1] = Axis3{};
    gyro[2] = Axis3{};
    accel[0] = Axis3{};
    accel[1] = Axis3{};
    accel[2] = Axis3{};

    magno = Axis3{};
    touchpad = TouchAxis{};

    temperature = 0;
    triggerButton = false;
    homeButton = false;
    backButton = false;
    volumeUpButton = false;
    volumeDownButton = false;
    battery = 0;

    usePad = true;
    reference = Orientation();
    orient = Orientation();
}
