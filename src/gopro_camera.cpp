#include "gopro_camera.h"
#include "config.h"
#include "dji_protocol.h"  // for DJI_MODE_* constants used in mode mapping
#include <cstring>

GoProCamera *GoProCamera::_instance = nullptr;

// ─── Public ──────────────────────────────────────────────────────────────────

void GoProCamera::begin() {
    _instance = this;
    BLEDevice::init("ESP32-GP-Bridge");
    BLEDevice::setPower(ESP_PWR_LVL_P9);
    BLEDevice::setMTU(500);
    startScan();
}

void GoProCamera::update() {
    // Deferred BLE writes from notify callbacks (calling writeValue() inside a
    // BLE notify callback crashes the stack).
    if (_pendingHwInfo && _bleConnected) {
        _pendingHwInfo = false;
        sendHardwareInfoQuery();
        return;
    }

    if (_pendingRegister && _bleConnected) {
        _pendingRegister = false;
        sendRegisterQuery();
        return;
    }

    if (_gpConnected || _bleConnected || _scanning) return;

    if (_targetFound) {
        connectAndSetup();
        return;
    }

    if (millis() - _lastAttemptMs > BLE_RECONNECT_DELAY_MS)
        startScan();
}

// ─── Scan ────────────────────────────────────────────────────────────────────

void GoProCamera::startScan() {
    _targetFound = false;
    _scanning    = true;

    BLEScan *scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(this, false);
    scan->setActiveScan(true);
    scan->setInterval(0x50);
    scan->setWindow(0x30);

    DBG_SERIAL.println("[BLE] Scanning for GoPro camera...");
    scan->start(BLE_SCAN_DURATION_SECS, scanDoneCallback, false);
}

void GoProCamera::scanDoneCallback(BLEScanResults /*r*/) {
    if (!_instance) return;
    _instance->_scanning = false;
    if (!_instance->_targetFound) {
        DBG_SERIAL.println("[BLE] Scan done — no GoPro found");
        _instance->_lastAttemptMs = millis();
    }
}

void GoProCamera::onResult(BLEAdvertisedDevice device) {
    bool isGoPro = false;

    // Primary: advertised service UUID 0xFEA6
    if (device.haveServiceUUID() &&
        device.isAdvertisingService(BLEUUID((uint16_t)GP_SERVICE_UUID))) {
        isGoPro = true;
    }

    // Fallback: manufacturer company ID 0xF202
    if (!isGoPro && device.haveManufacturerData()) {
        const std::string mfr = device.getManufacturerData();
        if (mfr.size() >= 2 &&
            (uint8_t)mfr[0] == GP_MANUFACTURER_ID_LO &&
            (uint8_t)mfr[1] == GP_MANUFACTURER_ID_HI) {
            isGoPro = true;
        }
    }

    // Fallback: device name
    if (!isGoPro && device.haveName() &&
        device.getName().find(GP_DEVICE_NAME_PREFIX) != std::string::npos) {
        isGoPro = true;
    }

    if (!isGoPro) return;

    DBG_SERIAL.printf("[BLE] Found GoPro: \"%s\"  addr=%s  rssi=%d\n",
                      device.haveName() ? device.getName().c_str() : "?",
                      device.getAddress().toString().c_str(),
                      device.getRSSI());

    BLEDevice::getScan()->stop();
    _targetAddr  = device.getAddress().toString();
    _targetType  = device.getAddressType();
    _targetFound = true;
    _scanning    = false;
}

// ─── Connect & characteristic discovery ──────────────────────────────────────

