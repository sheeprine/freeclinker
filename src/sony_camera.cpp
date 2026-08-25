#include "sony_camera.h"
#include "camera_registry.h"
#include "config.h"
#include "ble_debug.h"
#include <cstring>

SonyCamera *SonyCamera::_instance = nullptr;

// ─── Public ──────────────────────────────────────────────────────────────────

void SonyCamera::begin() {
    _instance = this;
    BLEDevice::init("ESP32-Sony-Bridge");
    BLEDevice::setPower(ESP_PWR_LVL_P9);
    BLEDevice::setMTU(500);

    // Sony's remote-control characteristics reject writes on an unencrypted
    // link. Just Works pairing: no input/output capability either side, the
    // user just needs the camera's Bluetooth Rmt Ctrl menu open for the
    // first-time bond.
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    BLEDevice::setSecurityCallbacks(this);

    BLESecurity security;
    security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    security.setCapability(ESP_IO_CAP_NONE);
    security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    security.setKeySize(16);

    startScan();
}

void SonyCamera::update() {
    // Deferred GATT discovery: the link just became secured (from
    // onAuthenticationComplete, a GAP stack callback) — do the actual
    // discovery/subscribe here instead, same reasoning as the other camera
    // backends defer their post-callback BLE work into update().
    if (_pendingDiscover && _secured) {
        _pendingDiscover = false;
        if (!discoverServiceAndSubscribe()) {
            _client->disconnect();
            _targetFound   = false;
            _lastAttemptMs = millis();
        }
        return;
    }

    if (_sonyConnected || _bleConnected || _scanning) return;

    if (_targetFound) {
        connectAndSetup();
        return;
    }

    if (millis() - _lastAttemptMs > BLE_RECONNECT_DELAY_MS)
        startScan();
}

// ─── Scan ────────────────────────────────────────────────────────────────────

void SonyCamera::startScan() {
    _targetFound   = false;
    _scanning      = true;
    _candidateAddr = "";
    _candidateName = "";
    _bestAddr      = "";
    _bestRssi      = -128;

    BLEScan *scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(this, false);
    scan->setActiveScan(true);
    scan->setInterval(0x50);
    scan->setWindow(0x30);

    std::string preferred = _registry ? _registry->preferredAddr() : "";
    if (preferred.empty())
        DBG_SERIAL.println("[BLE] Scanning for Sony camera...");
    else
        DBG_SERIAL.printf("[BLE] Scanning for Sony camera %s...\n", preferred.c_str());

    scan->start(BLE_SCAN_DURATION_SECS, scanDoneCallback, false);
}

void SonyCamera::scanDoneCallback(BLEScanResults /*r*/) {
    if (!_instance) return;
    _instance->_scanning = false;
    if (_instance->_targetFound) return;

    if (_instance->_matchMode == CAM_MATCH_BEST_SIGNAL) {
        if (!_instance->_bestAddr.empty()) {
            DBG_SERIAL.printf("[BLE] Best signal — connecting to %s (rssi=%d)\n",
                              _instance->_bestAddr.c_str(), _instance->_bestRssi);
            _instance->_targetAddr  = _instance->_bestAddr;
            _instance->_targetName  = _instance->_bestName;
            _instance->_targetType  = _instance->_bestType;
            _instance->_targetFound = true;
        } else {
            DBG_SERIAL.println("[BLE] Scan done — no Sony camera found");
            _instance->_lastAttemptMs = millis();
        }
        return;
    }

    if (!_instance->_candidateAddr.empty() && _instance->_matchMode != CAM_MATCH_STRICT) {
        DBG_SERIAL.printf("[BLE] Preferred not found — connecting to %s\n",
                          _instance->_candidateAddr.c_str());
        _instance->_targetAddr  = _instance->_candidateAddr;
        _instance->_targetName  = _instance->_candidateName;
        _instance->_targetType  = _instance->_candidateType;
        _instance->_targetFound = true;
    } else {
        if (_instance->_matchMode == CAM_MATCH_STRICT && !_instance->_candidateAddr.empty())
            DBG_SERIAL.println("[BLE] Preferred not found — strict mode, skipping other cameras");
        else
            DBG_SERIAL.println("[BLE] Scan done — no Sony camera found");
        _instance->_lastAttemptMs = millis();
    }
}

