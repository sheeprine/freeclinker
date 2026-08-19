#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLERemoteService.h>
#include <BLERemoteCharacteristic.h>
#include <BLEAdvertisedDevice.h>
#include <esp_gap_ble_api.h>
#include <esp_bt.h>             // esp_bt_dev_get_address()
#include "dji_protocol.h"
#include "telemetry.h"

using BatteryCallback = void (*)(const BatteryData &);

// BLE client for DJI Action cameras.
//
// Connection sequence
// ───────────────────
// 1. Scan — identify DJI cameras by manufacturer data (0xAA 0x08 … 0xFA)
// 2. Connect to strongest-signal device
// 3. Locate service 0xFFF0; subscribe to notify char 0xFFF4; store write char 0xFFF5
// 4. Send connection handshake (CmdSet 0x00, CmdID 0x19)
// 5. On ACK: subscribe to camera status push (CmdSet 0x1D, CmdID 0x05, 2 Hz)
// 6. Parse incoming status frames (CmdSet 0x1D, CmdID 0x02) for battery %
//
// Note: the camera does NOT push its own GPS over this BLE interface — the
// SDK's GPS command (0x00/0x17) flows ESP32 → camera, not the other way round.
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
    bool sendFrame(uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type,
                   const uint8_t *payload, uint16_t len, bool with_rsp = false);

    void handleNotification(uint8_t *data, size_t length);
    void dispatchFrame(uint8_t cmd_type, uint8_t cmd_set, uint8_t cmd_id,
                       const uint8_t *payload, uint16_t payload_len);

    void handleConnectResponse(const uint8_t *payload, uint16_t len);
    void handleCameraStatus(const uint8_t *payload, uint16_t len);

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

    BatteryCallback  _batteryCb  = nullptr;
    BatteryData      _battery{};

    static BLECamera *_instance;
};
