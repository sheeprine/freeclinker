#include "gopro_camera.h"
#include "camera_registry.h"
#include "config.h"
#include "ble_debug.h"
#include "dji_protocol.h"  // for DJI_MODE_* constants used in mode mapping
#include <BLESecurity.h>
#include <cstring>

GoProCamera *GoProCamera::_instance = nullptr;

// ─── Public ──────────────────────────────────────────────────────────────────

void GoProCamera::begin() {
    _instance = this;
    BLEDevice::init("ESP32-GP-Bridge");
    BLEDevice::setPower(ESP_PWR_LVL_P9);
    BLEDevice::setMTU(500);

    // MAX2 rejects Open GoPro commands on an unsecured link; request Secure
    // Connections + bonding with no-input/no-output pairing and let the BLE
    // stack negotiate encryption as part of the GATT connection.
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    static BLESecurity security;
    security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    security.setCapability(ESP_IO_CAP_NONE);
    security.setKeySize(16);
    security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

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

    if (_pendingRegisterSettings && _bleConnected) {
        _pendingRegisterSettings = false;
        sendRegisterSettings();
        return;
    }

    if (_pendingRegisterStatus && _bleConnected) {
        _pendingRegisterStatus = false;
        sendRegisterStatus();
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
    _targetFound   = false;
    _scanning      = true;
    _candidateAddr = "";
    _candidateName = "";

    BLEScan *scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(this, false);
    scan->setActiveScan(true);
    scan->setInterval(0x50);
    scan->setWindow(0x30);

    std::string preferred = _registry ? _registry->preferredAddr() : "";
    if (preferred.empty())
        DBG_SERIAL.println("[BLE] Scanning for GoPro camera...");
    else
        DBG_SERIAL.printf("[BLE] Scanning for GoPro %s...\n", preferred.c_str());

    scan->start(BLE_SCAN_DURATION_SECS, scanDoneCallback, false);
}

void GoProCamera::scanDoneCallback(BLEScanResults /*r*/) {
    if (!_instance) return;
    _instance->_scanning = false;
    if (!_instance->_targetFound) {
        if (!_instance->_candidateAddr.empty() && !_instance->_strictCamera) {
            // Preferred camera wasn't seen — fall back to first GoPro found
            DBG_SERIAL.printf("[BLE] Preferred not found — connecting to %s\n",
                              _instance->_candidateAddr.c_str());
            _instance->_targetAddr  = _instance->_candidateAddr;
            _instance->_targetName  = _instance->_candidateName;
            _instance->_targetType  = _instance->_candidateType;
            _instance->_targetFound = true;
        } else {
            if (_instance->_strictCamera && !_instance->_candidateAddr.empty())
                DBG_SERIAL.println("[BLE] Preferred not found — strict mode, skipping other cameras");
            else
                DBG_SERIAL.println("[BLE] Scan done — no GoPro found");
            _instance->_lastAttemptMs = millis();
        }
    }
}

// onResult is called for every advertised device during the scan.
// If a registry preferred address is set, stop scan early when it is found.
// Otherwise record the first GoPro found as fallback and keep scanning.
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
            (uint8_t)mfr[0] == GP_MANUFACTURER_ID_HI &&
            (uint8_t)mfr[1] == GP_MANUFACTURER_ID_LO) {
            isGoPro = true;
        }
    }

    // Fallback: device name
    if (!isGoPro && device.haveName() &&
        device.getName().find(GP_DEVICE_NAME_PREFIX) != std::string::npos) {
        isGoPro = true;
    }

    if (!isGoPro) return;

    std::string addr = device.getAddress().toString();
    std::string name = device.haveName() ? device.getName() : "GoPro";

    DBG_SERIAL.printf("[BLE] Found GoPro: \"%s\"  addr=%s  rssi=%d\n",
                      name.c_str(), addr.c_str(), device.getRSSI());

    // Keep first found as fallback
    if (_candidateAddr.empty()) {
        _candidateAddr = addr;
        _candidateName = name;
        _candidateType = device.getAddressType();
    }

    std::string preferred = _registry ? _registry->preferredAddr() : "";

    if (!preferred.empty() && addr == preferred) {
        // Preferred camera found — stop scan immediately
        BLEDevice::getScan()->stop();
        _targetAddr  = addr;
        _targetName  = name;
        _targetType  = device.getAddressType();
        _targetFound = true;
        _scanning    = false;
    } else if (preferred.empty() && !_targetFound) {
        // No preference — connect to first found (original behavior)
        BLEDevice::getScan()->stop();
        _targetAddr  = addr;
        _targetName  = name;
        _targetType  = device.getAddressType();
        _targetFound = true;
        _scanning    = false;
    }
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

    // GP-0074: setting write
    _settingChar = svc->getCharacteristic(BLEUUID(GP_CHAR_SETTING_WRITE));
    if (!_settingChar || !_settingChar->canWrite()) {
        DBG_SERIAL.println("[BLE] Setting write char GP-0074 not found");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }

    // GP-0075: setting response notify
    BLERemoteCharacteristic *settingNotify =
        svc->getCharacteristic(BLEUUID(GP_CHAR_SETTING_NOTIFY));
    if (!settingNotify || !settingNotify->canNotify()) {
        DBG_SERIAL.println("[BLE] Setting notify char GP-0075 not found");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }
    settingNotify->registerForNotify(settingNotifyCallback);
    DBG_SERIAL.println("[BLE] Subscribed to GP-0075 (setting notify)");

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

    // Record this camera in the registry so it is preferred on next boot
    if (_registry)
        _registry->onConnected(_targetName.c_str(), _targetAddr.c_str(),
                               (uint8_t)_targetType, /*GoPro=*/1);

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
    _settingChar   = nullptr;
    _queryChar     = nullptr;
    _targetFound   = false;
    _targetAddr    = "";
    _targetName    = "";
    _candidateAddr = "";
    _candidateName = "";
    _lastAttemptMs = millis();
    _cmdRx.reset();
    _settingRx.reset();
    _queryRx.reset();
}