// onResult runs for every advertised device during scan. Sony cameras are
// identified by manufacturer-specific data: company ID 0x012D (Sony) then
// device type 0x0300 (camera). Device name is a fallback (most Alpha bodies
// advertise as "ILCE-xxxx").
void SonyCamera::onResult(BLEAdvertisedDevice device) {
    bool isSony = false;

    if (device.haveManufacturerData()) {
        const std::string mfr = device.getManufacturerData();
        if (mfr.size() >= 4 &&
            (uint8_t)mfr[0] == SONY_MFR_ID_LO && (uint8_t)mfr[1] == SONY_MFR_ID_HI &&
            (uint8_t)mfr[2] == SONY_MFR_DEVTYPE_LO && (uint8_t)mfr[3] == SONY_MFR_DEVTYPE_HI) {
            isSony = true;

            // Diagnostic only — surface whether the camera's pairing menu is
            // open, since a first-time bond will otherwise fail silently.
            for (size_t i = 0; i + 1 < mfr.size(); i++) {
                if ((uint8_t)mfr[i] != SONY_PAIRING_TAG) continue;
                uint8_t flags = (uint8_t)mfr[i + 1];
                bool pairingOpen = (flags & SONY_PAIRING_ENABLED_BIT) &&
                                   (flags & SONY_REMOTE_ENABLED_BIT);
                if (!pairingOpen)
                    DBG_SERIAL.println("[BLE] Sony camera found but not in pairing mode — "
                                       "open Bluetooth Rmt Ctrl on the camera for a new pairing");
                break;
            }
        }
    }

    if (!isSony && device.haveName() &&
        device.getName().find(SONY_DEVICE_NAME_PREFIX) != std::string::npos) {
        isSony = true;
    }

    if (!isSony) return;

    std::string addr = device.getAddress().toString();
    std::string name = device.haveName() ? device.getName() : "Sony Alpha";

    DBG_SERIAL.printf("[BLE] Found Sony camera: \"%s\"  addr=%s  rssi=%d\n",
                      name.c_str(), addr.c_str(), device.getRSSI());

    if (_matchMode == CAM_MATCH_BEST_SIGNAL) {
        int8_t rssi = device.getRSSI();
        if (_bestAddr.empty() || rssi > _bestRssi) {
            _bestAddr = addr;
            _bestName = name;
            _bestType = device.getAddressType();
            _bestRssi = rssi;
        }
        return;  // keep scanning the full window to find the strongest signal
    }

    // Keep first found as fallback
    if (_candidateAddr.empty()) {
        _candidateAddr = addr;
        _candidateName = name;
        _candidateType = device.getAddressType();
    }

    std::string preferred = _registry ? _registry->preferredAddr() : "";

    if (!preferred.empty() && addr == preferred) {
        BLEDevice::getScan()->stop();
        _targetAddr  = addr;
        _targetName  = name;
        _targetType  = device.getAddressType();
        _targetFound = true;
        _scanning    = false;
    } else if (preferred.empty() && !_targetFound) {
        BLEDevice::getScan()->stop();
        _targetAddr  = addr;
        _targetName  = name;
        _targetType  = device.getAddressType();
        _targetFound = true;
        _scanning    = false;
    }
}

// ─── Connect, security, characteristic discovery ─────────────────────────────

bool SonyCamera::connectAndSetup() {
    DBG_SERIAL.printf("[BLE] Connecting to Sony camera %s ...\n", _targetAddr.c_str());

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

    _bleConnected = true;

    DBG_SERIAL.println("[BLE] Requesting encrypted/bonded link (Sony rejects writes until bonded)...");
    BLEAddress peerAddr = _client->getPeerAddress();
    esp_ble_set_encryption(*peerAddr.getNative(), ESP_BLE_SEC_ENCRYPT);
    return true;
}

// Runs once onAuthenticationComplete() reports the link is secured.
bool SonyCamera::discoverServiceAndSubscribe() {
    DBG_SERIAL.println("[BLE] Link secured — discovering Sony remote-control service");

    BLERemoteService *svc = _client->getService(BLEUUID(SONY_SERVICE_UUID));
    if (!svc) {
        DBG_SERIAL.println("[BLE] Sony service 8000FF00 not found");
        return false;
    }

    BLERemoteCharacteristic *notifyCh = svc->getCharacteristic(BLEUUID(SONY_CHAR_NOTIFY));
    if (!notifyCh || !notifyCh->canNotify()) {
        DBG_SERIAL.println("[BLE] Notify char 0xFF02 not found or not notifiable");
        return false;
    }
    notifyCh->registerForNotify(notifyCallback);
    DBG_SERIAL.println("[BLE] Subscribed to 0xFF02 (camera notify)");

    _cmdChar = svc->getCharacteristic(BLEUUID(SONY_CHAR_COMMAND));
    if (!_cmdChar || !_cmdChar->canWrite()) {
        DBG_SERIAL.println("[BLE] Command char 0xFF01 not found or not writable");
        return false;
    }

    // Neither service reports resolution/fps/EIS — mark EIS explicitly
    // unknown rather than implying "off". Battery (if the 0xCC10 discovery
    // below succeeds) fills in _camera.percent asynchronously.
    _camera = CameraData{};
    _camera.eis_mode = CAM_EIS_UNKNOWN;

    discoverBatteryService();

    _sonyConnected = true;

    if (_registry)
        _registry->onConnected(_targetName.c_str(), _targetAddr.c_str(),
                               (uint8_t)_targetType, /*Sony=*/3);

    DBG_SERIAL.println("[BLE] Sony camera ready — remote control active");
    return true;
}

