#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLERemoteService.h>
#include <BLERemoteCharacteristic.h>
#include <BLEAdvertisedDevice.h>
#include "camera.h"
#include "gopro_protocol.h"

// BLE client for GoPro cameras (Open GoPro BLE API).
//
// Connection sequence
// ───────────────────
// 1. Scan — identify GoPro by service UUID 0xFEA6 or manufacturer ID 0xF202
// 2. Connect; discover service FEA6; subscribe to GP-0073 and GP-0077 notify chars
// 3. Send Get Hardware Info (GP-0072) — polls until camera BLE stack is ready
// 4. Receive hardware info response (GP-0073, cmd_id=0x3C, status=0) → ready
// 5. Send register-for-setting-updates query (GP-0076, 0x52) for resolution/fps/EIS
//    — rejected on cameras that predate a queried setting (e.g. HyperSmooth is
//    HERO7+); a rejection just skips extended telemetry, it isn't fatal
// 6. On 0x52 ack (or rejection), send register-for-status-updates query
//    (GP-0076, 0x53) requesting the union of modern and legacy status IDs —
//    real HERO4/5-Session hardware ACKs the modern IDs (encoding=10, preset
//    group=96) but never actually pushes values for them, so recording/mode
//    telemetry is only reachable via the legacy IDs (recording=8, mode=43;
//    see gopro_protocol.h) requested alongside them. Camera is considered
//    ready once 0x53 succeeds. If the whole batch is rejected outright,
//    retries once with the legacy-only ID set; if that's rejected too, the
//    camera is still marked ready so record/mode commands work, just
//    without status telemetry.
// 7. Receive pushed setting/status updates (GP-0077, 0x92/0x93) at 2 Hz or on change
//
// Separately, best-effort discovers the standard Battery Service (0x180F)
// and subscribes to Battery Level (0x2A19) — HERO4/5-Session-era cameras
// report battery there instead of via status ID 70 on the FEA6 channel. A
// missing service/characteristic just means no battery telemetry, not a
// connection failure.
class GoProCamera : public Camera,
                    public BLEAdvertisedDeviceCallbacks,
                    public BLEClientCallbacks {
public:
    void begin();
    void update();

    bool isConnected() const override { return _gpConnected; }

    bool startRecording() override;
    bool stopRecording() override;
    // mode is a DJI_MODE_* constant; GoProCamera maps it to a GoPro preset group.
    bool switchCameraMode(uint8_t mode) override;
    bool triggerBurstSloMo() override;
    bool exitBurstSloMo() override;

private:
    // BLE stack callbacks
    void onResult(BLEAdvertisedDevice device) override;
    void onConnect(BLEClient *client) override;
    void onDisconnect(BLEClient *client) override;

    // ── BLE session ───────────────────────────────────────────────────────────
    void startScan();
    bool connectAndSetup();
    void discoverBatteryService();

    // ── GoPro commands ────────────────────────────────────────────────────────
    void sendCmd(uint8_t cmd_id, const uint8_t *params, uint8_t param_len);
    void sendSetting(uint8_t setting_id, uint8_t value);
    void sendHardwareInfoQuery();
    void sendRegisterSettings();
    void sendRegisterStatus();

    // ── Notification handling ─────────────────────────────────────────────────
    void handleCmdNotification(uint8_t *data, size_t len);
    void handleSettingNotification(uint8_t *data, size_t len);
    void handleQueryNotification(uint8_t *data, size_t len);
    void handleCmdMessage(const uint8_t *msg, size_t len);
    void handleSettingMessage(const uint8_t *msg, size_t len);
    void handleQueryMessage(const uint8_t *msg, size_t len);
    void parseStatusTlv(const uint8_t *tlv, size_t len);
    void handleBatteryNotification(uint8_t *data, size_t len);

    // ── Static trampolines ────────────────────────────────────────────────────
    static void cmdNotifyCallback(BLERemoteCharacteristic *pChar,
                                   uint8_t *data, size_t len, bool isNotify);
    static void settingNotifyCallback(BLERemoteCharacteristic *pChar,
                                       uint8_t *data, size_t len, bool isNotify);
    static void queryNotifyCallback(BLERemoteCharacteristic *pChar,
                                     uint8_t *data, size_t len, bool isNotify);
    static void batteryNotifyCallback(BLERemoteCharacteristic *pChar,
                                       uint8_t *data, size_t len, bool isNotify);
    static void scanDoneCallback(BLEScanResults results);

    // ── State ─────────────────────────────────────────────────────────────────
    BLEClient               *_client       = nullptr;
    BLERemoteCharacteristic *_cmdChar     = nullptr;  // GP-0072: command write
    BLERemoteCharacteristic *_settingChar = nullptr;  // GP-0074: setting write
    BLERemoteCharacteristic *_queryChar   = nullptr;  // GP-0076: query write
    std::string              _targetAddr;
    std::string              _targetName;
    esp_ble_addr_type_t      _targetType  = BLE_ADDR_TYPE_PUBLIC;
    bool                     _targetFound = false;
    bool                     _bleConnected = false;
    bool                     _gpConnected  = false;
    bool                     _scanning     = false;
    bool                     _legacyProtocol = false;  // HERO4/5-Session-era status IDs/mode command
    uint32_t                 _lastAttemptMs = 0;

    // First GoPro seen during scan — fallback when preferred addr isn't found
    std::string              _candidateAddr;
    std::string              _candidateName;
    esp_ble_addr_type_t      _candidateType = BLE_ADDR_TYPE_PUBLIC;

    // CAM_MATCH_BEST_SIGNAL: strongest-RSSI camera seen so far this scan
    std::string              _bestAddr;
    std::string              _bestName;
    esp_ble_addr_type_t      _bestType = BLE_ADDR_TYPE_PUBLIC;
    int8_t                   _bestRssi = -128;

    // Deferred actions (set from notify callbacks, executed in update())
    bool _pendingHwInfo         = false;  // retry Get Hardware Info
    bool _pendingRegisterSettings = false; // send setting registration after hw info OK
    bool _pendingRegisterStatus   = false; // send status registration after setting reg OK

    // Packet reassemblers for each notify characteristic
    GpRxAssembler _cmdRx;
    GpRxAssembler _settingRx;
    GpRxAssembler _queryRx;

    CameraData _camera{};

    static GoProCamera *_instance;
};
