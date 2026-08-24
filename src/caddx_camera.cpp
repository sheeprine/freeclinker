#include "caddx_camera.h"
#include "caddx_protocol.h"
#include "config.h"
#include "dji_protocol.h"  // for DJI_MODE_* constants used in mode mapping
#include <HTTPClient.h>

// Poll cadence: one request per update() call, spaced this far apart, cycling
// through allinfo/battery/sdstate. This keeps each update() call bounded
// (short HTTP timeout) while still refreshing telemetry a few times/second
// on aggregate — the same 2 Hz-ish ballpark the BLE cameras push at.
static constexpr uint32_t CADDX_POLL_INTERVAL_MS = 700;
static constexpr uint32_t CADDX_HTTP_TIMEOUT_MS  = 1200;
static constexpr uint32_t CADDX_WIFI_RETRY_MS    = 5000;

// ─── Public ──────────────────────────────────────────────────────────────────

void CaddxCamera::setWifiCredentials(const char *ssid, const char *password) {
    _ssid     = ssid ? ssid : "";
    _password = password ? password : "";
}

void CaddxCamera::begin() {
    // Concurrent AP+STA: web_server.cpp runs the config AP via WiFi.softAP()
    // independently of this. Explicitly requesting APSTA here means joining
    // the camera's network never disables that AP if it happens to already
    // be running (e.g. forced on via the BOOT button).
    WiFi.mode(WIFI_MODE_APSTA);
    if (_ssid.length() == 0) {
        DBG_SERIAL.println("[caddx] No Wi-Fi SSID configured — set with 'set caddx_ssid'");
        return;
    }
    DBG_SERIAL.printf("[caddx] Joining Wi-Fi \"%s\"...\n", _ssid.c_str());
    WiFi.begin(_ssid.c_str(), _password.length() ? _password.c_str() : nullptr);
}

void CaddxCamera::update() {
    if (_ssid.length() == 0) return;

    const bool wifiUp = (WiFi.status() == WL_CONNECTED);

    if (wifiUp && !_wifiWasConnected) {
        _ip = WiFi.gatewayIP();  // camera is the AP's DHCP gateway
        DBG_SERIAL.printf("[caddx] Wi-Fi connected — camera gateway %s\n",
                          _ip.toString().c_str());
        _attrProbed = false;
    }

    if (!wifiUp && _wifiWasConnected) {
        DBG_SERIAL.println("[caddx] Wi-Fi link lost — will reconnect");
        _connected      = false;
        _attrProbed     = false;
        _recordVariant  = RecordVariant::kUnknown;
        _camera         = CameraData{};
    }

    _wifiWasConnected = wifiUp;

    if (!wifiUp) {
        // ESP32 Arduino core auto-retries association by default; nothing
        // else to do here besides waiting. WiFi.begin() only needs to be
        // called again if the SSID itself changes at runtime, which is out
        // of scope (requires a reboot today, same as camera_type).
        return;
    }

    if (!_attrProbed) {
        pollDeviceAttr();
        return;
    }

    const uint32_t now = millis();
    if (now - _lastPollMs < CADDX_POLL_INTERVAL_MS) return;
    _lastPollMs = now;

    switch (_pollPhase) {
        case 0: pollAllInfo();  break;
        case 1: pollBattery();  break;
        default: pollSdState(); break;
    }
    _pollPhase = (_pollPhase + 1) % 3;
}

// ─── HTTP ─────────────────────────────────────────────────────────────────────

int CaddxCamera::httpGet(const String &path, String &body) {
    HTTPClient http;
    http.setConnectTimeout(CADDX_HTTP_TIMEOUT_MS);
    http.setTimeout(CADDX_HTTP_TIMEOUT_MS);
    if (!http.begin(baseUrl() + path)) return -1;
    int code = http.GET();
    if (code == 200) body = http.getString();
    http.end();
    return code;
}

// ─── Device identity / variant detection ─────────────────────────────────────

void CaddxCamera::pollDeviceAttr() {
    String body;
    int code = httpGet(CADDX_CGI_GET_DEVICE_ATTR, body);
    if (code != 200) {
        // Camera may still be booting its HTTP server right after Wi-Fi
        // association; retry on the next update() rather than backing off
        // hard, same spirit as the BLE drivers' reconnect loop.
        return;
    }

    String hardversion, type, name;
    caddx_parse_var(body, "hardversion", hardversion);
    caddx_parse_var(body, "type", type);
    caddx_parse_var(body, "name", name);

    _isNewApp = (hardversion == CADDX_HARDVERSION_NEWAPP);

    // Record-endpoint branch, verified from DV.executeCommand() (NewAPP
    // devices always collapse to record.cgi) and MainActivity's
    // WORK_MODE_VIDEO_NORMAL case (legacy devices branch on sensor type).
    // Confirmed against a real Orca: getdeviceattr.cgi reports
    // hardversion="NewAPP" (type="Hi3559V200", not the 34220 sensor id),
    // so this unit takes the record.cgi path — see findings.md.
    if (_isNewApp || type == CADDX_SENSOR_TYPE_34220) {
        _recordVariant = RecordVariant::kRecordCgi;
    } else {
        _recordVariant = RecordVariant::kRecord2Cgi;
    }

    DBG_SERIAL.printf("[caddx] Connected: name=\"%s\" hardversion=\"%s\" type=\"%s\" "
                      "→ %s\n",
                      name.c_str(), hardversion.c_str(), type.c_str(),
                      _recordVariant == RecordVariant::kRecordCgi ? "record.cgi"
                                                                   : "record2.cgi");

    _attrProbed = true;
    _connected  = true;
}

