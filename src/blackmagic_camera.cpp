#include "blackmagic_camera.h"
#include "camera_registry.h"
#include "config.h"
#include "ble_debug.h"

BlackmagicCamera *BlackmagicCamera::_instance = nullptr;

// ─── Public ──────────────────────────────────────────────────────────────────

void BlackmagicCamera::begin() {
    _instance = this;
    BLEDevice::init(BMD_DEVICE_NAME);
    BLEDevice::setPower(ESP_PWR_LVL_P9);

    // Outgoing/Incoming Camera Control and Camera Status are all marked
    // "(encrypted)" in the protocol doc — the first read/write/subscribe on
    // one of them auto-triggers bonding. ESP_IO_CAP_IN (keyboard-only) makes
    // the stack pick Passkey Entry, matching what the camera expects (it
    // shows the PIN, we type it back) — see onPassKeyRequest() below.
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    BLEDevice::setSecurityCallbacks(this);

    BLESecurity security;
    security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
    security.setCapability(ESP_IO_CAP_IN);
    security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    startScan();
}

void BlackmagicCamera::update() {
    if (_bmdConnected || _scanning) return;

    if (_targetFound) {
        connectAndSetup();
        return;
    }

    if (millis() - _lastAttemptMs > BLE_RECONNECT_DELAY_MS)
        startScan();
}

// ─── Scan ────────────────────────────────────────────────────────────────────

void BlackmagicCamera::startScan() {
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
        DBG_SERIAL.println("[BLE] Scanning for Blackmagic camera...");
    else
        DBG_SERIAL.printf("[BLE] Scanning for Blackmagic camera %s...\n", preferred.c_str());

    scan->start(BLE_SCAN_DURATION_SECS, scanDoneCallback, false);
}

void BlackmagicCamera::scanDoneCallback(BLEScanResults /*r*/) {
    if (!_instance) return;
    _instance->_scanning = false;
    if (!_instance->_targetFound) {
        if (!_instance->_candidateAddr.empty() && !_instance->_strictCamera) {
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
                DBG_SERIAL.println("[BLE] Scan done — no Blackmagic camera found");
            _instance->_lastAttemptMs = millis();
        }
    }
}