// Camera Control service (8000CC00) — battery reporting. Best-effort: some
// models reduce or disable this service while "Bluetooth Rmt Ctrl" is active,
// so a missing service/characteristic just means no battery telemetry, not a
// connection failure.
void SonyCamera::discoverBatteryService() {
    BLERemoteService *ccSvc = _client->getService(BLEUUID(SONY_CC_SERVICE_UUID));
    if (!ccSvc) {
        DBG_SERIAL.println("[BLE] Camera Control service 8000CC00 not available — no battery telemetry");
        return;
    }

    BLERemoteCharacteristic *battCh = ccSvc->getCharacteristic(BLEUUID(SONY_CC_CHAR_BATTERY));
    if (!battCh || !battCh->canNotify()) {
        DBG_SERIAL.println("[BLE] Battery char 0xCC10 not found — no battery telemetry");
        return;
    }
    battCh->registerForNotify(batteryNotifyCallback);
    DBG_SERIAL.println("[BLE] Subscribed to 0xCC10 (battery notify)");

    // Read once immediately rather than waiting for the camera to push a
    // change, so battery shows up right after connecting.
    if (battCh->canRead()) {
        std::string val = battCh->readValue();
        if (!val.empty())
            handleBatteryNotification((uint8_t *)val.data(), val.size());
    }
}

void SonyCamera::onConnect(BLEClient * /*c*/) {
    DBG_SERIAL.println("[BLE] onConnect");
}

void SonyCamera::onDisconnect(BLEClient * /*c*/) {
    DBG_SERIAL.println("[BLE] Sony camera disconnected — will rescan");
    _sonyConnected   = false;
    _bleConnected    = false;
    _secured         = false;
    _pendingDiscover = false;
    _cmdChar         = nullptr;
    _targetFound     = false;
    _targetAddr      = "";
    _targetName      = "";
    _candidateAddr   = "";
    _candidateName   = "";
    _lastAttemptMs   = millis();
}

// ─── Security callbacks (Just Works pairing + bonding) ───────────────────────

uint32_t SonyCamera::onPassKeyRequest() {
    DBG_SERIAL.println("[BLE] Passkey requested (unexpected under Just Works)");
    return 0;
}

void SonyCamera::onPassKeyNotify(uint32_t pass_key) {
    DBG_SERIAL.printf("[BLE] Passkey notify: %06u\n", (unsigned)pass_key);
}

bool SonyCamera::onSecurityRequest() {
    return true;
}

bool SonyCamera::onConfirmPIN(uint32_t pin) {
    DBG_SERIAL.printf("[BLE] Confirm PIN: %06u — accepting\n", (unsigned)pin);
    return true;
}

void SonyCamera::onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
    if (cmpl.success) {
        DBG_SERIAL.println("[BLE] Link secured (bonded)");
        _secured         = true;
        _pendingDiscover = true;
    } else {
        DBG_SERIAL.printf("[BLE] Pairing failed (reason=0x%02X) — open Bluetooth Rmt Ctrl "
                          "on the camera to allow a new pairing, then retry\n",
                          cmpl.fail_reason);
        if (_client) _client->disconnect();
    }
}

// ─── Sony commands ────────────────────────────────────────────────────────────

void SonyCamera::sendCommandByte(uint8_t cmd) {
    if (!_cmdChar) return;
    uint8_t buf[2] = {0x01, cmd};
    if (_debugBle) bleDebugDump(DBG_SERIAL, "TX", "0xFF01", buf, sizeof(buf));
    _cmdChar->writeValue(buf, sizeof(buf), true);
}

// Sony's remote buttons are momentary — down then up simulates one press.
void SonyCamera::sendButtonPress(uint8_t downCmd, uint8_t upCmd) {
    sendCommandByte(downCmd);
    sendCommandByte(upCmd);
}

