#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "camera.h"
#include "caddx_protocol.h"

// Wi-Fi/HTTP client for the Caddx Orca action camera.
//
// Unlike BLECamera (DJI) and GoProCamera, Orca is controlled over the
// camera's own Wi-Fi access point using plain unauthenticated HTTP GET
// requests to a CGI endpoint (the "Hi3510" action-camera protocol family —
// see src/caddx_protocol.h for full provenance notes). There is no BLE scan
// / pairing step and no CameraRegistry integration: the ESP32 simply joins
// a pre-configured SSID/password (see setWifiCredentials()) exactly like the
// official app requires the user to do from the phone's Wi-Fi settings.
//
// Connection sequence
// ────────────────────
// 1. Join the configured Wi-Fi network (WiFi.begin()).
// 2. Once associated, GET /cgi-bin/hi3510/getdeviceattr.cgi to learn the
//    camera's "hardversion" and "type" fields — these select which CGI
//    endpoint variant startRecording()/stopRecording() must use (see
//    caddx_protocol.h and re/caddx_orca_protocol/findings.md; this branch
//    is transcribed from the app but has not been exercised against real
//    hardware).
// 3. Poll /getallinfo.cgi (work state / mode / elapsed time),
//    /getbatterycapacity.cgi, and /getsdstate.cgi on a slow rotating
//    schedule to populate CameraData.
//
// Every HTTP request here is a *blocking* call (ESP32 Arduino's HTTPClient
// has no async API). Polling is deliberately infrequent and uses a short
// timeout to bound how long update() can stall the rest of the main loop —
// there is no precedent for an async HTTP state machine elsewhere in this
// codebase, and building one is out of scope for this driver.
class CaddxCamera : public Camera {
public:
    void begin();
    void update();

    bool isConnected() const override { return _connected; }

    bool startRecording() override;
    bool stopRecording() override;
    // mode is a DJI_MODE_* constant; mapped to a setcurworkmode.cgi name.
    // TODO(unverified): see caddx_camera.cpp — mode-name table is
    // transcribed from the app but the legacy/NewAPP selection has not been
    // confirmed against a real Orca.
    bool switchCameraMode(uint8_t mode) override;

    // Must be called before begin(). Caddx has no BLE scan/registry, so the
    // network to join has to be supplied explicitly (config_manager.cpp).
    void setWifiCredentials(const char *ssid, const char *password);

private:
    enum class RecordVariant : uint8_t { kUnknown, kRecordCgi, kRecord2Cgi };

    void pollDeviceAttr();   // one-shot, right after Wi-Fi association
    void pollAllInfo();      // work state / mode / elapsed time
    void pollAllInfoSs(const String &body);      // NewAPP/SigmaStar parsing
    void pollAllInfoLegacy(const String &body);  // legacy Hi3510 parsing
    void pollBattery();
    void pollSdState();

    // Fires one GET request; returns the HTTP status code (-1 on error).
    // On success, `body` is the response text.
    int httpGet(const String &path, String &body);

    String baseUrl() const { return "http://" + _ip.toString() + CADDX_CGI_PATH; }

    uint8_t mapWorkModeToDji(long workMode) const;             // legacy numeric mode
    bool mapSsWorkModeToDji(const String &name, uint8_t &out) const;  // NewAPP/SS name string

    String   _ssid;
    String   _password;
    IPAddress _ip;

    bool _wifiWasConnected = false;
    bool _connected         = false;  // application-level: getdeviceattr.cgi succeeded
    bool _attrProbed        = false;

    RecordVariant _recordVariant = RecordVariant::kUnknown;
    bool          _isNewApp      = false;  // hardversion == "NewAPP"

    uint32_t _lastPollMs   = 0;
    uint8_t  _pollPhase    = 0;   // round-robins allinfo / battery / sdstate

    CameraData _camera{};
};