// Blackmagic cameras advertise the Blackmagic Camera Service UUID directly,
// so filtering on it (rather than manufacturer data) is reliable here.
void BlackmagicCamera::onResult(BLEAdvertisedDevice device) {
    if (!device.haveServiceUUID() || !device.isAdvertisingService(BLEUUID(BMD_SERVICE_UUID)))
        return;

    std::string addr = device.getAddress().toString();
    std::string name = device.haveName() ? device.getName() : "Blackmagic Camera";

    DBG_SERIAL.printf("[BLE] Found Blackmagic camera: \"%s\"  addr=%s  rssi=%d\n",
                      name.c_str(), addr.c_str(), device.getRSSI());

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

bool BlackmagicCamera::connectAndSetup() {
    DBG_SERIAL.printf("[BLE] Connecting to Blackmagic camera %s ...\n", _targetAddr.c_str());

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

    BLERemoteService *svc = _client->getService(BLEUUID(BMD_SERVICE_UUID));
    if (!svc) {
        DBG_SERIAL.println("[BLE] Blackmagic Camera Service not found");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }

    // Plaintext characteristics — no bonding required for these.
    BLERemoteCharacteristic *verChar = svc->getCharacteristic(BLEUUID(BMD_CHAR_PROTOCOL_VERSION));
    if (verChar && verChar->canRead()) {
        std::string ver = verChar->readValue();
        DBG_SERIAL.printf("[BLE] Camera CCU protocol version: %s\n", ver.c_str());
    }

    BLERemoteCharacteristic *nameChar = svc->getCharacteristic(BLEUUID(BMD_CHAR_DEVICE_NAME));
    if (nameChar && nameChar->canWrite())
        nameChar->writeValue(std::string(BMD_DEVICE_NAME), true);

    _outgoingChar = svc->getCharacteristic(BLEUUID(BMD_CHAR_OUTGOING_CONTROL));
    if (!_outgoingChar) {
        DBG_SERIAL.println("[BLE] Outgoing Camera Control characteristic not found");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }

    // Encrypted characteristics — subscribing here auto-triggers bonding on
    // a first-time pairing (blocks in onPassKeyRequest() until the PIN shown
    // on the camera screen is typed into the serial console).
    BLERemoteCharacteristic *ctrlChar = svc->getCharacteristic(BLEUUID(BMD_CHAR_INCOMING_CONTROL));
    if (!ctrlChar || !ctrlChar->canNotify()) {
        DBG_SERIAL.println("[BLE] Incoming Camera Control characteristic not found or not notifiable");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }
    ctrlChar->registerForNotify(controlNotifyCallback);

    BLERemoteCharacteristic *statusChar = svc->getCharacteristic(BLEUUID(BMD_CHAR_CAMERA_STATUS));
    if (!statusChar || !statusChar->canNotify()) {
        DBG_SERIAL.println("[BLE] Camera Status characteristic not found or not notifiable");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }
    statusChar->registerForNotify(statusNotifyCallback);

    // No resolution/fps/EIS/battery telemetry is available over this
    // protocol — mark EIS explicitly unknown rather than implying "off".
    _camera = CameraData{};
    _camera.eis_mode = CAM_EIS_UNKNOWN;

    _bmdConnected = true;

    if (_registry)
        _registry->onConnected(_targetName.c_str(), _targetAddr.c_str(),
                               (uint8_t)_targetType, /*Blackmagic=*/4);

    DBG_SERIAL.println("[BLE] Blackmagic camera ready — recording control active");
    return true;
}

void BlackmagicCamera::onConnect(BLEClient * /*c*/) {
    DBG_SERIAL.println("[BLE] onConnect");
}

void BlackmagicCamera::onDisconnect(BLEClient * /*c*/) {
    DBG_SERIAL.println("[BLE] Blackmagic camera disconnected — will rescan");
    _bmdConnected  = false;
    _cameraReady   = false;
    _outgoingChar  = nullptr;
    _targetFound   = false;
    _targetAddr    = "";
    _targetName    = "";
    _candidateAddr = "";
    _candidateName = "";
    _lastAttemptMs = millis();
}

// ─── Security callbacks (Passkey Entry pairing) ──────────────────────────────

// The camera displays a 6-digit PIN on its own screen during first-time
// pairing and expects it typed back in on the connecting device. The ESP32
// has no display/keypad, so this blocks the BLE task reading digits from the
// serial console — the same approach used by schoolpost/BlueMagic32 and
// marklysze/Magic-Pocket-Control-ESP32, the two known-working community
// libraries for this exact protocol. This only happens once per bond.
uint32_t BlackmagicCamera::onPassKeyRequest() {
    DBG_SERIAL.println("[BLE] Blackmagic camera requesting pairing PIN.");
    DBG_SERIAL.println("[BLE] Enter the 6-digit PIN shown on the camera screen, then press Enter:");
    uint32_t pin = 0;
    char ch;
    do {
        while (!DBG_SERIAL.available()) delay(1);
        ch = (char)DBG_SERIAL.read();
        if (ch >= '0' && ch <= '9') {
            pin = pin * 10 + (uint32_t)(ch - '0');
            DBG_SERIAL.print(ch);
        }
    } while (ch != '\n');
    DBG_SERIAL.println();
    return pin;
}

void BlackmagicCamera::onPassKeyNotify(uint32_t pass_key) {
    DBG_SERIAL.printf("[BLE] Passkey notify: %06u\n", (unsigned)pass_key);
}

bool BlackmagicCamera::onSecurityRequest() {
    return true;
}

bool BlackmagicCamera::onConfirmPIN(uint32_t pin) {
    DBG_SERIAL.printf("[BLE] Confirm PIN: %06u — accepting\n", (unsigned)pin);
    return true;
}

void BlackmagicCamera::onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
    if (cmpl.success)
        DBG_SERIAL.println("[BLE] Link secured (bonded)");
    else
        DBG_SERIAL.printf("[BLE] Pairing failed (reason=0x%02X)\n", cmpl.fail_reason);
}