// The record button toggles recording state rather than commanding an
// explicit start/stop — guard on the last known state so a redundant call
// (e.g. arming while already recording) doesn't toggle the wrong way.
bool SonyCamera::startRecording() {
    if (!_cmdChar) return false;
    if (_camera.valid && _camera.recording) return true;
    sendButtonPress(SONY_CMD_RECORD_DOWN, SONY_CMD_RECORD_UP);
    DBG_SERIAL.println("[Sony] Record toggle sent (start)");
    return true;
}

bool SonyCamera::stopRecording() {
    if (!_cmdChar) return false;
    if (_camera.valid && !_camera.recording) return true;
    sendButtonPress(SONY_CMD_RECORD_DOWN, SONY_CMD_RECORD_UP);
    DBG_SERIAL.println("[Sony] Record toggle sent (stop)");
    return true;
}

bool SonyCamera::switchCameraMode(uint8_t /*mode*/) {
    DBG_SERIAL.println("[Sony] switchCameraMode not supported by the Sony BLE remote protocol");
    return false;
}

// ─── Notify callback (static trampoline) ──────────────────────────────────────

void SonyCamera::notifyCallback(BLERemoteCharacteristic * /*ch*/,
                                 uint8_t *data, size_t len, bool /*isNotify*/) {
    if (_instance) _instance->handleNotification(data, len);
}

// Notifications are [0x02, tag, value] triplets. Only recording state maps to
// CameraData — focus/shutter state have no corresponding OSD field, so they're
// only logged.
void SonyCamera::handleNotification(uint8_t *data, size_t len) {
    if (_debugBle) bleDebugDump(DBG_SERIAL, "RX", "0xFF02", data, len);
    if (len < 3 || data[0] != 0x02) return;

    uint8_t tag   = data[1];
    uint8_t value = data[2];

    switch (tag) {
    case SONY_NOTIFY_TAG_RECORDING:
        _camera.recording = (value == SONY_NOTIFY_VALUE_ACTIVE);
        _camera.valid     = true;
        DBG_SERIAL.printf("[Sony] Recording %s\n", _camera.recording ? "started" : "stopped");
        if (_cameraCb) _cameraCb(_camera);
        break;

    case SONY_NOTIFY_TAG_FOCUS:
        DBG_SERIAL.printf("[Sony] Focus %s\n",
                          value == SONY_NOTIFY_VALUE_ACTIVE ? "acquired" : "ready");
        break;

    case SONY_NOTIFY_TAG_SHUTTER:
        DBG_SERIAL.printf("[Sony] Shutter %s\n",
                          value == SONY_NOTIFY_VALUE_ACTIVE ? "active" : "ready");
        break;

    default:
        break;
    }
}

void SonyCamera::batteryNotifyCallback(BLERemoteCharacteristic * /*ch*/,
                                        uint8_t *data, size_t len, bool /*isNotify*/) {
    if (_instance) _instance->handleBatteryNotification(data, len);
}

// Battery Info (0xCC10) payload — see sony_protocol.h for the byte layout.
// Reports the body battery pack if present, otherwise the first pack.
void SonyCamera::handleBatteryNotification(uint8_t *data, size_t len) {
    if (_debugBle) bleDebugDump(DBG_SERIAL, "RX", "0xCC10", data, len);
    if (len < 4 + SONY_BATTERY_PACK_SIZE) return;

    uint16_t dataType = ((uint16_t)data[1] << 8) | data[2];
    if (dataType != 0x0000) return;   // not a battery-info payload

    uint8_t count = data[3];
    size_t  offset = 4;

    for (uint8_t i = 0; i < count && offset + SONY_BATTERY_PACK_SIZE <= len; i++) {
        uint8_t position = data[offset + 1];
        if (position == SONY_BATTERY_POS_BODY || i == 0) {
            uint32_t pct = ((uint32_t)data[offset + 3] << 24) | ((uint32_t)data[offset + 4] << 16) |
                           ((uint32_t)data[offset + 5] <<  8) |  (uint32_t)data[offset + 6];
            if (pct <= 100) {
                _camera.percent = (uint8_t)pct;
                _camera.valid   = true;
                DBG_SERIAL.printf("[Sony] Battery: %u%%\n", _camera.percent);
                if (_cameraCb) _cameraCb(_camera);
            } else {
                DBG_SERIAL.printf("[Sony] Battery payload out of range (raw=%lu) — "
                                  "layout may not match this camera, check debug_ble dump\n",
                                  (unsigned long)pct);
            }
            if (position == SONY_BATTERY_POS_BODY) break;
        }
        offset += SONY_BATTERY_PACK_SIZE;
    }
}
