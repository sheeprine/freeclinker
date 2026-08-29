#include "msp_serial.h"
#include "config.h"
#include <cstring>
#include <cstdio>

void MSPSerial::begin(HardwareSerial &serial) {
    _serial = &serial;
}

// ─── update() — call from loop() ─────────────────────────────────────────────

void MSPSerial::update() {
    if (millis() - _lastPollMs >= 100) {
        _lastPollMs = millis();
        sendRequest(MSP_STATUS);
        if (_auxChannel > 0) sendRequest(MSP_RC);
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
    switch (_rxCmd) {
        case MSP_STATUS: handleStatusResponse(); break;
        case MSP_RC:     handleRcResponse();     break;
    }
}

void MSPSerial::handleStatusResponse() {
    // MSP_STATUS payload layout:
    //   [0-1]  cycleTime       uint16
    //   [2-3]  i2cErrorCount   uint16
    //   [4-5]  sensorStatus    uint16
    //   [6-9]  flightModeFlags uint32  ← bit 0 = ARM box active
    if (_rxSize < 10) return;
    uint32_t flags = 0;
    memcpy(&flags, _rxBuf + 6, sizeof(flags));
    const bool armed = (flags & 0x01) != 0;
    if (armed != _armed) {
        _armed = armed;
        if (_armCb) _armCb(_armed);
    }
}

void MSPSerial::handleRcResponse() {
    // MSP_RC payload: N × uint16 LE — Roll, Pitch, Yaw, Throttle, AUX1, AUX2, …
    // AUX channel N maps to RC index (3 + N), i.e. AUX1 → index 4.
    if (_auxChannel == 0) return;
    const uint8_t rcIdx = 3 + _auxChannel;  // AUX1=4, AUX2=5, …
    if (_rxSize < static_cast<uint16_t>(rcIdx + 1) * 2) return;
    uint16_t value = 0;
    memcpy(&value, _rxBuf + rcIdx * 2, sizeof(value));
    const bool high = (value > 1500);
    if (high != _auxHigh) {
        _auxHigh = high;
        if (_auxSwitchCb) _auxSwitchCb(high);
    }
}

void MSPSerial::setAuxChannel(uint8_t channel) {
    if (channel != _auxChannel) {
        _auxChannel = channel;
        _auxHigh    = false;  // reset edge state on channel change
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

// ─── OSD template engine ─────────────────────────────────────────────────────

static void resolveToken(const char *tok, const CameraData &data,
                         char *val, size_t valLen) {
    static const char * const res_labels[] = {
        "480p", "720p", "1080", "1440", "2.7K", "4K", "4KW", "5.1K", "5.3K", "8K",
    };
    static const char * const fps_labels[] = {
        "24", "25", "30", "48", "50", "60", "90", "100", "120", "200", "240", "400",
    };
    static const char * const eis_labels[] = {
        "OFF", "RS", "HS", "RS+", "HB", "LOW", "HI", "BST", "ABS", "STD",
    };

    val[0] = '\0';

    if (strcmp(tok, "bat") == 0) {
        if (data.valid) snprintf(val, valLen, "%3u%%", data.percent);
        else            snprintf(val, valLen, "---");
    } else if (strcmp(tok, "rec") == 0) {
        if (!data.valid) {
            snprintf(val, valLen, "NC");
        } else if (data.temp_over >= 2) {
            snprintf(val, valLen, "CAM:HOT");
        } else if (data.recording) {
            uint16_t mins = data.record_time / 60;
            uint8_t  secs = data.record_time % 60;
            snprintf(val, valLen, "REC%3u:%02u", mins, secs);
        } else {
            snprintf(val, valLen, "IDLE");
        }
    } else if (strcmp(tok, "mode") == 0) {
        if (!data.valid) { snprintf(val, valLen, "---"); return; }
        // 3-char left-padded to preserve OSD column alignment
        switch (data.camera_mode) {
            case 0x00: snprintf(val, valLen, "SLO"); break;
            case 0x01: snprintf(val, valLen, "VID"); break;
            case 0x02: snprintf(val, valLen, "TL "); break;
            case 0x05: snprintf(val, valLen, "PHO"); break;
            case 0x0A: snprintf(val, valLen, "HYP"); break;
            default:   snprintf(val, valLen, "---"); break;
        }
    } else if (strcmp(tok, "res") == 0) {
        if (!data.valid) { snprintf(val, valLen, "---"); return; }
        const char *r = (data.resolution < sizeof(res_labels) / sizeof(res_labels[0]))
                      ? res_labels[data.resolution] : "---";
        snprintf(val, valLen, "%s", r);
    } else if (strcmp(tok, "fps") == 0) {
        if (!data.valid) { snprintf(val, valLen, "--"); return; }
        const char *f = (data.fps_idx < sizeof(fps_labels) / sizeof(fps_labels[0]))
                      ? fps_labels[data.fps_idx] : "--";
        snprintf(val, valLen, "%s", f);
    } else if (strcmp(tok, "eis") == 0) {
        if (!data.valid) { snprintf(val, valLen, "---"); return; }
        const char *e = (data.eis_mode < sizeof(eis_labels) / sizeof(eis_labels[0]))
                      ? eis_labels[data.eis_mode] : "---";
        snprintf(val, valLen, "%s", e);
    } else if (strcmp(tok, "rleft") == 0) {
        if (!data.valid || data.remain_time == 0) {
            snprintf(val, valLen, "---");
        } else {
            uint32_t mins = data.remain_time / 60;
            if (mins >= 60) {
                uint32_t hrs = mins / 60;
                snprintf(val, valLen, "%luh%02lum",
                         (unsigned long)hrs, (unsigned long)(mins % 60));
            } else {
                snprintf(val, valLen, "%lum", (unsigned long)mins);
            }
        }
    } else if (strcmp(tok, "rcap") == 0) {
        if (!data.valid || data.remain_cap_mb == 0) {
            snprintf(val, valLen, "---");
        } else if (data.remain_cap_mb >= 1000) {
            uint32_t gb_int  = data.remain_cap_mb / 1000;
            uint32_t gb_frac = (data.remain_cap_mb % 1000) / 100;
            snprintf(val, valLen, "%lu.%luG",
                     (unsigned long)gb_int, (unsigned long)gb_frac);
        } else {
            snprintf(val, valLen, "%luM", (unsigned long)data.remain_cap_mb);
        }
    }
}

static void expandTemplate(const char *tpl, const CameraData &data,
                           char *out, size_t outLen) {
    size_t outPos = 0;
    while (*tpl && outPos < outLen - 1) {
        if (*tpl == '{') {
            const char *end = strchr(tpl + 1, '}');
            if (!end) { out[outPos++] = *tpl++; continue; }
            char tok[16] = {};
            size_t tLen = static_cast<size_t>(end - (tpl + 1));
            if (tLen < sizeof(tok)) {
                memcpy(tok, tpl + 1, tLen);
                tok[tLen] = '\0';
                char val[17] = {};
                resolveToken(tok, data, val, sizeof(val));
                size_t vLen = strlen(val);
                size_t copy = (vLen < outLen - 1 - outPos) ? vLen : (outLen - 1 - outPos);
                memcpy(out + outPos, val, copy);
                outPos += copy;
            }
            tpl = end + 1;
        } else {
            out[outPos++] = *tpl++;
        }
    }
    out[outPos] = '\0';
}

// ─── OSD send functions ───────────────────────────────────────────────────────

void MSPSerial::sendCustomOSD1(const CameraData &data, const char *tpl) { sendCustomOSD(MSP_TEXT_CUSTOM_1, data, tpl); }
void MSPSerial::sendCustomOSD2(const CameraData &data, const char *tpl) { sendCustomOSD(MSP_TEXT_CUSTOM_2, data, tpl); }
void MSPSerial::sendCustomOSD3(const CameraData &data, const char *tpl) { sendCustomOSD(MSP_TEXT_CUSTOM_3, data, tpl); }
void MSPSerial::sendCustomOSD4(const CameraData &data, const char *tpl) { sendCustomOSD(MSP_TEXT_CUSTOM_4, data, tpl); }

void MSPSerial::sendPilotName(const CameraData &data, const char *tpl) { sendCustomOSD(MSP_TEXT_PILOT_NAME, data, tpl); }
void MSPSerial::sendCraftName(const CameraData &data, const char *tpl) { sendCustomOSD(MSP_TEXT_CRAFT_NAME, data, tpl); }

void MSPSerial::sendCustomOSD(uint8_t textType, const CameraData &data, const char *tpl) {
    char text[17] = {};
    expandTemplate(tpl, data, text, sizeof(text));
    sendCustomText(textType, text);
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
