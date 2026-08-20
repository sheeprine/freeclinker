#include "ble_camera.h"
#include "config.h"
#include <cstring>

BLECamera *BLECamera::_instance = nullptr;

// ─── Public ──────────────────────────────────────────────────────────────────

void BLECamera::begin() {
    _instance = this;
    BLEDevice::init("ESP32-DJI-Bridge");
    BLEDevice::setPower(ESP_PWR_LVL_P9);
    BLEDevice::setMTU(500);   // sets local preference; per-connection exchange done via _client->setMTU()
    startScan();
}

void BLECamera::update() {
    // Deferred connect ACK: send from the main loop, not from the BLE callback,
    // to avoid calling writeValue() inside a BLE stack context (causes crash).
    if (_pendingConnectAck && _bleConnected && !_djiConnected) {
        _pendingConnectAck = false;

        DJIConnectResponse resp{};
        resp.device_id = 0x00000001;
        resp.ret_code  = 0;

        if (!sendFrame(DJI_CMDSET_GENERAL, DJI_CMD_CONNECT, DJI_ACK,
                       reinterpret_cast<const uint8_t *>(&resp), sizeof(resp),
                       /*with_rsp=*/false, /*override_seq=*/(int32_t)_pendingAckSeq)) {
            DBG_SERIAL.println("[DJI] Failed to send connect ACK");
        } else {
            DBG_SERIAL.println("[DJI] Connection established — subscribing to status");
            if (sendStatusSubscription())
                _djiConnected = true;
        }
        return;
    }

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

    // Request MTU=500 immediately after connecting so DJI frames (up to ~56 bytes)
    // arrive in a single notification rather than truncated to 20 bytes.
    if (_client->setMTU(500)) {
        DBG_SERIAL.printf("[BLE] MTU exchange sent (want 500, got %u)\n", _client->getMTU());
    } else {
        DBG_SERIAL.println("[BLE] MTU exchange failed — staying at 23");
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

    // Write characteristic — ESP32 → camera.
    // Older cameras: 0xFFF5.  DJI Action 5 Pro: 0xFFF5 reports w=0, fall back to 0xFFF3.
    _writeChar = svc->getCharacteristic(BLEUUID((uint16_t)DJI_WRITE_CHAR_UUID));
    if (!_writeChar || !_writeChar->canWrite()) {
        _writeChar = svc->getCharacteristic(BLEUUID((uint16_t)DJI_WRITE_CHAR_UUID_ALT));
    }
    if (!_writeChar || !_writeChar->canWrite()) {
        DBG_SERIAL.println("[BLE] No writable command char found (tried 0xFFF5, 0xFFF3)");
        _client->disconnect();
        _targetFound   = false;
        _lastAttemptMs = millis();
        return false;
    }
    DBG_SERIAL.printf("[BLE] Write char 0x%04X ready\n",
                      _writeChar->getUUID().getNative()->uuid.uuid16);

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
                           const uint8_t *payload, uint16_t len,
                           bool with_rsp, int32_t override_seq) {
    if (!_writeChar) return false;
    uint8_t buf[DJI_MAX_FRAME];
    uint16_t seq = (override_seq >= 0) ? (uint16_t)override_seq : _seq++;
    uint16_t n = dji_build_frame(buf, sizeof(buf), cmd_set, cmd_id, cmd_type,
                                  seq, payload, len);
    if (n == 0) return false;
    _writeChar->writeValue(buf, n, with_rsp);
    return true;
}

// Step 1 of DJI handshake — send controller identity to the camera.
// The camera responds via notify (CmdSet 0x00, CmdID 0x19, DJI_ACK).
bool BLECamera::sendConnectionRequest() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_BT);

    DJIConnectRequest req{};
    // Use the lower 4 bytes of our BT MAC as device_id — stable across reboots,
    // unique per ESP32, no NVS needed.
    _deviceId        = (uint32_t)mac[2] << 24 | (uint32_t)mac[3] << 16 |
                       (uint32_t)mac[4] <<  8 | (uint32_t)mac[5];
    req.device_id    = _deviceId;
    req.mac_addr_len = 6;
    memcpy(req.mac_addr, mac, 6);
    req.fw_version  = 0x00;   // 0 = "no version" per DJI reference; non-zero triggers OTA update prompt
    req.verify_mode = 0;      // 0 = reconnect (camera auto-approves); 1 = new pairing
    req.verify_data = 0;

    bool ok = sendFrame(DJI_CMDSET_GENERAL, DJI_CMD_CONNECT, DJI_CMD,
                        reinterpret_cast<const uint8_t *>(&req), sizeof(req),
                        /*with_rsp=*/true);
    if (ok) DBG_SERIAL.println("[DJI] Connection request sent");
    return ok;
}