bool GoProCamera::connectAndSetup() {
    DBG_SERIAL.printf("[BLE] Connecting to GoPro %s ...\n", _targetAddr.c_str());

    if (!_client) {
        _client = BLEDevice::createClient();
        _client->setClientCallbacks(this);
    }

    if (!_client->connect(BLEAddress(_targetAddr), _targetType)) {
        DBG_SERIAL.println("[BLE] Connection failed");
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }

    if (_client->setMTU(500))
        DBG_SERIAL.printf("[BLE] MTU exchange OK (got %u)\n", _client->getMTU());
    else
        DBG_SERIAL.println("[BLE] MTU exchange failed — staying at 23");

    BLERemoteService *svc = _client->getService(BLEUUID((uint16_t)GP_SERVICE_UUID));
    if (!svc) {
        DBG_SERIAL.println("[BLE] GoPro service 0xFEA6 not found");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }

    // GP-0073: command response notify
    BLERemoteCharacteristic *cmdNotify =
        svc->getCharacteristic(BLEUUID(GP_CHAR_CMD_NOTIFY));
    if (!cmdNotify || !cmdNotify->canNotify()) {
        DBG_SERIAL.println("[BLE] Command notify char GP-0073 not found");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }
    cmdNotify->registerForNotify(cmdNotifyCallback);
    DBG_SERIAL.println("[BLE] Subscribed to GP-0073 (command notify)");

    // GP-0077: query/status notify
    BLERemoteCharacteristic *queryNotify =
        svc->getCharacteristic(BLEUUID(GP_CHAR_QUERY_NOTIFY));
    if (!queryNotify || !queryNotify->canNotify()) {
        DBG_SERIAL.println("[BLE] Query notify char GP-0077 not found");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }
    queryNotify->registerForNotify(queryNotifyCallback);
    DBG_SERIAL.println("[BLE] Subscribed to GP-0077 (query notify)");

    // GP-0072: command write
    _cmdChar = svc->getCharacteristic(BLEUUID(GP_CHAR_CMD_WRITE));
    if (!_cmdChar || !_cmdChar->canWrite()) {
        DBG_SERIAL.println("[BLE] Command write char GP-0072 not found");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }

    // GP-0076: query write
    _queryChar = svc->getCharacteristic(BLEUUID(GP_CHAR_QUERY_WRITE));
    if (!_queryChar || !_queryChar->canWrite()) {
        DBG_SERIAL.println("[BLE] Query write char GP-0076 not found");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }

    _bleConnected = true;
    DBG_SERIAL.println("[BLE] GoPro BLE connected — querying hardware info");

    // Kick off handshake; deferred so we're not writing from the connect callback
    _pendingHwInfo = true;
    return true;
}

void GoProCamera::onConnect(BLEClient * /*c*/) {
    DBG_SERIAL.println("[BLE] onConnect");
}

void GoProCamera::onDisconnect(BLEClient * /*c*/) {
    DBG_SERIAL.println("[BLE] GoPro disconnected — will rescan");
    _gpConnected   = false;
    _bleConnected  = false;
    _cmdChar       = nullptr;
    _queryChar     = nullptr;
    _targetFound   = false;
    _lastAttemptMs = millis();
    _cmdRx.reset();
    _queryRx.reset();
}

// ─── GoPro commands ───────────────────────────────────────────────────────────

// Sends a TLV-encoded command to GP-0072.
// Packet: [header(1), cmd_id(1), param_len(1), param_val...]
// header byte = total payload length (cmd_id + tlv), general 5-bit encoding.
void GoProCamera::sendCmd(uint8_t cmd_id, const uint8_t *params, uint8_t param_len) {
    if (!_cmdChar) return;
    uint8_t buf[32];
    uint8_t msg_len = 1 + param_len;   // cmd_id(1) + params
    buf[0] = msg_len & 0x1F;           // general 5-bit header
    buf[1] = cmd_id;
    if (params && param_len > 0)
        memcpy(buf + 2, params, param_len);
    _cmdChar->writeValue(buf, 1 + msg_len, false);
}

void GoProCamera::sendHardwareInfoQuery() {
    if (!_cmdChar) return;
    // Packet: [0x01, 0x3C]  (header=1, cmd=GetHardwareInfo, no params)
    const uint8_t buf[] = {0x01, GP_CMD_GET_HARDWARE_INFO};
    _cmdChar->writeValue(const_cast<uint8_t *>(buf), sizeof(buf), false);
    DBG_SERIAL.println("[GP] Get hardware info sent");
}

