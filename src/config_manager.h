#pragma once

#include <Preferences.h>
#include <Stream.h>
#include <stdint.h>

class CameraRegistry;  // forward declaration

class ConfigManager {
public:
    static constexpr uint8_t OSD_TPL_LEN = 32;

    struct Config {
        uint32_t disarmStopDelayMs;  // delay between FC disarm and stopping recording
        bool     stopOnDisarm;       // false = never stop recording on disarm
        uint8_t  auxChannel;         // AUX channel for camera mode switch (0=disabled, 1=AUX1, …)
        uint8_t  auxMode;            // camera mode when AUX is high (0x00=slow motion)
        uint8_t  cameraType;         // 0=DJI, 1=GoPro
        // OSD custom message templates — tokens: {bat} {rec} {mode} {res} {fps} {eis} {rleft} {rcap}
        char osd1Tpl[OSD_TPL_LEN];  // Custom Message 1 (default: battery)
        char osd2Tpl[OSD_TPL_LEN];  // Custom Message 2 (default: recording state)
        char osd3Tpl[OSD_TPL_LEN];  // Custom Message 3 (default: camera settings)
        char osd4Tpl[OSD_TPL_LEN];  // Custom Message 4 (default: storage)
    };

    static constexpr uint32_t DEFAULT_DISARM_STOP_DELAY_MS = 0;
    static constexpr bool     DEFAULT_STOP_ON_DISARM       = true;
    static constexpr uint8_t  DEFAULT_AUX_CHANNEL          = 0;
    static constexpr uint8_t  DEFAULT_AUX_MODE             = 0x00;  // slow motion
    static constexpr uint8_t  DEFAULT_CAMERA_TYPE          = 0;     // DJI
    static constexpr const char *DEFAULT_OSD1_TPL = "CAM:{bat}";
    static constexpr const char *DEFAULT_OSD2_TPL = "{rec}";
    static constexpr const char *DEFAULT_OSD3_TPL = "{mode} {res}/{fps} {eis}";
    static constexpr const char *DEFAULT_OSD4_TPL = "{rleft} {rcap}";

    void begin(Stream &serial);
    void update();
    void setRegistry(CameraRegistry *reg) { _registry = reg; }

    const Config &config() const { return _cfg; }

    // Process a single CLI command, writing response to `out`.
    void processCommand(const char *line, Stream &out);

    // Individual setters — each persists to NVS immediately.
    void setCameraType(uint8_t v);
    void setDisarmDelay(uint32_t ms);
    void setStopOnDisarm(bool v);
    void setAuxChannel(uint8_t ch);
    void setAuxMode(uint8_t mode);
    void setOsdTemplate(uint8_t n, const char *tpl);  // n = 1..4

private:
    void load();
    void save();
    void printAll(Stream &out);
    void handleLine(const char *line, Stream &out);
    void handleCamerasCmd(const char *sub, Stream &out);

    Preferences     _prefs;
    Config          _cfg{};
    CameraRegistry *_registry = nullptr;
    Stream         *_serial   = nullptr;
    char            _buf[80];
    uint8_t         _len = 0;
};
