#include "msp_serial.h"
#include "config.h"
#include <cstring>
#include <cstdio>

void MSPSerial::begin(HardwareSerial &serial) {
    _serial = &serial;
}

// ─── update() — call from loop() ─────────────────────────────────────────────

void MSPSerial::update() {
    // Poll arm state at 10 Hz
    if (millis() - _lastPollMs >= 100) {
        _lastPollMs = millis();
        sendRequest(MSP_STATUS);
    }
    while (_serial->available())
        feedByte(static_cast<uint8_t>(_serial->read()));
}

// ─── TX: request ─────────────────────────────────────────────────────────────

void MSPSerial::sendRequest(uint16_t cmd) {
    constexpr uint8_t FLAG    = 0x00;
    const     uint8_t cmdLo   = cmd & 0xFF;
    const     uint8_t cmdHi   = cmd >> 8;
    constexpr uint8_t sizeLo  = 0x00;
    constexpr uint8_t sizeHi  = 0x00;

    uint8_t crc = 0;
    crc = crc8DvbS2(crc, FLAG);
    crc = crc8DvbS2(crc, cmdLo);
    crc = crc8DvbS2(crc, cmdHi);
    crc = crc8DvbS2(crc, sizeLo);
    crc = crc8DvbS2(crc, sizeHi);

    _serial->write('$');
    _serial->write('X');
    _serial->write('<');
    _serial->write(FLAG);
    _serial->write(cmdLo);
    _serial->write(cmdHi);
    _serial->write(sizeLo);
    _serial->write(sizeHi);
    _serial->write(crc);
}

// ─── RX parser ───────────────────────────────────────────────────────────────

void MSPSerial::feedByte(uint8_t b) {
    switch (_rxState) {
    case RxState::IDLE:
        if (b == '$') _rxState = RxState::HDR_X;
        break;
    case RxState::HDR_X:
        _rxState = (b == 'X') ? RxState::HDR_DIR : RxState::IDLE;
        break;
    case RxState::HDR_DIR:
        _rxState = (b == '>') ? RxState::FLAG : RxState::IDLE;
        break;
    case RxState::FLAG:
        _rxCrc   = crc8DvbS2(0, b);
        _rxState = RxState::CMD_LO;
        break;
    case RxState::CMD_LO:
        _rxCmd   = b;
        _rxCrc   = crc8DvbS2(_rxCrc, b);
        _rxState = RxState::CMD_HI;
        break;
    case RxState::CMD_HI:
        _rxCmd  |= static_cast<uint16_t>(b) << 8;
        _rxCrc   = crc8DvbS2(_rxCrc, b);
        _rxState = RxState::SZ_LO;
        break;
    case RxState::SZ_LO:
        _rxSize  = b;
        _rxCrc   = crc8DvbS2(_rxCrc, b);
        _rxState = RxState::SZ_HI;
        break;
    case RxState::SZ_HI:
        _rxSize |= static_cast<uint16_t>(b) << 8;
        _rxCrc   = crc8DvbS2(_rxCrc, b);
        _rxPos   = 0;
        _rxState = (_rxSize == 0) ? RxState::CRC : RxState::PAYLOAD;
        break;
    case RxState::PAYLOAD:
        if (_rxPos < RX_BUF_SIZE) _rxBuf[_rxPos] = b;
        _rxCrc = crc8DvbS2(_rxCrc, b);
        if (++_rxPos >= _rxSize) _rxState = RxState::CRC;
        break;
    case RxState::CRC:
        if (b == _rxCrc) processResponse();
        _rxState = RxState::IDLE;
        break;
    }
}

void MSPSerial::processResponse() {
    if (_rxCmd != MSP_STATUS || _rxSize < 10) return;

    // MSP_STATUS payload layout:
    //   [0-1]  cycleTime      uint16
    //   [2-3]  i2cErrorCount  uint16
    //   [4-5]  sensorStatus   uint16
    //   [6-9]  flightModeFlags uint32  ← bit 0 = ARM box active
    uint32_t flags = 0;
    memcpy(&flags, _rxBuf + 6, sizeof(flags));
    const bool armed = (flags & 0x01) != 0;

    if (armed != _armed) {
        _armed = armed;
        if (_armCb) _armCb(_armed);
    }
}

