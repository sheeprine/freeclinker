#pragma once
#include <stdint.h>

struct BatteryData {
    bool     valid;
    uint8_t  percent;      // 0–100 %
    uint16_t voltage;      // mV        (0 = unknown)
    int16_t  current;      // mA, positive = discharging  (0 = unknown)
    uint16_t remaining;    // mAh remaining  (0 = unknown)
    uint16_t capacity;     // mAh total      (0 = unknown)
    int8_t   temperature;  // °C  (INT8_MIN = unknown)
    uint8_t  cellCount;    // 0 = unknown
    bool     recording;    // true while camera_status == 0x03
};
