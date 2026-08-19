#include "ble_camera.h"
#include "config.h"
#include <cstring>

BLECamera *BLECamera::_instance = nullptr;

// ─── Public ──────────────────────────────────────────────────────────────────

void BLECamera::begin() {
    _instance = this;
    BLEDevice::init("ESP32-DJI-Bridge");
    BLEDevice::setPower(ESP_PWR_LVL_P9);
    BLEDevice::setMTU(500);   // camera status frames are ~56 bytes; default MTU is fine but 500 leaves room
    startScan();
}

void BLECamera::update() {
    if (_djiConnected || _bleConnected || _scanning) return;

    if (_targetFound) {
        connectAndSetup();
        return;
    }

    if (millis() - _lastAttemptMs > BLE_RECONNECT_DELAY_MS)
        startScan();
}

// ─── Scan ────────────────────────────────────────────────────────────────────

void BLECamera::startScan() {
    _targetFound = false;
    _scanning    = true;

    BLEScan *scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(this, /*wantDuplicates=*/false);
    scan->setActiveScan(true);
    scan->setInterval(0x50);   // matches DJI SDK scan params
    scan->setWindow(0x30);

    DBG_SERIAL.println("[BLE] Scanning for DJI Action camera...");
    scan->start(BLE_SCAN_DURATION_SECS, scanDoneCallback, false);
}

void BLECamera::scanDoneCallback(BLEScanResults /*r*/) {
    if (!_instance) return;
    _instance->_scanning = false;
    if (!_instance->_targetFound) {
        DBG_SERIAL.println("[BLE] Scan done — no DJI camera found");
        _instance->_lastAttemptMs = millis();
    }
}

