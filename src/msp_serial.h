#pragma once
#include <Arduino.h>
#include "telemetry.h"

// ─── MSP command IDs ─────────────────────────────────────────────────────────
// Legacy MSP v1 IDs (accepted in MSP v2 frames):
#define MSP_STATUS                  101   // poll FC arm / flight-mode state

// Betaflight text field setter (msp_protocol_v2_betaflight.h: 0x3007).
// Direction '<' (command). Payload: textType(1) + textLen(1) + text bytes.
#define MSP2_SET_TEXT               0x3007

// Text type values for MSP2_SET_TEXT / MSP2_GET_TEXT
#define MSP_TEXT_CUSTOM_1           7   // "Custom Message 1" OSD field
#define MSP_TEXT_CUSTOM_2           8   // "Custom Message 2" OSD field

// Custom vendor command — carries DJI Action camera battery state.
// Receivers that don't recognise 0x3001 silently ignore the frame.
#define MSP2_CAMERA_BATTERY         0x3001

// ─── MSP v2 payload structs ───────────────────────────────────────────────────
// Packed so memcpy onto the wire is safe regardless of compiler padding.

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

    // Build and send MSP2_CAMERA_BATTERY (0x3001) from a BatteryData snapshot.
    void sendCameraBattery(const BatteryData &data);

    // Format battery % as "CAM:NNN%" and push as Betaflight Custom Message 1.
    void sendBatteryAsCustomMessage(const BatteryData &data);

    // Push "REC" or "IDLE" as Betaflight Custom Message 2.
    void sendRecordingAsCustomMessage(const BatteryData &data);

    bool isArmed() const { return _armed; }
    void setArmCallback(ArmCallback cb) { _armCb = cb; }

private:
    // ── TX ────────────────────────────────────────────────────────────────
    // MSP v2 frame: $ X dir | flag(1) cmd(2LE) size(2LE) payload | crc8
    // dir='>' for push (sensor injection); dir='<' for commands (SET_TEXT etc.)
    void sendFrame(uint16_t cmd, const uint8_t *payload, uint16_t length, char dir = '>');
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
