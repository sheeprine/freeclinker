#pragma once
#include <Arduino.h>
#include "telemetry.h"

// ─── MSP command IDs ─────────────────────────────────────────────────────────
// Legacy MSP v1 IDs (accepted in MSP v2 frames):
#define MSP_STATUS                  101   // poll FC arm / flight-mode state

// Standard Betaflight sensor-injection commands (0x1Fxx range):
#define MSP2_SENSOR_GPS             0x1F03
#define MSP2_SENSOR_BAROMETER       0x1F01
#define MSP2_SENSOR_MAGNETOMETER    0x1F02

// Betaflight 4.5+ text field setter (see src/main/msp/msp.h in BF source).
// Payload: textType(1) + text bytes (length from MSP size field, no null).
#define MSP2_SET_TEXT               0x1031

// Text type values for MSP2_SET_TEXT / MSP2_GET_TEXT
#define MSP_TEXT_PILOT_NAME         0
#define MSP_TEXT_CRAFT_NAME         1
#define MSP_TEXT_PID_PROFILE_NAME   2
#define MSP_TEXT_RATE_PROFILE_NAME  3
#define MSP_TEXT_HARDWARE_NAME      4
#define MSP_TEXT_CUSTOM_1           5   // "Custom Message 1" OSD element
#define MSP_TEXT_CUSTOM_2           6   // "Custom Message 2" OSD element

// Custom vendor command — carries DJI Action camera battery state.
// Receivers that don't recognise 0x3001 silently ignore the frame.
#define MSP2_CAMERA_BATTERY         0x3001

// GPS fix-type values expected by Betaflight
#define MSP_GPS_FIX_NONE    0
#define MSP_GPS_FIX_2D      2
#define MSP_GPS_FIX_3D      3

// Sentinel for trueYaw when heading is unavailable
#define MSP_GPS_YAW_INVALID 36000

// ─── MSP v2 payload structs ───────────────────────────────────────────────────
// Packed so memcpy onto the wire is safe regardless of compiler padding.

struct __attribute__((packed)) MSP2GpsPayload {
    uint8_t  instance;                // sensor instance index (0)
    uint16_t gpsWeek;                 // GPS week number (0 if unknown)
    uint32_t msTOW;                   // time-of-week in ms (0 if unknown)
    uint8_t  fixType;                 // MSP_GPS_FIX_*
    uint8_t  satellitesInView;
    uint16_t horizontalPosAccuracy;   // mm
    uint16_t verticalPosAccuracy;     // mm
    uint16_t horizontalVelAccuracy;   // mm/s
    uint16_t hdop;                    // * 100
    int32_t  longitude;               // degrees * 1e7
    int32_t  latitude;                // degrees * 1e7
    int32_t  mslAltitude;             // cm
    int32_t  nedVelNorth;             // cm/s
    int32_t  nedVelEast;              // cm/s
    int32_t  nedVelDown;              // cm/s
    uint16_t groundCourse;            // degrees * 100
    uint16_t trueYaw;                 // degrees * 100, 36000 = invalid
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  min;
    uint8_t  sec;
};
static_assert(sizeof(MSP2GpsPayload) == 48, "GPS payload size mismatch");

// MSP2_CAMERA_BATTERY (0x3001) payload — custom vendor message.
struct __attribute__((packed)) MSP2CameraBatteryPayload {
    uint8_t  percent;      // 0–100 %
    uint16_t voltage;      // mV
    int16_t  current;      // mA  (positive = discharging)
    uint16_t remaining;    // mAh remaining
    uint16_t capacity;     // mAh total
    int8_t   temperature;  // °C  (INT8_MIN = unknown)
    uint8_t  cellCount;    // 0 = unknown
};
static_assert(sizeof(MSP2CameraBatteryPayload) == 11, "Battery payload size mismatch");

// Arm-state change notification — called on the main task (from update()).
using ArmCallback = void (*)(bool armed);

// ─────────────────────────────────────────────────────────────────────────────

class MSPSerial {
public:
    void begin(HardwareSerial &serial);

    // Call from loop() — polls FC for arm state and drains RX bytes.
    void update();

    // Build and send MSP2_SENSOR_GPS from a TelemetryData snapshot.
    void sendGPS(const TelemetryData &data);

    // Build and send MSP2_CAMERA_BATTERY (0x3001) from a BatteryData snapshot.
    void sendCameraBattery(const BatteryData &data);

    // Format battery % as "CAM:NNN%" and push as Betaflight Custom Message 1.
    void sendBatteryAsCustomMessage(const BatteryData &data);

    bool isArmed() const { return _armed; }
    void setArmCallback(ArmCallback cb) { _armCb = cb; }

private:
    // ── TX ────────────────────────────────────────────────────────────────
    // MSP v2 push frame:    $ X > | flag(1) cmd(2LE) size(2LE) payload | crc8
    void sendFrame(uint16_t cmd, const uint8_t *payload, uint16_t length);
    // MSP v2 request frame: $ X < | flag(1) cmd(2LE) size(2LE) [empty] | crc8
    void sendRequest(uint16_t cmd);
    // MSP2_SET_TEXT helper: type(1) + text bytes
    void sendCustomText(uint8_t textType, const char *text);

    // ── RX parser ─────────────────────────────────────────────────────────
    void feedByte(uint8_t b);
    void processResponse();

    enum class RxState : uint8_t {
        IDLE, HDR_X, HDR_DIR, FLAG, CMD_LO, CMD_HI, SZ_LO, SZ_HI, PAYLOAD, CRC
    };

    static constexpr uint8_t RX_BUF_SIZE = 32;
    RxState  _rxState = RxState::IDLE;
    uint8_t  _rxBuf[RX_BUF_SIZE]{};
    uint16_t _rxCmd   = 0;
    uint16_t _rxSize  = 0;
    uint16_t _rxPos   = 0;
    uint8_t  _rxCrc   = 0;

    // ── CRC ───────────────────────────────────────────────────────────────
    static uint8_t crc8DvbS2(uint8_t crc, uint8_t byte);
    static uint8_t crc8DvbS2Buf(uint8_t seed,
                                  const uint8_t *buf, uint16_t length);

    // ── State ─────────────────────────────────────────────────────────────
    HardwareSerial *_serial     = nullptr;
    bool            _armed      = false;
    ArmCallback     _armCb      = nullptr;
    uint32_t        _lastPollMs = 0;
};