bool BLECamera::startRecording() {
    DJIRecordControl ctrl{};
    ctrl.device_id = _deviceId;
    ctrl.action    = DJI_RECORD_START;
    bool ok = sendFrame(DJI_CMDSET_CAMERA, DJI_CMD_RECORD_CTRL, DJI_CMD,
                        reinterpret_cast<const uint8_t *>(&ctrl), sizeof(ctrl),
                        /*with_rsp=*/true);
    if (ok) DBG_SERIAL.println("[DJI] Record start sent");
    return ok;
}

bool BLECamera::stopRecording() {
    DJIRecordControl ctrl{};
    ctrl.device_id = _deviceId;
    ctrl.action    = DJI_RECORD_STOP;
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

// Each BLE notification arrives as ≤20 bytes (ATT MTU 23 = 20 data bytes if no MTU exchange).
// Full DJI frames can be 27–51+ bytes. We dispatch whatever we receive:
//   • If the notification holds the complete frame (len >= frame_len): full CRC parse.
//   • If truncated (len < frame_len): header-only dispatch — enough for the 4-step handshake
//     because the critical fields (CmdSet, CmdID, cmd_type, seq, ret_code) all land within
//     the first 20 bytes of every known frame type.
void BLECamera::handleNotification(uint8_t *data, size_t length) {
    if (length == 0) return;

#if DEBUG_PRINT_SERVICES
    DBG_SERIAL.printf("[BLE] Notify %u bytes:", length);
    for (size_t i = 0; i < length && i < 24; i++)
        DBG_SERIAL.printf(" %02X", data[i]);
    if (length > 24) DBG_SERIAL.print(" …");
    DBG_SERIAL.println();
#endif

    // 0x55-prefixed telemetry stream and anything else that isn't a DJI frame — ignore.
    if (data[0] != DJI_SOF) return;
    if (length < 14) return;   // need at least SOF…CmdID

    uint16_t frame_len = (uint16_t)data[1] | (((uint16_t)data[2] & 0x03) << 8);
    uint8_t  cmd_type  = data[3];
    uint16_t seq       = (uint16_t)data[8] | ((uint16_t)data[9] << 8);
    uint8_t  cmd_set   = data[12];
    uint8_t  cmd_id    = data[13];

    // Complete frame — verify both CRCs before dispatching.
    if (frame_len >= DJI_OVERHEAD && length >= frame_len) {
        const uint8_t *payload;
        uint16_t payload_len;
        if (!dji_parse_frame(data, frame_len, &cmd_type, &cmd_set, &cmd_id,
                              &payload, &payload_len)) {
            DBG_SERIAL.printf("[DJI] CRC error: cs=0x%02X id=0x%02X\n", cmd_set, cmd_id);
            return;
        }
        dispatchFrame(cmd_type, cmd_set, cmd_id, seq, payload, payload_len);
        return;
    }

    // Partial frame (MTU limited): dispatch header + whatever payload bytes arrived.
    // All critical fields for the 4-step handshake are within the first 20 bytes.
    const uint8_t *partial     = (length > 14) ? data + 14 : nullptr;
    uint16_t       partial_len = (length > 14) ? (uint16_t)(length - 14) : 0;
    dispatchFrame(cmd_type, cmd_set, cmd_id, seq, partial, partial_len);
}

void BLECamera::dispatchFrame(uint8_t cmd_type, uint8_t cmd_set, uint8_t cmd_id,
                               uint16_t seq, const uint8_t *payload, uint16_t payload_len) {
    if (cmd_set == DJI_CMDSET_GENERAL && cmd_id == DJI_CMD_CONNECT) {
        if (DJI_IS_ACK(cmd_type))
            handleConnectResponse(payload, payload_len);
        else
            handleConnectCommand(seq, payload, payload_len);
    } else if (cmd_set == DJI_CMDSET_CAMERA && cmd_id == DJI_CMD_STATUS_PUSH) {
        handleCameraStatus(payload, payload_len);
    } else if (cmd_set == DJI_CMDSET_CAMERA && cmd_id == DJI_CMD_NEW_STATUS_PUSH) {
        handleNewCameraStatus(payload, payload_len);
    } else if (cmd_set == DJI_CMDSET_CAMERA && cmd_id == DJI_CMD_RECORD_CTRL
               && DJI_IS_ACK(cmd_type)) {
        handleRecordAck(payload, payload_len);
    } else {
        DBG_SERIAL.printf("[DJI] Unhandled: cs=0x%02X id=0x%02X type=0x%02X\n",
                          cmd_set, cmd_id, cmd_type);
    }
}

// ─── DJI frame handlers ───────────────────────────────────────────────────────

// Step 4 of handshake: camera ACKed our connect request.
// Don't subscribe yet — the camera will next send its own hello (step 5),
// which we ACK (step 6) and THEN subscribe to status (step 7).
void BLECamera::handleConnectResponse(const uint8_t *payload, uint16_t len) {
    // ret_code is at DJIConnectResponse byte 4 (after the 4-byte device_id).
    // We always get at least 6 bytes of payload even with 20-byte MTU notifications.
    if (len < 5) {
        DBG_SERIAL.println("[DJI] ACK payload too short — ignoring");
        return;
    }
    uint8_t ret_code = payload[4];
    if (ret_code != 0) {
        DBG_SERIAL.printf("[DJI] Connection rejected: ret_code=%u\n", ret_code);
        _client->disconnect();
        return;
    }
    DBG_SERIAL.println("[DJI] Connect ACK received — waiting for camera hello");
}

// Step 5 of handshake: camera sends its own connect request (cmd_type 0x02 = CMD).
// We must ACK with the same seq number, then subscribe to status.
// NOTE: called from inside the BLE notify callback — defer the BLE write to update().
void BLECamera::handleConnectCommand(uint16_t camSeq, const uint8_t * /*payload*/,
                                      uint16_t /*len*/) {
    if (_djiConnected || _pendingConnectAck) return;   // already handled
    DBG_SERIAL.printf("[DJI] Camera hello (seq=0x%04X) — queuing ACK\n", camSeq);
    _pendingAckSeq     = camSeq;
    _pendingConnectAck = true;
}

void BLECamera::handleRecordAck(const uint8_t *payload, uint16_t len) {
    uint8_t ret = (len > 0) ? payload[0] : 0xFF;
    if (ret == 0)
        DBG_SERIAL.println("[DJI] Record command OK");
    else
        DBG_SERIAL.printf("[DJI] Record command rejected: ret=0x%02X\n", ret);
}

void BLECamera::handleCameraStatus(const uint8_t *payload, uint16_t len) {
    if (len < sizeof(DJICameraStatus)) return;
    const auto *s = reinterpret_cast<const DJICameraStatus *>(payload);

    _camera.percent      = s->bat_percent;
    _camera.recording    = (s->camera_status == 0x03);
    _camera.camera_mode  = s->camera_mode;
    _camera.eis_mode     = s->eis_mode;
    _camera.temp_over    = s->temp_over;
    _camera.record_time  = s->record_time;
    _camera.remain_cap_mb = s->remain_capacity;
    _camera.remain_time  = s->remain_time;
    _camera.valid        = true;

    if (_cameraCb) _cameraCb(_camera);

    DBG_SERIAL.printf("[DJI] bat=%u%%  mode=0x%02X  rec=%s  eis=%u  "
                      "time=%us  sd=%uMB  remain=%us  temp=%u\n",
                      s->bat_percent, s->camera_mode,
                      _camera.recording ? "yes" : "no",
                      s->eis_mode, s->record_time,
                      s->remain_capacity, s->remain_time, s->temp_over);
}

// 0x1D/0x06: newer cameras (Action 5 Pro, etc.) push mode name + params as ASCII.
// The frame carries two TLV-ish sections at fixed offsets (46 bytes total).
void BLECamera::handleNewCameraStatus(const uint8_t *payload, uint16_t len) {
    if (len < 46) return;

    char mode_name[21] = {};
    char mode_param[21] = {};
    uint8_t name_len  = payload[1];
    uint8_t param_len = payload[24];
    if (name_len  > 20) name_len  = 20;
    if (param_len > 20) param_len = 20;
    memcpy(mode_name,  payload + 2,  name_len);
    memcpy(mode_param, payload + 25, param_len);

    DBG_SERIAL.printf("[DJI] New status: mode=\"%s\"  param=\"%s\"\n",
                      mode_name, mode_param);
}