// ─── Telemetry polling ────────────────────────────────────────────────────────

void CaddxCamera::pollAllInfo() {
    String body;
    const char *path = _isNewApp ? CADDX_CGI_GET_CUR_ALL_INFO : CADDX_CGI_GET_ALL_INFO;
    int code = httpGet(path, body);
    if (code != 200) return;

    if (_isNewApp) {
        pollAllInfoSs(body);
    } else {
        pollAllInfoLegacy(body);
    }

    _camera.valid = true;
    if (_cameraCb) _cameraCb(_camera);
}

// SigmaStar ("SS") family — hardware-confirmed on a real NewAPP Orca.
// "state" is 20 (working/recording) or 21 (standby); "mode" is a free-form
// name string, not a number (see caddx_protocol.h). No thermal/event
// telemetry was found anywhere in this family's app code, so temp_over is
// deliberately left untouched here rather than guessed.
void CaddxCamera::pollAllInfoSs(const String &body) {
    long state = -1, pasttime = -1;
    String mode;
    bool haveState = caddx_parse_var_int(body, "state", state);
    bool haveMode  = caddx_parse_var(body, "mode", mode);
    caddx_parse_var_int(body, "pasttime", pasttime);

    if (haveState) _camera.recording = (state == CADDX_SS_STATE_WORKING);

    // The "mode" field has been observed to occasionally come back as
    // non-ASCII garbage on real hardware (uninitialized firmware buffer?).
    // An unrecognized string is treated as "no update" rather than forced
    // into a default DJI_MODE_* bucket.
    uint8_t djiMode;
    if (haveMode && mapSsWorkModeToDji(mode, djiMode)) {
        _camera.camera_mode = djiMode;
    }

    if (pasttime >= 0) _camera.record_time = (uint16_t)(pasttime & 0xFFFF);
}

// Legacy (non-NewAPP) Hi3510 devices — transcribed from decompiled code but
// never exercised against real hardware.
void CaddxCamera::pollAllInfoLegacy(const String &body) {
    long mode = -1, state = -1, event = -1, pasttime = -1;
    bool haveState = caddx_parse_var_int(body, "state", state);
    caddx_parse_var_int(body, "mode", mode);
    caddx_parse_var_int(body, "event", event);
    caddx_parse_var_int(body, "pasttime", pasttime);

    if (haveState) {
        _camera.recording = (state == CADDX_WORK_STATE_RECORD ||
                             state == CADDX_WORK_STATE_VIDEO_LOOP ||
                             state == CADDX_WORK_STATE_VIDEO_TIMELAPSE ||
                             state == CADDX_WORK_STATE_VIDEO_BURST);
    }
    if (mode >= 0) _camera.camera_mode = mapWorkModeToDji(mode);
    if (pasttime >= 0) _camera.record_time = (uint16_t)(pasttime & 0xFFFF);

    // Common.EVENT_* thermal events → CameraData.temp_over (0=ok,1=warn,3=shutdown).
    // The temp_over 4-level scale is DJI's own convention (see
    // dji_protocol.h) reused here as the closest fit.
    if (event == CADDX_EVENT_CHIP_TEMPERATURE_HIGH ||
        event == CADDX_EVENT_BATTERY_TEMPERATURE_HIGH) {
        _camera.temp_over = 1;
    } else if (event == CADDX_EVENT_CHIP_TEMPERATURE_ALARM ||
               event == CADDX_EVENT_BATTERY_TEMPERATURE_ALARM) {
        _camera.temp_over = 3;
    } else if (event == CADDX_EVENT_NORMAL) {
        _camera.temp_over = 0;
    }
}

void CaddxCamera::pollBattery() {
    String body;
    int code = httpGet(CADDX_CGI_GET_BATTERY, body);
    if (code != 200) return;

    long capacity = -1;
    if (caddx_parse_var_int(body, "capacity", capacity) &&
        capacity >= 0 && capacity <= 100) {
        _camera.percent = (uint8_t)capacity;
        _camera.valid   = true;
        if (_cameraCb) _cameraCb(_camera);
    }
}

void CaddxCamera::pollSdState() {
    String body;
    int code = httpGet(CADDX_CGI_GET_SD_STATE, body);
    if (code != 200) return;

    String total, used;
    bool haveTotal = caddx_parse_var(body, "total", total);
    bool haveUsed  = caddx_parse_var(body, "used", used);
    if (haveTotal && haveUsed) {
        total.replace(" MB", "");
        used.replace(" MB", "");
        long totalMb = total.toInt();
        long usedMb  = used.toInt();
        if (totalMb > 0 && usedMb >= 0 && usedMb <= totalMb) {
            _camera.remain_cap_mb = (uint32_t)(totalMb - usedMb);
            _camera.valid         = true;
            if (_cameraCb) _cameraCb(_camera);
        }
    }
}

