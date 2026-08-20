#pragma once

#include <Preferences.h>
#include <Stream.h>
#include <stdint.h>

class ConfigManager {
public:
    struct Config {
        uint32_t disarmStopDelayMs;  // delay between FC disarm and stopping recording
        bool     stopOnDisarm;       // false = never stop recording on disarm
        uint8_t  auxChannel;         // AUX channel for camera mode switch (0=disabled, 1=AUX1, …)
        uint8_t  auxMode;            // camera mode when AUX is high (0x00=slow motion)
        uint8_t  cameraType;         // 0=DJI, 1=GoPro
    };

    static constexpr uint32_t DEFAULT_DISARM_STOP_DELAY_MS = 0;
    static constexpr bool     DEFAULT_STOP_ON_DISARM       = true;
    static constexpr uint8_t  DEFAULT_AUX_CHANNEL          = 0;
    static constexpr uint8_t  DEFAULT_AUX_MODE             = 0x00;  // slow motion
    static constexpr uint8_t  DEFAULT_CAMERA_TYPE          = 0;     // DJI

    void begin(Stream &serial);
    void update();

    const Config &config() const { return _cfg; }

    // Process a single CLI command, writing response to `out`.
    void processCommand(const char *line, Stream &out);

    // Individual setters — each persists to NVS immediately.
    void setCameraType(uint8_t v);
    void setDisarmDelay(uint32_t ms);
    void setStopOnDisarm(bool v);
    void setAuxChannel(uint8_t ch);
    void setAuxMode(uint8_t mode);

private:
    void load();
    void save();
    void printAll(Stream &out);
    void handleLine(const char *line, Stream &out);

    Preferences _prefs;
    Config      _cfg{};
    Stream     *_serial = nullptr;
    char        _buf[80];
    uint8_t     _len = 0;
};
