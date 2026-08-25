#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLERemoteService.h>
#include <BLERemoteCharacteristic.h>
#include <BLEAdvertisedDevice.h>
#include "camera.h"
#include "insta360_protocol.h"

// BLE client for Insta360 action cameras (X3, X4, ONE R/RS, GO 2/3, etc.),
// speaking the same simple BLE command channel the official phone app uses
// (service 0xBE80) — see insta360_protocol.h for the wire format and sources.
//
// Connection sequence
// ────────────────────
// 1. Scan — identify candidates by the advertised primary service UUID
//    0xBE80 where present, OR (best-effort fallback, since no vendor scan
//    filter for this family is documented/verified anywhere we could check)
//    an advertised local name that looks like an Insta360 camera. Neither
//    signal is guaranteed for every model/firmware; once a camera has
//    connected once, the registry's saved address takes over for reconnects
//    regardless of what its name looks like.
// 2. Connect; no bonding/encryption is required by this protocol (unlike
//    Sony/Blackmagic) — commands and notifications are plaintext.
// 3. Negotiate a larger ATT MTU so command responses (which can run past 20
//    bytes) arrive in a single notification rather than needing manual
//    reassembly, then subscribe to 0xBE82 (notify) and locate 0xBE81
//    (write); best-effort discover the standard Bluetooth SIG Battery
//    Service (0x180F) / Battery Level (0x2A19).
// 4. Recording start/stop send a Phone Command (start/stop video) and wait
//    for the camera's 200-OK ack before updating CameraData.recording — the
//    response's protobuf body (actual camera-pushed recording/battery/mode
//    state) has no available schema to decode, so nothing else is parsed
//    from it. switchCameraMode is unsupported: the protocol only exposes
//    separate "start recording in mode X" commands (HDR/bullet/timeshift/
//    loop), not a stateless mode-switch, and only the "start video normal"
//    variant is verified here.
class Insta360Camera : public Camera,
                        public BLEAdvertisedDeviceCallbacks,
                        public BLEClientCallbacks {
public:
    void begin();
    void update();

    bool isConnected() const override { return _instaConnected; }

    bool startRecording() override;
    bool stopRecording() override;
    // Not supported — see class comment above.
    bool switchCameraMode(uint8_t mode) override;

private:
    // BLE stack callbacks
    void onResult(BLEAdvertisedDevice device) override;
    void onConnect(BLEClient *client) override;
    void onDisconnect(BLEClient *client) override;

    // ── BLE session ───────────────────────────────────────────────────────────
    void startScan();
    bool connectAndSetup();
    void discoverBatteryService();  // best-effort; logs and continues on failure
    bool sendCommand(uint16_t cmd);

    // ── Notification handling ─────────────────────────────────────────────────
    void handleNotification(uint8_t *data, size_t len);
    void handleBatteryNotification(uint8_t *data, size_t len);

    // ── Static trampolines ────────────────────────────────────────────────────
    static void notifyCallback(BLERemoteCharacteristic *pChar,
                                uint8_t *data, size_t len, bool isNotify);
    static void batteryNotifyCallback(BLERemoteCharacteristic *pChar,
                                       uint8_t *data, size_t len, bool isNotify);
    static void scanDoneCallback(BLEScanResults results);

    // ── State ─────────────────────────────────────────────────────────────────
    BLEClient               *_client    = nullptr;
    BLERemoteCharacteristic *_writeChar = nullptr;  // 0xBE81: command write

    std::string          _targetAddr;
    std::string          _targetName;
    esp_ble_addr_type_t  _targetType     = BLE_ADDR_TYPE_PUBLIC;
    bool                 _targetFound    = false;
    bool                 _bleConnected   = false;  // BLE link up, not yet ready
    bool                 _instaConnected = false;  // service discovered, ready for commands
    bool                 _scanning       = false;
    uint32_t             _lastAttemptMs  = 0;
    uint32_t             _seq            = INSTA_SEQ_START;

    // First Insta360 camera seen during scan — fallback when preferred addr isn't found
    std::string          _candidateAddr;
    std::string          _candidateName;
    esp_ble_addr_type_t  _candidateType = BLE_ADDR_TYPE_PUBLIC;

    // CAM_MATCH_BEST_SIGNAL: strongest-RSSI camera seen so far this scan
    std::string          _bestAddr;
    std::string          _bestName;
    esp_ble_addr_type_t  _bestType = BLE_ADDR_TYPE_PUBLIC;
    int8_t               _bestRssi = -128;

    // Set right after sending a start/stop command; cleared once the 200-OK
    // ack (or a failure) resolves it. Only one recording command is ever
    // in flight at a time, so no correlation beyond this flag is needed.
    bool _awaitingRecordAck   = false;
    bool _pendingRecordTarget = false;

    CameraData _camera{};

    static Insta360Camera *_instance;
};
