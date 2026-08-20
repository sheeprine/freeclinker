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
// 5. Send register-for-status-updates query (GP-0076) for battery, encoding, etc.
// 6. Receive status change notifications (GP-0077) at 2 Hz or on change
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

    // ── GoPro commands ────────────────────────────────────────────────────────
    void sendCmd(uint8_t cmd_id, const uint8_t *params, uint8_t param_len);
    void sendSetting(uint8_t setting_id, uint8_t value);
    void sendHardwareInfoQuery();
    void sendRegisterQuery();

    // ── Notification handling ─────────────────────────────────────────────────
    void handleCmdNotification(uint8_t *data, size_t len);
    void handleSettingNotification(uint8_t *data, size_t len);
    void handleQueryNotification(uint8_t *data, size_t len);
    void handleCmdMessage(const uint8_t *msg, size_t len);
    void handleSettingMessage(const uint8_t *msg, size_t len);
    void handleQueryMessage(const uint8_t *msg, size_t len);
    void parseStatusTlv(const uint8_t *tlv, size_t len);

    // ── Static trampolines ────────────────────────────────────────────────────
    static void cmdNotifyCallback(BLERemoteCharacteristic *pChar,
                                   uint8_t *data, size_t len, bool isNotify);
    static void settingNotifyCallback(BLERemoteCharacteristic *pChar,
                                       uint8_t *data, size_t len, bool isNotify);
    static void queryNotifyCallback(BLERemoteCharacteristic *pChar,
                                     uint8_t *data, size_t len, bool isNotify);
    static void scanDoneCallback(BLEScanResults results);

    // ── State ─────────────────────────────────────────────────────────────────
    BLEClient               *_client       = nullptr;
    BLERemoteCharacteristic *_cmdChar     = nullptr;  // GP-0072: command write
    BLERemoteCharacteristic *_settingChar = nullptr;  // GP-0074: setting write
    BLERemoteCharacteristic *_queryChar   = nullptr;  // GP-0076: query write
    std::string              _targetAddr;
    esp_ble_addr_type_t      _targetType  = BLE_ADDR_TYPE_PUBLIC;
    bool                     _targetFound = false;
    bool                     _bleConnected = false;
    bool                     _gpConnected  = false;
    bool                     _scanning     = false;
    uint32_t                 _lastAttemptMs = 0;

    // Deferred actions (set from notify callbacks, executed in update())
    bool _pendingHwInfo  = false;  // retry Get Hardware Info
    bool _pendingRegister = false; // send register query after hw info OK

    // Packet reassemblers for each notify characteristic
    GpRxAssembler _cmdRx;
    GpRxAssembler _settingRx;
    GpRxAssembler _queryRx;

    CameraData _camera{};

    static GoProCamera *_instance;
};
