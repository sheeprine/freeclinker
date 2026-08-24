#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLERemoteService.h>
#include <BLERemoteCharacteristic.h>
#include <BLEAdvertisedDevice.h>
#include <BLESecurity.h>
#include <esp_gap_ble_api.h>
#include "camera.h"
#include "sony_protocol.h"

// BLE client for Sony Alpha cameras (Sony's undocumented BLE remote-control
// protocol — button-press simulation, not a telemetry API; see
// sony_protocol.h and freemote (https://github.com/coral/freemote)).
//
// Connection sequence
// ────────────────────
// 1. Scan — identify Sony cameras by manufacturer ID 0x012D / device type 0x0300
// 2. Connect; request an encrypted, bonded link (camera rejects writes on an
//    unbonded link — first-time pairing requires the camera's Bluetooth Rmt
//    Ctrl menu to be open)
// 3. Once secured: discover service 8000FF00, subscribe to 0xFF02 notify,
//    locate the 0xFF01 write characteristic; best-effort discover service
//    8000CC00 and subscribe to 0xCC10 (battery) — optional, some models
//    reduce this service while remote-button control is active
// 4. Camera pushes focus/shutter/recording state changes on 0xFF02 and
//    battery level changes on 0xCC10
class SonyCamera : public Camera,
                    public BLEAdvertisedDeviceCallbacks,
                    public BLEClientCallbacks,
                    public BLESecurityCallbacks {
public:
    void begin();
    void update();

    bool isConnected() const override { return _sonyConnected; }

    bool startRecording() override;
    bool stopRecording() override;
    // Not supported — the Sony remote-button protocol has no mode-switch command.
    bool switchCameraMode(uint8_t mode) override;

private:
    // BLE stack callbacks
    void onResult(BLEAdvertisedDevice device) override;
    void onConnect(BLEClient *client) override;
    void onDisconnect(BLEClient *client) override;

    // BLESecurityCallbacks — bonding/pairing (Just Works, no PIN either side)
    uint32_t onPassKeyRequest() override;
    void     onPassKeyNotify(uint32_t pass_key) override;
    bool     onSecurityRequest() override;
    bool     onConfirmPIN(uint32_t pin) override;
    void     onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override;

    // ── BLE session ───────────────────────────────────────────────────────────
    void startScan();
    bool connectAndSetup();            // connect + request encryption
    bool discoverServiceAndSubscribe(); // runs once the link is secured
    void discoverBatteryService();      // best-effort; logs and continues on failure

    // ── Sony commands ─────────────────────────────────────────────────────────
    void sendCommandByte(uint8_t cmd);
    void sendButtonPress(uint8_t downCmd, uint8_t upCmd);

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
    BLEClient                *_client       = nullptr;
    BLERemoteCharacteristic  *_cmdChar      = nullptr;  // 0xFF01: command write
    std::string               _targetAddr;
    std::string               _targetName;
    esp_ble_addr_type_t       _targetType   = BLE_ADDR_TYPE_PUBLIC;
    bool                      _targetFound  = false;
    bool                      _bleConnected = false;   // BLE link up (pre-security)
    bool                      _secured      = false;   // link encrypted + bonded
    bool                      _sonyConnected = false;  // service discovered, ready for commands
    bool                      _scanning     = false;
    uint32_t                  _lastAttemptMs = 0;

    // First Sony camera seen during scan — fallback when preferred addr isn't found
    std::string               _candidateAddr;
    std::string               _candidateName;
    esp_ble_addr_type_t       _candidateType = BLE_ADDR_TYPE_PUBLIC;

    // Deferred: set once onAuthenticationComplete() reports success, executed
    // from update() since GATT discovery shouldn't run inside a GAP callback.
    bool                      _pendingDiscover = false;

    CameraData      _camera{};

    static SonyCamera *_instance;
};
