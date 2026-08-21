#pragma once
#include <stdint.h>
#include "telemetry.h"

using CameraCallback = void (*)(const CameraData &);

class CameraRegistry;  // forward declaration — avoids pulling in registry headers here

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

protected:
    CameraCallback  _cameraCb = nullptr;
    CameraRegistry *_registry = nullptr;
};
