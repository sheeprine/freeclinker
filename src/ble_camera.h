#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLERemoteService.h>
#include <BLERemoteCharacteristic.h>
#include <BLEAdvertisedDevice.h>
#include <esp_gap_ble_api.h>
#include <esp_mac.h>            // esp_read_mac()
#include "camera.h"
#include "dji_protocol.h"

// BLE client for DJI Action cameras.
//
// 4-step connection sequence (DJI BLE protocol)
// ──────────────────────────────────────────────
// 1. Scan — identify DJI cameras by manufacturer data (0xAA 0x08 … 0xFA)
// 2. Connect; subscribe to 0xFFF4 notify; locate write char (0xFFF5, or 0xFFF3 on Action 5 Pro)
// 3. ESP32 → camera: connect request   (CmdSet 0x00 / CmdID 0x19 / CMD)
// 4. Camera → ESP32: ACK               (CmdSet 0x00 / CmdID 0x19 / ACK, ret_code 0 = OK)
// 5. Camera → ESP32: its own hello     (CmdSet 0x00 / CmdID 0x19 / CMD, cmd_type 0x02)
// 6. ESP32 → camera: ACK to camera hello (same seq as step 5)
// 7. ESP32 → camera: status subscription (CmdSet 0x1D / CmdID 0x05, 2 Hz)
// 8. Camera → ESP32: 2 Hz status push  (CmdSet 0x1D / CmdID 0x02) — battery % + mode
class BLECamera : public Camera,
                  public BLEAdvertisedDeviceCallbacks,
                  public BLEClientCallbacks {
public:
    void begin() override;
    void update() override;

    bool isConnected() const override { return _djiConnected; }

    bool startRecording() override;
    bool stopRecording() override;
    bool switchCameraMode(uint8_t mode) override;  // DJI_MODE_* constants

private:
    // BLE stack callbacks
    void onResult(BLEAdvertisedDevice device) override;
    void onConnect(BLEClient *client) override;
    void onDisconnect(BLEClient *client) override;

    // ── BLE session ───────────────────────────────────────────────────────
    void startScan();
    bool connectAndSetup();         // BLE connect + char discovery + notify sub
    bool sendConnectionRequest();   // DJI handshake step 1
    bool sendStatusSubscription();  // DJI handshake step 2

    // ── DJI frame I/O ─────────────────────────────────────────────────────
    // override_seq >= 0 forces a specific seq number (for ACKing camera frames).
    bool sendFrame(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type,
                   const uint8_t *payload, uint16_t len,
                   bool with_rsp = false, int32_t override_seq = -1);

    void handleNotification(uint8_t *data, size_t length);
    void dispatchFrame(uint8_t cmd_type, uint8_t cmd_set, uint8_t cmd_id,
                       uint16_t seq, const uint8_t *payload, uint16_t payload_len);

    void handleConnectResponse(const uint8_t *payload, uint16_t len);
    void handleConnectCommand(uint16_t camSeq, const uint8_t *payload, uint16_t len);
    void handleCameraStatus(const uint8_t *payload, uint16_t len);
    void handleNewCameraStatus(const uint8_t *payload, uint16_t len);
    void handleRecordAck(const uint8_t *payload, uint16_t len);
    void handleModeSwitchAck(const uint8_t *payload, uint16_t len);

    // ── Debug ─────────────────────────────────────────────────────────────
    void printServices();

    // ── Static trampolines ────────────────────────────────────────────────
    static void notifyCallback(BLERemoteCharacteristic *pChar,
                                uint8_t *pData, size_t length, bool isNotify);
    static void scanDoneCallback(BLEScanResults results);

    // ── State ─────────────────────────────────────────────────────────────
    BLEClient                *_client       = nullptr;
    BLERemoteCharacteristic  *_writeChar    = nullptr;
    std::string               _targetAddr;
    std::string               _targetName;
    esp_ble_addr_type_t       _targetType   = BLE_ADDR_TYPE_PUBLIC;
    bool                      _targetFound  = false;
    bool                      _bleConnected = false;   // BLE layer up
    bool                      _djiConnected = false;   // DJI handshake complete
    bool                      _scanning     = false;
    uint16_t                  _seq          = 0;
    uint32_t                  _lastAttemptMs = 0;

    // First DJI camera seen during scan — fallback when preferred addr isn't found
    std::string               _candidateAddr;
    std::string               _candidateName;
    esp_ble_addr_type_t       _candidateType = BLE_ADDR_TYPE_PUBLIC;

    // CAM_MATCH_BEST_SIGNAL: strongest-RSSI camera seen so far this scan
    std::string               _bestAddr;
    std::string               _bestName;
    esp_ble_addr_type_t       _bestType = BLE_ADDR_TYPE_PUBLIC;
    int8_t                    _bestRssi = -128;

    // Deferred connect ACK: set from the notify callback, executed in update()
    // to avoid calling writeValue() from inside a BLE stack callback.
    bool                      _pendingConnectAck = false;
    uint16_t                  _pendingAckSeq     = 0;

    uint32_t        _deviceId  = 0;      // device_id used in connect request
    CameraData      _camera{};

    static BLECamera *_instance;
};
