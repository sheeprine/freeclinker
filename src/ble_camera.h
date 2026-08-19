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
#include "dji_protocol.h"
#include "telemetry.h"

using BatteryCallback = void (*)(const BatteryData &);

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
class BLECamera : public BLEAdvertisedDeviceCallbacks,
                  public BLEClientCallbacks {
public:
    void begin();
    void update();   // call from loop()

    void setBatteryCallback(BatteryCallback cb) { _batteryCb = cb; }

    bool isConnected() const { return _djiConnected; }

    bool startRecording();
    bool stopRecording();

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
    void handleRecordAck(const uint8_t *payload, uint16_t len);

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
    esp_ble_addr_type_t       _targetType   = BLE_ADDR_TYPE_PUBLIC;
    bool                      _targetFound  = false;
    bool                      _bleConnected = false;   // BLE layer up
    bool                      _djiConnected = false;   // DJI handshake complete
    bool                      _scanning     = false;
    uint16_t                  _seq          = 0;
    uint32_t                  _lastAttemptMs = 0;

    // Deferred connect ACK: set from the notify callback, executed in update()
    // to avoid calling writeValue() from inside a BLE stack callback.
    bool                      _pendingConnectAck = false;
    uint16_t                  _pendingAckSeq     = 0;

    uint32_t         _deviceId   = 0;      // device_id used in connect request
    BatteryCallback  _batteryCb  = nullptr;
    BatteryData      _battery{};

    static BLECamera *_instance;
};
