#pragma once
#include <stdint.h>
#include "telemetry.h"

using CameraCallback = void (*)(const CameraData &);

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

    void setCameraCallback(CameraCallback cb) { _cameraCb = cb; }

protected:
    CameraCallback _cameraCb = nullptr;
};
