#pragma once
#include <stdint.h>
#include "telemetry.h"

using CameraCallback = void (*)(const CameraData &);

class CameraRegistry;  // forward declaration — avoids pulling in registry headers here

// Camera matching strategy applied during scan when the preferred
// (last-connected or manually selected) address isn't the only eligible
// camera seen.
#define CAM_MATCH_FALLBACK    0  // connect to preferred if seen, else fall back to any eligible camera
#define CAM_MATCH_STRICT      1  // only ever connect to the preferred camera; ignore all others
#define CAM_MATCH_BEST_SIGNAL 2  // ignore preferred; connect to whichever eligible camera has the strongest RSSI

// Abstract camera interface implemented by BLECamera (DJI) and GoProCamera.
class Camera {
public:
    virtual ~Camera() = default;
    virtual void begin() = 0;
    virtual void update() = 0;
    virtual bool isConnected() const = 0;
    virtual bool startRecording() = 0;
    virtual bool stopRecording() = 0;
    // mode uses DJI_MODE_* constants; each implementation maps as needed.
    virtual bool switchCameraMode(uint8_t mode) = 0;
    // Trigger a burst slow-motion capture (GoPro only; no-op on other cameras).
    virtual bool triggerBurstSloMo() { return false; }
    // Return to standard sub-mode after a burst slow-motion capture.
    virtual bool exitBurstSloMo() { return false; }

    void setCameraCallback(CameraCallback cb) { _cameraCb = cb; }
    void setRegistry(CameraRegistry *reg)     { _registry = reg; }
    // v is one of the CAM_MATCH_* constants above.
    void setMatchMode(uint8_t v)              { _matchMode = v; }
    // When true (default), avoid connecting to a camera whose advertisement
    // indicates it's asleep/powered-down but still remote-wake advertising —
    // a GATT connection attempt would otherwise wake it. GoPro-specific;
    // other backends have no sleep-advertisement format to detect and ignore
    // this flag. Bypassed for an explicitly, manually selected camera.
    void setWakeGuard(bool v)                 { _wakeGuard = v; }
    // When true, implementations log raw BLE TX/RX packets to DBG_SERIAL.
    void setDebugBle(bool v)                  { _debugBle = v; }
    // When true (default), radio TX power is set to its minimum instead of
    // maximum — shorter range, but less RF interference with the flight
    // controller's 2.4GHz RC receiver, which shares the aircraft with this
    // radio. Applies to BLE power on the BLE-based backends and, for
    // CaddxCamera (the only Wi-Fi backend), Wi-Fi TX power.
    void setLowPowerMode(bool v)              { _lowPowerMode = v; }

protected:
    CameraCallback  _cameraCb     = nullptr;
    CameraRegistry *_registry     = nullptr;
    uint8_t         _matchMode    = CAM_MATCH_FALLBACK;
    bool            _wakeGuard    = true;
    bool            _debugBle     = false;  // when true, log raw BLE packets
    bool            _lowPowerMode = true;
};
