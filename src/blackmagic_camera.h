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
#include "blackmagic_protocol.h"

// BLE client for Blackmagic Design cameras (Pocket Cinema Camera, URSA,
// Studio range) speaking the Blackmagic Camera Control Protocol over BLE —
// see blackmagic_protocol.h for the wire format and sources.
//
// Connection sequence
// ────────────────────
// 1. Scan — identify cameras by the advertised Blackmagic Camera Service
//    UUID (291d567a-...)
// 2. Connect; read Protocol Version and write our Device Name (both
//    plaintext, no bonding needed)
// 3. Subscribe to Incoming Camera Control and Camera Status (both marked
//    "encrypted" in the protocol doc) — the first read/write/subscribe on an
//    encrypted characteristic auto-triggers bonding. The camera displays a
//    6-digit PIN on its own screen and expects it typed back in; since the
//    ESP32 has no display/keypad of its own, onPassKeyRequest() blocks
//    reading digits from the serial console — same approach used by the two
//    known-working community libraries for this protocol (schoolpost/
//    BlueMagic32, marklysze/Magic-Pocket-Control-ESP32). This only happens
//    once per bond, not on every reconnect.
// 4. Camera pushes Transport Mode changes (recording state) on Incoming
//    Camera Control, and power/ready flags on Camera Status.
//
// No battery, resolution, fps, or EIS telemetry is exposed over this
// protocol — CameraData stays at defaults for those fields.
class BlackmagicCamera : public Camera,
                          public BLEAdvertisedDeviceCallbacks,
                          public BLEClientCallbacks,
                          public BLESecurityCallbacks {
public:
    void begin();
    void update();

    bool isConnected() const override { return _bmdConnected; }

    bool startRecording() override;
    bool stopRecording() override;
    // Not supported — the protocol has no photo/video mode-switch command
    // for this camera family (they're always in "cinema camera" mode).
    bool switchCameraMode(uint8_t mode) override;

private:
    // BLE stack callbacks
    void onResult(BLEAdvertisedDevice device) override;
    void onConnect(BLEClient *client) override;
    void onDisconnect(BLEClient *client) override;

    // BLESecurityCallbacks — Passkey Entry pairing (camera displays the PIN)
    uint32_t onPassKeyRequest() override;
    void     onPassKeyNotify(uint32_t pass_key) override;
    bool     onSecurityRequest() override;
    bool     onConfirmPIN(uint32_t pin) override;
    void     onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override;

    // ── BLE session ───────────────────────────────────────────────────────────
    void startScan();
    bool connectAndSetup();
    void sendTransportMode(uint8_t mode);

    // ── Notification handling ─────────────────────────────────────────────────
    void handleControlNotification(uint8_t *data, size_t len);
    void handleStatusNotification(uint8_t *data, size_t len);

    // ── Static trampolines ────────────────────────────────────────────────────
    static void controlNotifyCallback(BLERemoteCharacteristic *pChar,
                                       uint8_t *data, size_t len, bool isNotify);
    static void statusNotifyCallback(BLERemoteCharacteristic *pChar,
                                      uint8_t *data, size_t len, bool isNotify);
    static void scanDoneCallback(BLEScanResults results);

    // ── State ─────────────────────────────────────────────────────────────────
    BLEClient               *_client       = nullptr;
    BLERemoteCharacteristic *_outgoingChar = nullptr;  // Outgoing Camera Control (write)

    std::string          _targetAddr;
    std::string          _targetName;
    esp_ble_addr_type_t  _targetType   = BLE_ADDR_TYPE_PUBLIC;
    bool                 _targetFound  = false;
    bool                 _bmdConnected = false;  // service discovered, subscribed, ready for commands
    bool                 _scanning     = false;
    bool                 _cameraReady  = false;  // Camera Status bit 0x20 — informational only
    uint32_t             _lastAttemptMs = 0;

    // First Blackmagic camera seen during scan — fallback when preferred addr isn't found
    std::string          _candidateAddr;
    std::string          _candidateName;
    esp_ble_addr_type_t  _candidateType = BLE_ADDR_TYPE_PUBLIC;

    CameraData _camera{};

    static BlackmagicCamera *_instance;
};