bool CaddxCamera::mapSsWorkModeToDji(const String &name, uint8_t &out) const {
    if (name == CADDX_MODE_NEW_NORMAL_VIDEO)    { out = DJI_MODE_VIDEO;        return true; }
    if (name == CADDX_MODE_NEW_NORMAL_PHOTO)    { out = DJI_MODE_PHOTO;        return true; }
    if (name == CADDX_MODE_NEW_SLOW_MOTION)     { out = DJI_MODE_SLOW_MOTION;  return true; }
    if (name == CADDX_MODE_NEW_TIMELAPSE_VIDEO) { out = DJI_MODE_TIMELAPSE;    return true; }
    return false;
}

uint8_t CaddxCamera::mapWorkModeToDji(long workMode) const {
    switch (workMode) {
        case CADDX_WORK_MODE_VIDEO_SLOW:       return DJI_MODE_SLOW_MOTION;
        case CADDX_WORK_MODE_VIDEO_TIMELAPSE:
        case CADDX_WORK_MODE_MULTI_TIMELAPSE:
        case CADDX_WORK_MODE_VIDEO_LAPSE_BURST: return DJI_MODE_TIMELAPSE;
        case CADDX_WORK_MODE_PHOTO_SINGLE:
        case CADDX_WORK_MODE_PHOTO_TIMER:
        case CADDX_WORK_MODE_PHOTO_RAW:
        case CADDX_WORK_MODE_MULTI_BURST:
        case CADDX_WORK_MODE_MULTI_CONTINUOUS:  return DJI_MODE_PHOTO;
        default:                                return DJI_MODE_VIDEO;
    }
}

// ─── Recording control ────────────────────────────────────────────────────────

bool CaddxCamera::startRecording() {
    if (!_connected) return false;
    String body;
    const char *path = (_recordVariant == RecordVariant::kRecordCgi)
                            ? CADDX_CGI_RECORD_START
                            : CADDX_CGI_RECORD2_START;
    int code = httpGet(path, body);
    DBG_SERIAL.printf("[caddx] Record start (%s) → HTTP %d\n", path, code);
    return code == 200 && body.indexOf("SvrFuncResult") < 0;
}

bool CaddxCamera::stopRecording() {
    if (!_connected) return false;
    String body;
    const char *path = (_recordVariant == RecordVariant::kRecordCgi)
                            ? CADDX_CGI_RECORD_STOP
                            : CADDX_CGI_RECORD2_STOP;
    int code = httpGet(path, body);
    DBG_SERIAL.printf("[caddx] Record stop (%s) → HTTP %d\n", path, code);
    return code == 200 && body.indexOf("SvrFuncResult") < 0;
}

// TODO(unverified): the legacy/NewAPP work-mode name split below is
// transcribed from Common.java/CameraParmeras.java (legacy) and
// SSExchangeWorkMode.java (NewAPP) in the decompiled app, but — unlike
// startRecording()/stopRecording() — this whole call has never been
// exercised against a real Orca. It is also not capability-checked: the
// real app enumerates supported modes via getallworkmode.cgi first and only
// offers modes the connected camera actually supports; this driver just
// fires the request and reports success purely on HTTP 200.
bool CaddxCamera::switchCameraMode(uint8_t mode) {
    if (!_connected) return false;

    const char *name;
    if (_isNewApp) {
        switch (mode) {
            case DJI_MODE_SLOW_MOTION: name = CADDX_MODE_NEW_SLOW_MOTION;     break;
            case DJI_MODE_PHOTO:       name = CADDX_MODE_NEW_NORMAL_PHOTO;    break;
            case DJI_MODE_TIMELAPSE:
            case DJI_MODE_HYPERLAPSE:  name = CADDX_MODE_NEW_TIMELAPSE_VIDEO; break;
            default:                   name = CADDX_MODE_NEW_NORMAL_VIDEO;   break;
        }
    } else {
        switch (mode) {
            case DJI_MODE_SLOW_MOTION: name = CADDX_MODE_LEGACY_SLOW_REC;     break;
            case DJI_MODE_PHOTO:       name = CADDX_MODE_LEGACY_NORMAL_PHOTO; break;
            case DJI_MODE_TIMELAPSE:
            case DJI_MODE_HYPERLAPSE:  name = CADDX_MODE_LEGACY_VIDEO_LAPSE;  break;
            default:                   name = CADDX_MODE_LEGACY_NORMAL_VIDEO; break;
        }
    }

    String path = String(CADDX_CGI_SET_WORKMODE) + name;
    path.replace(" ", "%20");
    String body;
    int code = httpGet(path, body);
    DBG_SERIAL.printf("[caddx] Set work mode \"%s\" → HTTP %d\n", name, code);
    return code == 200;
}
