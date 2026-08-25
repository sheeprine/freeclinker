#include "insta360_camera.h"
#include "camera_registry.h"
#include "config.h"
#include "ble_debug.h"
#include <cctype>

Insta360Camera *Insta360Camera::_instance = nullptr;

// ─── Public ──────────────────────────────────────────────────────────────────

void Insta360Camera::begin() {
    _instance = this;
    BLEDevice::init("FreeCLinker");
    BLEDevice::setPower(ESP_PWR_LVL_P9);

    startScan();
}

void Insta360Camera::update() {
    if (_instaConnected || _bleConnected || _scanning) return;

    if (_targetFound) {
        connectAndSetup();
        return;
    }

    if (millis() - _lastAttemptMs > BLE_RECONNECT_DELAY_MS)
        startScan();
}

// ─── Scan ────────────────────────────────────────────────────────────────────

void Insta360Camera::startScan() {
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
        DBG_SERIAL.println("[BLE] Scanning for Insta360 camera...");
    else
        DBG_SERIAL.printf("[BLE] Scanning for Insta360 camera %s...\n", preferred.c_str());

    scan->start(BLE_SCAN_DURATION_SECS, scanDoneCallback, false);
}

void Insta360Camera::scanDoneCallback(BLEScanResults /*r*/) {
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
            DBG_SERIAL.println("[BLE] Scan done — no Insta360 camera found");
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
            DBG_SERIAL.println("[BLE] Scan done — no Insta360 camera found");
        _instance->_lastAttemptMs = millis();
    }
}

// No vendor scan filter (manufacturer ID, dedicated advertised service) for
// this family is documented/verified anywhere we could check, unlike DJI/
// GoPro/Sony/Blackmagic — this name check is a best-effort heuristic only.
// Cameras are user-nameable and product docs show the factory default is
// "<model> <serial-suffix>" (e.g. "X3 123456"), not a fixed "Insta360"
// prefix, so a miss here doesn't necessarily mean it isn't one.
static bool looksLikeInsta360Name(const std::string &name) {
    std::string lower = name;
    for (char &c : lower) c = (char)tolower((unsigned char)c);

    if (lower.find("insta360") != std::string::npos) return true;

    static const char *prefixes[] = {
        "x3", "x4", "x5", "one r", "one x", "go 2", "go 3", "go3s", "ace",
    };
    for (const char *p : prefixes)
        if (lower.rfind(p, 0) == 0) return true;

    return false;
}