// ─── MSP v2 framing ──────────────────────────────────────────────────────────
// Wire format: '$'  'X'  dir  flag(1)  cmd(2 LE)  size(2 LE)  payload(size)  crc8(1)
// CRC8/DVB-S2 covers: flag + cmd[0] + cmd[1] + size[0] + size[1] + payload
void MSPSerial::sendFrame(uint16_t cmd, const uint8_t *payload, uint16_t length, char dir) {
    constexpr uint8_t FLAG = 0x00;

    const uint8_t cmdLo  = cmd    & 0xFF;
    const uint8_t cmdHi  = cmd    >> 8;
    const uint8_t sizeLo = length & 0xFF;
    const uint8_t sizeHi = length >> 8;

    // CRC over flag + cmd(2) + size(2) + payload
    uint8_t crc = 0;
    crc = crc8DvbS2(crc, FLAG);
    crc = crc8DvbS2(crc, cmdLo);
    crc = crc8DvbS2(crc, cmdHi);
    crc = crc8DvbS2(crc, sizeLo);
    crc = crc8DvbS2(crc, sizeHi);
    crc = crc8DvbS2Buf(crc, payload, length);

    // Write atomically — UART TX FIFO on ESP32 is 128 bytes, well above our 56-byte frame
    _serial->write('$');
    _serial->write('X');
    _serial->write(static_cast<uint8_t>(dir));
    _serial->write(FLAG);
    _serial->write(cmdLo);
    _serial->write(cmdHi);
    _serial->write(sizeLo);
    _serial->write(sizeHi);
    _serial->write(payload, length);
    _serial->write(crc);
}

void MSPSerial::sendCustomText(uint8_t textType, const char *text) {
    const uint8_t textLen = static_cast<uint8_t>(strlen(text));
    uint8_t buf[1 + 1 + 16];   // type(1) + length(1) + up to 16 chars
    buf[0] = textType;
    buf[1] = textLen;
    memcpy(buf + 2, text, textLen);
    sendFrame(MSP2_SET_TEXT, buf, 2 + textLen, '<');
}

void MSPSerial::sendBatteryMsg(const CameraData &data) {
    char text[12];
    if (data.valid)
        snprintf(text, sizeof(text), "CAM:%3u%%", data.percent);
    else
        snprintf(text, sizeof(text), "CAM:---");
    sendCustomText(MSP_TEXT_CUSTOM_1, text);
}

void MSPSerial::sendRecordingMsg(const CameraData &data) {
    char text[12];
    if (data.temp_over >= 2) {
        snprintf(text, sizeof(text), "CAM:HOT");
    } else if (data.recording) {
        uint16_t mins = data.record_time / 60;
        uint8_t  secs = data.record_time % 60;
        snprintf(text, sizeof(text), "REC%3u:%02u", mins, secs);
    } else {
        snprintf(text, sizeof(text), "IDLE");
    }
    sendCustomText(MSP_TEXT_CUSTOM_2, text);
}

void MSPSerial::sendCameraStatus(const CameraData &data) {
    MSP2CameraPayload p{};
    p.percent       = data.percent;
    p.camera_mode   = data.camera_mode;
    p.recording     = data.recording ? 1 : 0;
    p.temp_over     = data.temp_over;
    p.eis_mode      = data.eis_mode;
    p.record_time   = data.record_time;
    p.remain_cap_mb = data.remain_cap_mb;
    p.remain_time   = data.remain_time;

    sendFrame(MSP2_CAMERA_BATTERY,
              reinterpret_cast<const uint8_t *>(&p),
              sizeof(p));
}

// ─── CRC8/DVB-S2 (polynomial 0xD5) ──────────────────────────────────────────

uint8_t MSPSerial::crc8DvbS2(uint8_t crc, uint8_t byte) {
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x80) ? (crc << 1) ^ 0xD5 : (crc << 1);
    }
    return crc;
}

uint8_t MSPSerial::crc8DvbS2Buf(uint8_t crc,
                                  const uint8_t *buf, uint16_t length) {
    for (uint16_t i = 0; i < length; i++) {
        crc = crc8DvbS2(crc, buf[i]);
    }
    return crc;
}