void GoProCamera::sendRegisterQuery() {
    if (!_queryChar) return;
    // Register for change notifications on all tracked status IDs.
    const uint8_t ids[] = {
        GP_STATUS_OVERHEATING,
        GP_STATUS_ENCODING,
        GP_STATUS_ENC_DURATION,
        GP_STATUS_REMAINING_TIME,
        GP_STATUS_SD_REMAINING,
        GP_STATUS_BATTERY_PCT,
        GP_STATUS_PRESET_GROUP,
    };
    // Payload: [query_id, id_0, id_1, ...]
    uint8_t payload_len = 1 + (uint8_t)sizeof(ids);
    uint8_t buf[32];
    buf[0] = payload_len & 0x1F;         // header
    buf[1] = GP_QUERY_REGISTER_STATUS;   // 0x52
    memcpy(buf + 2, ids, sizeof(ids));
    _queryChar->writeValue(buf, 1 + payload_len, false);
    DBG_SERIAL.println("[GP] Register for status updates sent");
}

// ─── Recording & mode commands ────────────────────────────────────────────────

bool GoProCamera::startRecording() {
    // Shutter on: cmd=0x01, TLV=[len=1, val=1]
    const uint8_t p[] = {0x01, 0x01};
    sendCmd(GP_CMD_SET_SHUTTER, p, sizeof(p));
    DBG_SERIAL.println("[GP] Shutter on sent");
    return true;
}

bool GoProCamera::stopRecording() {
    // Shutter off: cmd=0x01, TLV=[len=1, val=0]
    const uint8_t p[] = {0x01, 0x00};
    sendCmd(GP_CMD_SET_SHUTTER, p, sizeof(p));
    DBG_SERIAL.println("[GP] Shutter off sent");
    return true;
}

bool GoProCamera::switchCameraMode(uint8_t dji_mode) {
    // Map DJI_MODE_* → GoPro preset group
    uint8_t group;
    switch (dji_mode) {
        case DJI_MODE_PHOTO:       group = GP_PRESET_PHOTO;     break;
        case DJI_MODE_TIMELAPSE:   group = GP_PRESET_TIMELAPSE; break;
        case DJI_MODE_HYPERLAPSE:  group = GP_PRESET_TIMELAPSE; break;
        default:                   group = GP_PRESET_VIDEO;     break;  // VIDEO + others
    }
    const uint8_t p[] = {0x01, group};
    sendCmd(GP_CMD_LOAD_PRESET_GROUP, p, sizeof(p));
    DBG_SERIAL.printf("[GP] Load preset group %u sent\n", group);
    return true;
}

// ─── Notify callbacks (static trampolines) ────────────────────────────────────

void GoProCamera::cmdNotifyCallback(BLERemoteCharacteristic * /*ch*/,
                                     uint8_t *data, size_t len, bool /*isNotify*/) {
    if (_instance) _instance->handleCmdNotification(data, len);
}

void GoProCamera::queryNotifyCallback(BLERemoteCharacteristic * /*ch*/,
                                       uint8_t *data, size_t len, bool /*isNotify*/) {
    if (_instance) _instance->handleQueryNotification(data, len);
}

// ─── Notification handling ────────────────────────────────────────────────────

void GoProCamera::handleCmdNotification(uint8_t *data, size_t len) {
    if (_cmdRx.feed(data, len))
        handleCmdMessage(_cmdRx.buf, _cmdRx.expected);
}

void GoProCamera::handleQueryNotification(uint8_t *data, size_t len) {
    if (_queryRx.feed(data, len))
        handleQueryMessage(_queryRx.buf, _queryRx.expected);
}