// onResult runs for every advertised device during scan.
// DJI cameras are identified by manufacturer-specific data bytes
// [0]=0xAA [1]=0x08 (manufacturer ID 0x08AA) and [4]=0xFA.
// Device name is used as a fallback if manufacturer data is absent.
void BLECamera::onResult(BLEAdvertisedDevice device) {
    bool isDJI = false;

    if (device.haveManufacturerData()) {
        const std::string mfr = device.getManufacturerData();
        if (mfr.size() >= 5 &&
            (uint8_t)mfr[0] == 0xAA &&
            (uint8_t)mfr[1] == 0x08 &&
            (uint8_t)mfr[4] == 0xFA) {
            isDJI = true;
        }
    }

    if (!isDJI && device.haveName() &&
        device.getName().find(DJI_DEVICE_NAME_PREFIX) != std::string::npos) {
        isDJI = true;
    }

    if (!isDJI) return;

    DBG_SERIAL.printf("[BLE] Found DJI camera: \"%s\"  addr=%s  rssi=%d\n",
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

bool BLECamera::connectAndSetup() {
    DBG_SERIAL.printf("[BLE] Connecting to %s ...\n", _targetAddr.c_str());

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
    DBG_SERIAL.println("[BLE] BLE connected");

#if DEBUG_PRINT_SERVICES
    printServices();
#endif

    // Locate the DJI service
    BLERemoteService *svc = _client->getService(BLEUUID((uint16_t)DJI_SERVICE_UUID));
    if (!svc) {
        DBG_SERIAL.println("[BLE] DJI service 0xFFF0 not found");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }

    // Notify characteristic (0xFFF4) — camera → ESP32
    BLERemoteCharacteristic *notifyCh =
        svc->getCharacteristic(BLEUUID((uint16_t)DJI_NOTIFY_CHAR_UUID));
    if (!notifyCh || !notifyCh->canNotify()) {
        DBG_SERIAL.println("[BLE] Notify char 0xFFF4 not found or not notifiable");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }
    notifyCh->registerForNotify(notifyCallback);
    DBG_SERIAL.println("[BLE] Subscribed to 0xFFF4 notify");

    // Write characteristic (0xFFF5) — ESP32 → camera
    _writeChar = svc->getCharacteristic(BLEUUID((uint16_t)DJI_WRITE_CHAR_UUID));
    if (!_writeChar || !_writeChar->canWrite()) {
        DBG_SERIAL.println("[BLE] Write char 0xFFF5 not found or not writable");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }
    DBG_SERIAL.println("[BLE] Write char 0xFFF5 ready");

    _bleConnected = true;

    // Kick off the DJI handshake; response arrives via notify callback
    sendConnectionRequest();
    return true;
}

void BLECamera::onConnect(BLEClient * /*c*/) {
    DBG_SERIAL.println("[BLE] onConnect");
}

void BLECamera::onDisconnect(BLEClient * /*c*/) {
    DBG_SERIAL.println("[BLE] Disconnected — will rescan");
    _djiConnected  = false;
    _bleConnected  = false;
    _writeChar     = nullptr;
    _targetFound   = false;
    _lastAttemptMs = millis();
}

// ─── Service listing (debug) ──────────────────────────────────────────────────

void BLECamera::printServices() {
    DBG_SERIAL.println("[BLE] ── Services ─────────────────────────────");
    auto *svcs = _client->getServices();
    if (!svcs) { DBG_SERIAL.println("[BLE] (none)"); return; }
    for (auto &[u, svc] : *svcs) {
        DBG_SERIAL.printf("[BLE] Svc  %s\n", svc->getUUID().toString().c_str());
        auto *chs = svc->getCharacteristics();
        if (!chs) continue;
        for (auto &[cu, ch] : *chs)
            DBG_SERIAL.printf("[BLE]   Chr %s  n=%d r=%d w=%d\n",
                              ch->getUUID().toString().c_str(),
                              ch->canNotify(), ch->canRead(), ch->canWrite());
    }
    DBG_SERIAL.println("[BLE] ──────────────────────────────────────────");
}

// ─── DJI frame send ───────────────────────────────────────────────────────────

bool BLECamera::sendFrame(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type,
                           const uint8_t *payload, uint16_t len, bool with_rsp) {
    if (!_writeChar) return false;
    uint8_t buf[DJI_MAX_FRAME];
    uint16_t n = dji_build_frame(buf, sizeof(buf), cmd_set, cmd_id, cmd_type,
                                  _seq++, payload, len);
    if (n == 0) return false;
    _writeChar->writeValue(buf, n, with_rsp);
    return true;
}

// Step 1 of DJI handshake — send controller identity to the camera.
// The camera responds via notify (CmdSet 0x00, CmdID 0x19, DJI_ACK).
bool BLECamera::sendConnectionRequest() {
    DJIConnectRequest req{};
    req.device_id    = 0x00000001;
    req.mac_addr_len = 6;
    const uint8_t *mac = esp_bt_dev_get_address();
    if (mac) memcpy(req.mac_addr, mac, 6);
    req.fw_version  = 0x01000000;
    req.verify_mode = 0;
    req.verify_data = 0;

    bool ok = sendFrame(DJI_CMDSET_GENERAL, DJI_CMD_CONNECT, DJI_CMD,
                        reinterpret_cast<const uint8_t *>(&req), sizeof(req),
                        /*with_rsp=*/true);
    if (ok) DBG_SERIAL.println("[DJI] Connection request sent");
    return ok;
}

bool BLECamera::startRecording() {
    DJIRecordControl ctrl{ DJI_RECORD_START };
    bool ok = sendFrame(DJI_CMDSET_CAMERA, DJI_CMD_RECORD_CTRL, DJI_CMD,
                        reinterpret_cast<const uint8_t *>(&ctrl), sizeof(ctrl),
                        /*with_rsp=*/true);
    if (ok) DBG_SERIAL.println("[DJI] Record start sent");
    return ok;
}

bool BLECamera::stopRecording() {
    DJIRecordControl ctrl{ DJI_RECORD_STOP };
    bool ok = sendFrame(DJI_CMDSET_CAMERA, DJI_CMD_RECORD_CTRL, DJI_CMD,
                        reinterpret_cast<const uint8_t *>(&ctrl), sizeof(ctrl),
                        /*with_rsp=*/true);
    if (ok) DBG_SERIAL.println("[DJI] Record stop sent");
    return ok;
}

// Step 2 — subscribe to 2 Hz camera status push (battery, mode, temps, …).
bool BLECamera::sendStatusSubscription() {
    DJIStatusSubscription sub{};
    sub.push_mode = DJI_PUSH_PERIODIC_ON_CHANGE;
    sub.push_freq = 20;   // 20 × 0.1 Hz = 2 Hz (only value the camera accepts)

    bool ok = sendFrame(DJI_CMDSET_CAMERA, DJI_CMD_STATUS_SUB, DJI_CMD,
                        reinterpret_cast<const uint8_t *>(&sub), sizeof(sub));
    if (ok) DBG_SERIAL.println("[DJI] Status subscription sent (2 Hz)");
    return ok;
}

// ─── Incoming notify ─────────────────────────────────────────────────────────

void BLECamera::notifyCallback(BLERemoteCharacteristic * /*ch*/,
                                uint8_t *data, size_t len, bool /*isNotify*/) {
    if (_instance) _instance->handleNotification(data, static_cast<size_t>(len));
}

void BLECamera::handleNotification(uint8_t *data, size_t length) {
#if DEBUG_PRINT_SERVICES
    DBG_SERIAL.printf("[BLE] Notify %u bytes:", length);
    for (size_t i = 0; i < length && i < 24; i++)
        DBG_SERIAL.printf(" %02X", data[i]);
    if (length > 24) DBG_SERIAL.print(" …");
    DBG_SERIAL.println();
#endif

    uint8_t cmd_type, cmd_set, cmd_id;
    const uint8_t *payload;
    uint16_t payload_len;

    if (!dji_parse_frame(data, (uint16_t)length,
                          &cmd_type, &cmd_set, &cmd_id,
                          &payload, &payload_len)) {
        DBG_SERIAL.println("[DJI] Frame parse failed (bad CRC or length)");
        return;
    }

    dispatchFrame(cmd_type, cmd_set, cmd_id, payload, payload_len);
}

void BLECamera::dispatchFrame(uint8_t cmd_type, uint8_t cmd_set, uint8_t cmd_id,
                               const uint8_t *payload, uint16_t payload_len) {
    if (cmd_set == DJI_CMDSET_GENERAL && cmd_id == DJI_CMD_CONNECT && DJI_IS_ACK(cmd_type))
        handleConnectResponse(payload, payload_len);
    else if (cmd_set == DJI_CMDSET_CAMERA && cmd_id == DJI_CMD_STATUS_PUSH)
        handleCameraStatus(payload, payload_len);
    else
        DBG_SERIAL.printf("[DJI] Unhandled frame: cmdset=0x%02X cmdid=0x%02X type=0x%02X\n",
                          cmd_set, cmd_id, cmd_type);
}

// ─── DJI frame handlers ───────────────────────────────────────────────────────

void BLECamera::handleConnectResponse(const uint8_t *payload, uint16_t len) {
    if (len < sizeof(DJIConnectResponse)) {
        DBG_SERIAL.println("[DJI] Connect response too short");
        return;
    }
    const auto *resp = reinterpret_cast<const DJIConnectResponse *>(payload);
    if (resp->ret_code != 0) {
        DBG_SERIAL.printf("[DJI] Connection rejected, ret_code=%u\n", resp->ret_code);
        _client->disconnect();
        return;
    }
    DBG_SERIAL.println("[DJI] Connection accepted");
    if (sendStatusSubscription())
        _djiConnected = true;
}

void BLECamera::handleCameraStatus(const uint8_t *payload, uint16_t len) {
    if (len < sizeof(DJICameraStatus)) return;
    const auto *status = reinterpret_cast<const DJICameraStatus *>(payload);

    _battery.percent     = status->bat_percent;
    _battery.voltage     = 0;   // not provided by this BLE interface
    _battery.current     = 0;
    _battery.remaining   = 0;
    _battery.capacity    = 0;
    _battery.temperature = INT8_MIN;  // unknown
    _battery.cellCount   = 0;
    _battery.valid       = true;

    if (_batteryCb) _batteryCb(_battery);

    DBG_SERIAL.printf("[DJI] Status: bat=%u%%  mode=0x%02X  status=0x%02X  "
                      "rec=%us  sd=%uMB  temp=%u\n",
                      status->bat_percent, status->camera_mode,
                      status->camera_status, status->record_time,
                      status->remain_capacity, status->temp_over);
}
