#pragma once
#include <stdint.h>

// Normalised telemetry snapshot passed between the BLE layer and the serial
// output layer.  All fields use SI-adjacent units that match the MSP v2 sensor
// wire format to keep the conversion trivial.
struct TelemetryData {
    bool     valid;          // false until at least one GPS fix is parsed

    // GPS fix
    uint8_t  fixType;        // 0 = no fix, 2 = 2-D, 3 = 3-D
    uint8_t  numSatellites;
    int32_t  latitude;       // degrees × 1 × 10⁷  (positive = North)
    int32_t  longitude;      // degrees × 1 × 10⁷  (positive = East)
    int32_t  altitudeMSL;    // centimetres above mean sea level
    int32_t  velNorth;       // cm/s  (NED frame)
    int32_t  velEast;        // cm/s
    int32_t  velDown;        // cm/s
    uint16_t groundSpeed;    // cm/s
    uint16_t groundCourse;   // degrees × 100
    uint16_t hdop;           // × 100  (e.g. 120 = HDOP 1.20)

    // UTC time (0 when unknown)
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
};

struct BatteryData {
    bool     valid;
    uint8_t  percent;      // 0–100 %
    uint16_t voltage;      // mV        (0 = unknown)
    int16_t  current;      // mA, positive = discharging  (0 = unknown)
    uint16_t remaining;    // mAh remaining  (0 = unknown)
    uint16_t capacity;     // mAh total      (0 = unknown)
    int8_t   temperature;  // °C  (INT8_MIN = unknown)
    uint8_t  cellCount;    // 0 = unknown
};