// Assembled command response: [cmd_id, status, optional_data...]
void GoProCamera::handleCmdMessage(const uint8_t *msg, size_t len) {
    if (len < 2) return;
    uint8_t cmd_id = msg[0];
    uint8_t status = msg[1];

    if (cmd_id == GP_CMD_GET_HARDWARE_INFO) {
        if (status == 0) {
            DBG_SERIAL.println("[GP] Hardware info OK — registering for status updates");
            _pendingRegister = true;   // deferred write to avoid callback crash
        } else {
            DBG_SERIAL.printf("[GP] Hardware info failed (status=0x%02X) — retrying\n", status);
            _pendingHwInfo = true;
        }
    } else if (cmd_id == GP_CMD_SET_SHUTTER) {
        if (status == 0)
            DBG_SERIAL.println("[GP] Shutter OK");
        else
            DBG_SERIAL.printf("[GP] Shutter rejected: 0x%02X\n", status);
    } else if (cmd_id == GP_CMD_LOAD_PRESET_GROUP) {
        if (status == 0)
            DBG_SERIAL.println("[GP] Preset group OK");
        else
            DBG_SERIAL.printf("[GP] Preset group rejected: 0x%02X\n", status);
    }
}

// Assembled query response / status notification:
// [query_id, status, id, len, val..., id, len, val..., ...]
void GoProCamera::handleQueryMessage(const uint8_t *msg, size_t len) {
    if (len < 2) return;
    uint8_t query_id = msg[0];
    uint8_t status   = msg[1];

    if (status != 0) {
        DBG_SERIAL.printf("[GP] Query 0x%02X failed: status=0x%02X\n", query_id, status);
        return;
    }

    // On successful register response, mark as fully connected
    if (query_id == GP_QUERY_REGISTER_STATUS && !_gpConnected) {
        _gpConnected = true;
        DBG_SERIAL.println("[GP] Status registration OK — camera ready");
    }

    // Parse TLV triplets and update camera data
    if (len > 2) {
        parseStatusTlv(msg + 2, len - 2);
        if (_camera.valid && _gpConnected && _cameraCb)
            _cameraCb(_camera);
    }
}

// Parse TLV status triplets: [id(1), len(1), value(len)...]
void GoProCamera::parseStatusTlv(const uint8_t *tlv, size_t len) {
    size_t pos = 0;
    bool   updated = false;

    while (pos + 2 <= len) {
        uint8_t id   = tlv[pos];
        uint8_t vlen = tlv[pos + 1];
        pos += 2;
        if (pos + vlen > len) break;
        const uint8_t *v = tlv + pos;
        pos += vlen;

        switch (id) {
            case GP_STATUS_BATTERY_PCT:
                if (vlen >= 1) { _camera.percent = v[0]; updated = true; }
                break;

            case GP_STATUS_ENCODING:
                if (vlen >= 1) { _camera.recording = (v[0] != 0); updated = true; }
                break;

            case GP_STATUS_ENC_DURATION:
                if (vlen >= 4) {
                    uint32_t secs = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                                    ((uint32_t)v[2] <<  8) |  (uint32_t)v[3];
                    _camera.record_time = (uint16_t)(secs & 0xFFFF);
                    updated = true;
                }
                break;

            case GP_STATUS_REMAINING_TIME:
                if (vlen >= 4) {
                    _camera.remain_time = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                                          ((uint32_t)v[2] <<  8) |  (uint32_t)v[3];
                    updated = true;
                }
                break;

            case GP_STATUS_SD_REMAINING:
                if (vlen >= 4) {
                    _camera.remain_cap_mb = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                                            ((uint32_t)v[2] <<  8) |  (uint32_t)v[3];
                    updated = true;
                }
                break;

            case GP_STATUS_OVERHEATING:
                if (vlen >= 1) { _camera.temp_over = v[0] ? 1 : 0; updated = true; }
                break;

            case GP_STATUS_PRESET_GROUP:
                if (vlen >= 1) {
                    // Map GoPro preset group → DJI-style camera_mode for telemetry
                    switch (v[0]) {
                        case 1:  _camera.camera_mode = DJI_MODE_PHOTO;     break;
                        case 2:  _camera.camera_mode = DJI_MODE_TIMELAPSE; break;
                        default: _camera.camera_mode = DJI_MODE_VIDEO;     break;
                    }
                    updated = true;
                }
                break;
        }
    }

    if (updated)
        _camera.valid = true;
}