// ─── Blackmagic commands ──────────────────────────────────────────────────────

void BlackmagicCamera::sendTransportMode(uint8_t mode) {
    uint8_t pkt[12];
    uint8_t len = bmdBuildTransportModeCommand(mode, pkt);
    if (_debugBle) bleDebugDump(DBG_SERIAL, "TX", "OutgoingControl", pkt, len);
    _outgoingChar->writeValue(pkt, len, true);
}

// Transport Mode is an absolute assign-value (not a toggle like Sony's
// remote buttons), so re-sending the same mode while already in it is
// harmless — no need to guard on current state first.
bool BlackmagicCamera::startRecording() {
    if (!_outgoingChar) return false;
    sendTransportMode(BMD_TRANSPORT_MODE_RECORD);
    DBG_SERIAL.println("[Blackmagic] Record start sent");
    return true;
}

bool BlackmagicCamera::stopRecording() {
    if (!_outgoingChar) return false;
    sendTransportMode(BMD_TRANSPORT_MODE_PREVIEW);
    DBG_SERIAL.println("[Blackmagic] Record stop sent");
    return true;
}

bool BlackmagicCamera::switchCameraMode(uint8_t /*mode*/) {
    DBG_SERIAL.println("[Blackmagic] switchCameraMode not supported — no mode-switch command in this protocol");
    return false;
}

// ─── Notify callbacks (static trampolines) ───────────────────────────────────

void BlackmagicCamera::controlNotifyCallback(BLERemoteCharacteristic * /*ch*/,
                                              uint8_t *data, size_t len, bool /*isNotify*/) {
    if (_instance) _instance->handleControlNotification(data, len);
}

void BlackmagicCamera::statusNotifyCallback(BLERemoteCharacteristic * /*ch*/,
                                             uint8_t *data, size_t len, bool /*isNotify*/) {
    if (_instance) _instance->handleStatusNotification(data, len);
}

// Incoming Camera Control messages use the same [dest,len,cmdId,reserved,
// category,parameter,type,operation,data...] framing as outgoing ones. Only
// Transport Mode (category 10, parameter 1) maps to CameraData — anything
// else the camera reports (codec, resolution, white balance, etc.) is
// ignored.
void BlackmagicCamera::handleControlNotification(uint8_t *data, size_t len) {
    if (_debugBle) bleDebugDump(DBG_SERIAL, "RX", "IncomingControl", data, len);
    if (len < 9) return;
    if (data[4] != BMD_CATEGORY_MEDIA || data[5] != BMD_MEDIA_PARAM_TRANSPORT_MODE) return;

    bool recording = (data[8] == BMD_TRANSPORT_MODE_RECORD);
    if (_camera.valid && _camera.recording == recording) return;

    _camera.recording = recording;
    _camera.valid     = true;
    DBG_SERIAL.printf("[Blackmagic] Recording %s\n", recording ? "started" : "stopped");
    if (_cameraCb) _cameraCb(_camera);
}

void BlackmagicCamera::handleStatusNotification(uint8_t *data, size_t len) {
    if (_debugBle) bleDebugDump(DBG_SERIAL, "RX", "CameraStatus", data, len);
    if (len < 1) return;

    bool ready = (data[0] & BMD_STATUS_CAMERA_READY) != 0;
    if (ready != _cameraReady) {
        _cameraReady = ready;
        DBG_SERIAL.printf("[Blackmagic] Camera status: power=%d connected=%d ready=%d\n",
                          (data[0] & BMD_STATUS_POWERED_ON) != 0,
                          (data[0] & BMD_STATUS_CONNECTED) != 0,
                          ready);
    }
}