// Identify candidates by the advertised primary service UUID 0xBE80 where
// present; fall back to the name heuristic above when it isn't (some BLE
// stacks omit a custom service from the advertisement payload — its
// 31-byte budget is easily eaten by the local name — so absence here isn't
// conclusive either).
void Insta360Camera::onResult(BLEAdvertisedDevice device) {
    bool isInsta = device.haveServiceUUID() &&
                   device.isAdvertisingService(BLEUUID(INSTA_SERVICE_UUID));

    if (!isInsta && device.haveName() && looksLikeInsta360Name(device.getName()))
        isInsta = true;

    if (!isInsta) return;

    std::string addr = device.getAddress().toString();
    std::string name = device.haveName() ? device.getName() : "Insta360 Camera";

    DBG_SERIAL.printf("[BLE] Found Insta360 camera: \"%s\"  addr=%s  rssi=%d\n",
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

    if ((!preferred.empty() && addr == preferred) ||
        (preferred.empty() && !_targetFound)) {
        BLEDevice::getScan()->stop();
        _targetAddr  = addr;
        _targetName  = name;
        _targetType  = device.getAddressType();
        _targetFound = true;
        _scanning    = false;
    }
}

// ─── Connect, service discovery, subscriptions ───────────────────────────────

bool Insta360Camera::connectAndSetup() {
    DBG_SERIAL.printf("[BLE] Connecting to Insta360 camera %s ...\n", _targetAddr.c_str());

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

    _bleConnected = true;

    // Bump the ATT MTU so a command response (documented examples run past
    // 20 bytes) arrives as a single notification instead of needing manual
    // multi-packet reassembly.
    if (_client->setMTU(500))
        DBG_SERIAL.printf("[BLE] MTU exchange OK (got %u)\n", _client->getMTU());
    else
        DBG_SERIAL.println("[BLE] MTU exchange failed — staying at 23");

    BLERemoteService *svc = _client->getService(BLEUUID(INSTA_SERVICE_UUID));
    if (!svc) {
        DBG_SERIAL.println("[BLE] Insta360 camera-control service (0xBE80) not found");
        _client->disconnect();
        _bleConnected  = false;
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }

    BLERemoteCharacteristic *notifyCh = svc->getCharacteristic(BLEUUID(INSTA_CHAR_NOTIFY));
    if (!notifyCh || !notifyCh->canNotify()) {
        DBG_SERIAL.println("[BLE] Notify char 0xBE82 not found or not notifiable");
        _client->disconnect();
        _bleConnected  = false;
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }
    notifyCh->registerForNotify(notifyCallback);

    _writeChar = svc->getCharacteristic(BLEUUID(INSTA_CHAR_WRITE));
    if (!_writeChar || !_writeChar->canWrite()) {
        DBG_SERIAL.println("[BLE] Write char 0xBE81 not found or not writable");
        _client->disconnect();
        _bleConnected  = false;
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }

    // No resolution/fps/EIS telemetry available — the camera's status
    // protobuf isn't decodable here (see insta360_protocol.h). Battery (if
    // the discovery below succeeds) fills in _camera.percent asynchronously.
    _camera = CameraData{};
    _camera.eis_mode = CAM_EIS_UNKNOWN;

    discoverBatteryService();

    _instaConnected = true;

    if (_registry)
        _registry->onConnected(_targetName.c_str(), _targetAddr.c_str(),
                               (uint8_t)_targetType, /*Insta360=*/5);

    DBG_SERIAL.println("[BLE] Insta360 camera ready — recording control active");
    return true;
}

// Standard Bluetooth SIG Battery Service — best-effort, since it isn't part
// of the documented 0xBE80 command channel. A missing service/characteristic
// just means no battery telemetry, not a connection failure.
void Insta360Camera::discoverBatteryService() {
    BLERemoteService *battSvc = _client->getService(BLEUUID((uint16_t)0x180F));
    if (!battSvc) {
        DBG_SERIAL.println("[BLE] Battery Service (0x180F) not available — no battery telemetry");
        return;
    }

    BLERemoteCharacteristic *battCh = battSvc->getCharacteristic(BLEUUID((uint16_t)0x2A19));
    if (!battCh || !battCh->canNotify()) {
        DBG_SERIAL.println("[BLE] Battery Level char (0x2A19) not found — no battery telemetry");
        return;
    }
    battCh->registerForNotify(batteryNotifyCallback);
    DBG_SERIAL.println("[BLE] Subscribed to 0x2A19 (battery notify)");

    if (battCh->canRead()) {
        std::string val = battCh->readValue();
        if (!val.empty())
            handleBatteryNotification((uint8_t *)val.data(), val.size());
    }
}

void Insta360Camera::onConnect(BLEClient * /*c*/) {
    DBG_SERIAL.println("[BLE] onConnect");
}

void Insta360Camera::onDisconnect(BLEClient * /*c*/) {
    DBG_SERIAL.println("[BLE] Insta360 camera disconnected — will rescan");
    _instaConnected      = false;
    _bleConnected        = false;
    _writeChar           = nullptr;
    _targetFound         = false;
    _targetAddr          = "";
    _targetName          = "";
    _candidateAddr       = "";
    _candidateName       = "";
    _awaitingRecordAck   = false;
    _lastAttemptMs       = millis();
}

// ─── Commands ─────────────────────────────────────────────────────────────────

bool Insta360Camera::sendCommand(uint16_t cmd) {
    if (!_writeChar) return false;
    uint8_t pkt[INSTA_PACKET_LEN];
    instaBuildCommand(cmd, _seq++, pkt);
    if (_debugBle) bleDebugDump(DBG_SERIAL, "TX", "0xBE81", pkt, sizeof(pkt));
    _writeChar->writeValue(pkt, sizeof(pkt), true);
    return true;
}

// The camera only tells us whether the command it just received succeeded
// (a 200-OK ack) — there's no separate, decodable "current recording state"
// push to fall back on (see insta360_protocol.h), so state is derived from
// the command we sent, gated on that ack.
bool Insta360Camera::startRecording() {
    if (!_writeChar) return false;
    if (_camera.valid && _camera.recording) return true;
    if (!sendCommand(INSTA_CMD_START_VIDEO)) return false;
    _awaitingRecordAck   = true;
    _pendingRecordTarget = true;
    DBG_SERIAL.println("[Insta360] Start video sent");
    return true;
}

bool Insta360Camera::stopRecording() {
    if (!_writeChar) return false;
    if (_camera.valid && !_camera.recording) return true;
    if (!sendCommand(INSTA_CMD_STOP_VIDEO)) return false;
    _awaitingRecordAck   = true;
    _pendingRecordTarget = false;
    DBG_SERIAL.println("[Insta360] Stop video sent");
    return true;
}

bool Insta360Camera::switchCameraMode(uint8_t /*mode*/) {
    DBG_SERIAL.println("[Insta360] switchCameraMode not supported — protocol only exposes "
                       "separate start-recording-in-mode-X commands, not a mode switch");
    return false;
}

// ─── Notify callbacks (static trampolines) ───────────────────────────────────

void Insta360Camera::notifyCallback(BLERemoteCharacteristic * /*ch*/,
                                     uint8_t *data, size_t len, bool /*isNotify*/) {
    if (_instance) _instance->handleNotification(data, len);
}

void Insta360Camera::batteryNotifyCallback(BLERemoteCharacteristic * /*ch*/,
                                            uint8_t *data, size_t len, bool /*isNotify*/) {
    if (_instance) _instance->handleBatteryNotification(data, len);
}

// Only the fixed header is parsed (see insta360_protocol.h) — a bare 7-byte
// Keep-Alive-shaped ack carries no response code and is just logged; a full
// Phone Command response's code at [7:9] resolves whichever recording
// command is currently in flight. The protobuf body (if any) is never
// decoded — no available schema.
void Insta360Camera::handleNotification(uint8_t *data, size_t len) {
    if (_debugBle) bleDebugDump(DBG_SERIAL, "RX", "0xBE82", data, len);

    bool isKeepAlive = len >= 7  && data[4] == INSTA_MSGTYPE_KEEPALIVE && data[5] == 0 && data[6] == 0;
    bool isCommand   = len >= 9  && data[4] == INSTA_MSGTYPE_COMMAND   && data[5] == 0 && data[6] == 0;

    if (!isKeepAlive && !isCommand) return;

    if (!_awaitingRecordAck) return;  // not something we're waiting on

    if (isKeepAlive) return;  // bare ack, no response code to check yet — wait for the real one

    uint16_t code = (uint16_t)data[7] | ((uint16_t)data[8] << 8);
    _awaitingRecordAck = false;

    if (code != INSTA_RESP_OK) {
        DBG_SERIAL.printf("[Insta360] Recording command failed (response=%u)\n", code);
        return;
    }

    _camera.recording = _pendingRecordTarget;
    _camera.valid      = true;
    DBG_SERIAL.printf("[Insta360] Recording %s\n", _camera.recording ? "started" : "stopped");
    if (_cameraCb) _cameraCb(_camera);
}

// Battery Level (0x2A19) — standard BLE SIG format: a single byte, 0-100.
void Insta360Camera::handleBatteryNotification(uint8_t *data, size_t len) {
    if (_debugBle) bleDebugDump(DBG_SERIAL, "RX", "0x2A19", data, len);
    if (len < 1 || data[0] > 100) return;

    _camera.percent = data[0];
    _camera.valid   = true;
    DBG_SERIAL.printf("[Insta360] Battery: %u%%\n", _camera.percent);
    if (_cameraCb) _cameraCb(_camera);
}