// ─── GoPro commands ───────────────────────────────────────────────────────────

// Writes a setting to GP-0074.
// Packet: [header(1), setting_id(1), value_length=1(1), value(1)]
void GoProCamera::sendSetting(uint8_t setting_id, uint8_t value) {
    if (!_settingChar) return;
    uint8_t buf[] = {0x03, setting_id, 0x01, value};
    if (_debugBle) bleDebugDump(DBG_SERIAL, "TX", "GP-0074", buf, sizeof(buf));
    _settingChar->writeValue(buf, sizeof(buf), false);
}

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
    if (_debugBle) bleDebugDump(DBG_SERIAL, "TX", "GP-0072", buf, 1 + msg_len);
    _cmdChar->writeValue(buf, 1 + msg_len, false);
}

void GoProCamera::sendHardwareInfoQuery() {
    if (!_cmdChar) return;
    // Packet: [0x01, 0x3C]  (header=1, cmd=GetHardwareInfo, no params)
    const uint8_t buf[] = {0x01, GP_CMD_GET_HARDWARE_INFO};
    if (_debugBle) bleDebugDump(DBG_SERIAL, "TX", "GP-0072", buf, sizeof(buf));
    _cmdChar->writeValue(const_cast<uint8_t *>(buf), sizeof(buf), false);
    DBG_SERIAL.println("[GP] Get hardware info sent");
}

void GoProCamera::sendRegisterSettings() {
    if (!_queryChar) return;
    // Register for change notifications on setting IDs via 0x52.
    const uint8_t ids[] = {
        GP_SETTING_RESOLUTION,
        GP_SETTING_FPS,
        GP_SETTING_HYPERSMOOTH,
    };
    uint8_t payload_len = 1 + (uint8_t)sizeof(ids);
    uint8_t buf[32];
    buf[0] = payload_len & 0x1F;
    buf[1] = GP_QUERY_REGISTER_SETTING;
    memcpy(buf + 2, ids, sizeof(ids));
    if (_debugBle) bleDebugDump(DBG_SERIAL, "TX", "GP-0076", buf, 1 + payload_len);
    _queryChar->writeValue(buf, 1 + payload_len, false);
    DBG_SERIAL.println("[GP] Register for setting updates sent");
}

