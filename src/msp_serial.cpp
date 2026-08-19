#include "msp_serial.h"
#include "config.h"
#include <cstring>

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

// ─── Public ──────────────────────────────────────────────────────────────────

void MSPSerial::sendGPS(const TelemetryData &data) {
    MSP2GpsPayload p{};

    p.instance              = 0;
    p.gpsWeek               = 0;           // unknown — Betaflight uses UTC fields
    p.msTOW                 = 0;
    p.fixType               = data.fixType;
    p.satellitesInView      = data.numSatellites;
    p.horizontalPosAccuracy = (data.hdop * 10);  // hdop*100 → approx mm accuracy
    p.verticalPosAccuracy   = (data.hdop * 15);
    p.horizontalVelAccuracy = 500;               // 5 m/s — conservative default
    p.hdop                  = data.hdop;
    p.longitude             = data.longitude;
    p.latitude              = data.latitude;
    p.mslAltitude           = data.altitudeMSL;
    p.nedVelNorth           = data.velNorth;
    p.nedVelEast            = data.velEast;
    p.nedVelDown            = data.velDown;
    p.groundCourse          = data.groundCourse;
    p.trueYaw               = MSP_GPS_YAW_INVALID;
    p.year                  = data.year;
    p.month                 = data.month;
    p.day                   = data.day;
    p.hour                  = data.hour;
    p.min                   = data.minute;
    p.sec                   = data.second;

    sendFrame(MSP2_SENSOR_GPS,
              reinterpret_cast<const uint8_t *>(&p),
              sizeof(p));
}

// ─── MSP v2 framing ──────────────────────────────────────────────────────────
//
// Wire format:
//   '$'  'X'  '>'  flag(1)  cmd(2 LE)  size(2 LE)  payload(size)  crc8(1)
//
// CRC8/DVB-S2 covers: flag + cmd[0] + cmd[1] + size[0] + size[1] + payload
//
// Betaflight CLI setup:
//   serial <N> 0 115200 8 0 0 0    # configure UART N as MSP
//   feature GPS
//   set gps_provider = MSP
//   save
//
void MSPSerial::sendFrame(uint16_t cmd, const uint8_t *payload, uint16_t length) {
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
    _serial->write('>');
    _serial->write(FLAG);
    _serial->write(cmdLo);
    _serial->write(cmdHi);
    _serial->write(sizeLo);
    _serial->write(sizeHi);
    _serial->write(payload, length);
    _serial->write(crc);
}

void MSPSerial::sendCameraBattery(const BatteryData &data) {
    MSP2CameraBatteryPayload p{};
    p.percent     = data.percent;
    p.voltage     = data.voltage;
    p.current     = data.current;
    p.remaining   = data.remaining;
    p.capacity    = data.capacity;
    p.temperature = data.temperature;
    p.cellCount   = data.cellCount;

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
