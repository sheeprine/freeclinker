#pragma once

#include <Preferences.h>
#include <Stream.h>
#include <stdint.h>

class CameraRegistry;  // forward declaration
class Camera;           // forward declaration
struct CameraData;      // forward declaration

class ConfigManager {
public:
    static constexpr uint8_t OSD_TPL_LEN = 32;

    struct Config {
        uint32_t disarmStopDelayMs;  // delay between FC disarm and stopping recording
        bool     stopOnDisarm;       // false = never stop recording on disarm
        uint8_t  auxChannel;         // AUX channel for camera mode switch (0=disabled, 1=AUX1, …)
        uint8_t  auxMode;            // camera mode when AUX is high (0x00=slow motion)
        uint8_t  cameraType;         // 0=DJI, 1=GoPro, 2=Caddx, 3=Sony, 4=Blackmagic, 5=Insta360
        uint8_t  cameraMatchMode;    // CAM_MATCH_* — see camera.h
        bool     cameraWakeGuard;    // true = don't connect to a sleeping/powered-down GoPro (see camera.h setWakeGuard)
        bool     debugBle;           // true = log raw BLE TX/RX packets to the serial console
        bool     lowPowerMode;       // true = minimum BLE/Wi-Fi TX power, to reduce interference with the RC receiver (see camera.h setLowPowerMode)
        // OSD custom message templates — tokens: {bat} {rec} {mode} {res} {fps} {eis} {rleft} {rcap}
        char osd1Tpl[OSD_TPL_LEN];  // Custom Message 1 (default: battery)
        char osd2Tpl[OSD_TPL_LEN];  // Custom Message 2 (default: recording state)
        char osd3Tpl[OSD_TPL_LEN];  // Custom Message 3 (default: camera settings)
        char osd4Tpl[OSD_TPL_LEN];  // Custom Message 4 (default: storage)
        // Betaflight 4.5 has Pilot Name and Craft Name but not the Custom
        // Message 1-4 fields (added in 4.6). When enabled, the four osdN
        // templates above are not sent at all and pilotNameTpl/craftNameTpl
        // are sent to Pilot Name/Craft Name instead.
        bool     bf45Compat;
        bool     pilotNameEnabled;  // false = skip sending Pilot Name even when bf45Compat is on
        char     pilotNameTpl[OSD_TPL_LEN];  // Pilot Name (default: battery)
        bool     craftNameEnabled;  // false = skip sending Craft Name even when bf45Compat is on
        char     craftNameTpl[OSD_TPL_LEN];  // Craft Name (default: recording state)
        // No Caddx SSID/password here — those live in CameraRegistry
        // (see setCaddxSsid()/setCaddxPass()), same storage every other
        // camera's connection identity uses.
    };

    static constexpr uint32_t DEFAULT_DISARM_STOP_DELAY_MS = 0;
    static constexpr bool     DEFAULT_STOP_ON_DISARM       = true;
    static constexpr uint8_t  DEFAULT_AUX_CHANNEL          = 0;
    static constexpr uint8_t  DEFAULT_AUX_MODE             = 0x00;  // slow motion
    static constexpr uint8_t  DEFAULT_CAMERA_TYPE          = 0;     // DJI
    static constexpr uint8_t  DEFAULT_CAMERA_MATCH_MODE    = 0;     // CAM_MATCH_FALLBACK
    static constexpr bool     DEFAULT_CAMERA_WAKE_GUARD    = true;
    static constexpr bool     DEFAULT_DEBUG_BLE            = false;
    static constexpr bool     DEFAULT_LOW_POWER_MODE       = true;
    static constexpr const char *DEFAULT_OSD1_TPL = "CAM:{bat}";
    static constexpr const char *DEFAULT_OSD2_TPL = "{rec}";
    static constexpr const char *DEFAULT_OSD3_TPL = "{mode} {res}/{fps} {eis}";
    static constexpr const char *DEFAULT_OSD4_TPL = "{rleft} {rcap}";
    static constexpr bool     DEFAULT_BF45_COMPAT      = false;
    static constexpr bool     DEFAULT_PILOT_NAME_ENABLED = true;
    static constexpr const char *DEFAULT_PILOT_NAME_TPL = "CAM:{bat}";
    static constexpr bool     DEFAULT_CRAFT_NAME_ENABLED = true;
    static constexpr const char *DEFAULT_CRAFT_NAME_TPL = "{rec}";

    void begin(Stream &serial);
    void update();
    void setRegistry(CameraRegistry *reg) { _registry = reg; }
    // `data` must outlive the ConfigManager — main.cpp passes the address of
    // its file-scope CameraData, refreshed on every camera callback.
    void setCamera(Camera *cam, const CameraData *data) { _camera = cam; _cameraData = data; }

    const Config &config() const { return _cfg; }

    // Process a single CLI command, writing response to `out`.
    void processCommand(const char *line, Stream &out);

    // Individual setters — each persists to NVS immediately.
    void setCameraType(uint8_t v);
    void setDisarmDelay(uint32_t ms);
    void setStopOnDisarm(bool v);
    void setAuxChannel(uint8_t ch);
    void setAuxMode(uint8_t mode);
    void setCameraMatchMode(uint8_t v);
    void setCameraWakeGuard(bool v);
    void setDebugBle(bool v);
    void setLowPowerMode(bool v);
    void setOsdTemplate(uint8_t n, const char *tpl);  // n = 1..4
    void setBf45Compat(bool v);
    void setPilotNameEnabled(bool v);
    void setPilotNameTemplate(const char *tpl);
    void setCraftNameEnabled(bool v);
    void setCraftNameTemplate(const char *tpl);
    // Both write into the registry's preferred/newest Caddx entry, not NVS
    // (see CameraEntry) — return false if there's nothing to write to yet
    // (setCaddxPass before any setCaddxSsid) or ssid is empty.
    bool setCaddxSsid(const char *ssid);
    bool setCaddxPass(const char *pass);

private:
    void load();
    void save();
    void printAll(Stream &out);
    void handleLine(const char *line, Stream &out);
    void handleCamerasCmd(const char *sub, Stream &out);
    void handleWifiCmd(const char *sub, Stream &out);

    Preferences       _prefs;
    Config            _cfg{};
    CameraRegistry   *_registry   = nullptr;
    Camera           *_camera     = nullptr;
    const CameraData *_cameraData = nullptr;
    Stream           *_serial     = nullptr;
    char            _buf[80];
    uint8_t         _len = 0;
};