void GoProCamera::sendRegisterStatus() {
    if (!_queryChar) return;
    // Register for change notifications on status IDs via 0x53.
    const uint8_t ids[] = {
        GP_STATUS_OVERHEATING,
        GP_STATUS_ENCODING,
        GP_STATUS_ENC_DURATION,
        GP_STATUS_REMAINING_TIME,
        GP_STATUS_SD_REMAINING,
        GP_STATUS_BATTERY_PCT,
        GP_STATUS_PRESET_GROUP,
    };
    uint8_t payload_len = 1 + (uint8_t)sizeof(ids);
    uint8_t buf[32];
    buf[0] = payload_len & 0x1F;
    buf[1] = GP_QUERY_REGISTER_STATUS;
    memcpy(buf + 2, ids, sizeof(ids));
    if (_debugBle) bleDebugDump(DBG_SERIAL, "TX", "GP-0076", buf, 1 + payload_len);
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

bool GoProCamera::triggerBurstSloMo() {
    if (!_gpConnected) return false;
    // Setting 239 (General Sub Mode) = 32 (Burst Slo-Mo). Requires Mission 1 Pro.
    sendSetting(GP_SETTING_GENERAL_SUB_MODE, GP_SUB_MODE_BURST_SLOMO);
    DBG_SERIAL.println("[GP] Sub-mode → Burst Slo-Mo sent");
    return true;
}

bool GoProCamera::exitBurstSloMo() {
    if (!_gpConnected) return false;
    sendSetting(GP_SETTING_GENERAL_SUB_MODE, GP_SUB_MODE_STANDARD);
    DBG_SERIAL.println("[GP] Sub-mode → Standard sent");
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

void GoProCamera::settingNotifyCallback(BLERemoteCharacteristic * /*ch*/,
                                         uint8_t *data, size_t len, bool /*isNotify*/) {
    if (_instance) _instance->handleSettingNotification(data, len);
}

void GoProCamera::queryNotifyCallback(BLERemoteCharacteristic * /*ch*/,
                                       uint8_t *data, size_t len, bool /*isNotify*/) {
    if (_instance) _instance->handleQueryNotification(data, len);
}

// ─── Notification handling ────────────────────────────────────────────────────

void GoProCamera::handleCmdNotification(uint8_t *data, size_t len) {
    if (_debugBle) bleDebugDump(DBG_SERIAL, "RX", "GP-0073", data, len);
    if (_cmdRx.feed(data, len))
        handleCmdMessage(_cmdRx.buf, _cmdRx.expected);
}

void GoProCamera::handleSettingNotification(uint8_t *data, size_t len) {
    if (_debugBle) bleDebugDump(DBG_SERIAL, "RX", "GP-0075", data, len);
    if (_settingRx.feed(data, len))
        handleSettingMessage(_settingRx.buf, _settingRx.expected);
}

void GoProCamera::handleQueryNotification(uint8_t *data, size_t len) {
    if (_debugBle) bleDebugDump(DBG_SERIAL, "RX", "GP-0077", data, len);
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
            DBG_SERIAL.println("[GP] Hardware info OK — registering for setting updates");
            _pendingRegisterSettings = true;   // deferred write to avoid callback crash
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

// Setting write response: [setting_id, status]
void GoProCamera::handleSettingMessage(const uint8_t *msg, size_t len) {
    if (len < 2) return;
    uint8_t setting_id = msg[0];
    uint8_t status     = msg[1];
    if (status == 0)
        DBG_SERIAL.printf("[GP] Setting %u OK\n", setting_id);
    else
        DBG_SERIAL.printf("[GP] Setting %u rejected: 0x%02X\n", setting_id, status);
}

// Assembled query response: [query_id, status, id, len, val..., ...]
// Assembled pushed notification (0x92/0x93): [notify_id, id, len, val..., ...]
// (no status byte — it's unsolicited, not a response to a request).
void GoProCamera::handleQueryMessage(const uint8_t *msg, size_t len) {
    if (len < 1) return;
    uint8_t query_id = msg[0];

    if (query_id == GP_QUERY_REGISTER_SETTING || query_id == GP_QUERY_REGISTER_STATUS) {
        if (len < 2) return;
        uint8_t status = msg[1];
        if (status != 0) {
            DBG_SERIAL.printf("[GP] Query 0x%02X failed: status=0x%02X\n", query_id, status);
            return;
        }

        if (query_id == GP_QUERY_REGISTER_SETTING) {
            DBG_SERIAL.println("[GP] Setting registration OK — registering for status updates");
            _pendingRegisterStatus = true;
        } else if (!_gpConnected) {
            _gpConnected = true;
            DBG_SERIAL.println("[GP] Status registration OK — camera ready");
        }

        if (len > 2) {
            parseStatusTlv(msg + 2, len - 2);
            if (_camera.valid && _gpConnected && _cameraCb)
                _cameraCb(_camera);
        }
    } else if (query_id == GP_NOTIFY_SETTING_UPDATE || query_id == GP_NOTIFY_STATUS_UPDATE) {
        if (len > 1) {
            parseStatusTlv(msg + 1, len - 1);
            if (_camera.valid && _gpConnected && _cameraCb)
                _cameraCb(_camera);
        }
    } else {
        DBG_SERIAL.printf("[GP] Unhandled query/notify id 0x%02X\n", query_id);
    }
}

// Parse TLV triplets: [id(1), len(1), value(len)...]
// Handles both status IDs and setting IDs — they share the same query channel
// and do not collide with any of the IDs we register for.
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

        // ── Status IDs ───────────────────────────────────────────────────────
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
                switch (v[0]) {
                    case 1:  _camera.camera_mode = DJI_MODE_PHOTO;     break;
                    case 2:  _camera.camera_mode = DJI_MODE_TIMELAPSE; break;
                    default: _camera.camera_mode = DJI_MODE_VIDEO;     break;
                }
                updated = true;
            }
            break;

        // ── Setting IDs ──────────────────────────────────────────────────────
        // GP_SETTING_RESOLUTION (2): map to CAM_RES_* constants
        case GP_SETTING_RESOLUTION:
            if (vlen >= 1) {
                uint8_t r;
                switch (v[0]) {
                    case 1:   r = CAM_RES_4K;      break;  // 4K 16:9
                    case 2:   r = CAM_RES_4K_WIDE;  break;  // 4K SuperView / 4:3
                    case 4:   r = CAM_RES_2_7K;    break;
                    case 9:   r = CAM_RES_1080P;   break;
                    case 12:  r = CAM_RES_720P;    break;
                    case 31:  r = CAM_RES_8K;      break;
                    case 100: r = CAM_RES_5_3K;    break;
                    default:  r = CAM_RES_UNKNOWN; break;
                }
                _camera.resolution = r;
                updated = true;
                DBG_SERIAL.printf("[GP] Resolution: raw=%u → 0x%02X\n", v[0], r);
            }
            break;

        // GP_SETTING_FPS (3): map to CAM_FPS_* constants
        case GP_SETTING_FPS:
            if (vlen >= 1) {
                uint8_t f;
                switch (v[0]) {
                    case 0:  f = CAM_FPS_240;     break;
                    case 1:  f = CAM_FPS_120;     break;
                    case 2:  f = CAM_FPS_100;     break;
                    case 5:  f = CAM_FPS_60;      break;
                    case 6:  f = CAM_FPS_50;      break;
                    case 8:  f = CAM_FPS_30;      break;
                    case 9:  f = CAM_FPS_25;      break;
                    case 10: f = CAM_FPS_24;      break;
                    case 13: f = CAM_FPS_200;     break;
                    case 15: f = CAM_FPS_400;     break;
                    default: f = CAM_FPS_UNKNOWN; break;
                }
                _camera.fps_idx = f;
                updated = true;
                DBG_SERIAL.printf("[GP] FPS: raw=%u → 0x%02X\n", v[0], f);
            }
            break;

        // GP_SETTING_HYPERSMOOTH (135): map to CAM_EIS_* constants
        case GP_SETTING_HYPERSMOOTH:
            if (vlen >= 1) {
                uint8_t e;
                switch (v[0]) {
                    case 0:   e = CAM_EIS_OFF;     break;
                    case 1:   e = CAM_EIS_LOW;     break;
                    case 2:   e = CAM_EIS_HIGH;    break;
                    case 3:   e = CAM_EIS_BOOST;   break;
                    case 4:   e = CAM_EIS_AUTO;    break;
                    case 100: e = CAM_EIS_STD;     break;
                    default:  e = CAM_EIS_UNKNOWN; break;
                }
                _camera.eis_mode = e;
                updated = true;
                DBG_SERIAL.printf("[GP] HyperSmooth: raw=%u → 0x%02X\n", v[0], e);
            }
            break;
        }
    }

    if (updated)
        _camera.valid = true;
}
