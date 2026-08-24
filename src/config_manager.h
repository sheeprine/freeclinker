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
    static constexpr uint8_t WIFI_CRED_LEN = 64;  // WPA2 passphrase max is 63 chars + NUL

    struct Config {
        uint32_t disarmStopDelayMs;  // delay between FC disarm and stopping recording
        bool     stopOnDisarm;       // false = never stop recording on disarm
        uint8_t  auxChannel;         // AUX channel for camera mode switch (0=disabled, 1=AUX1, …)
        uint8_t  auxMode;            // camera mode when AUX is high (0x00=slow motion)
        uint8_t  cameraType;         // 0=DJI, 1=GoPro, 2=Caddx
        bool     strictCamera;       // true = never connect to a camera other than the preferred one
        bool     debugBle;           // true = log raw BLE TX/RX packets to the serial console
        // OSD custom message templates — tokens: {bat} {rec} {mode} {res} {fps} {eis} {rleft} {rcap}
        char osd1Tpl[OSD_TPL_LEN];  // Custom Message 1 (default: battery)
        char osd2Tpl[OSD_TPL_LEN];  // Custom Message 2 (default: recording state)
        char osd3Tpl[OSD_TPL_LEN];  // Custom Message 3 (default: camera settings)
        char osd4Tpl[OSD_TPL_LEN];  // Custom Message 4 (default: storage)
        // Caddx Orca only — the camera has no BLE pairing flow, so the Wi-Fi
        // network it creates has to be configured explicitly (see caddx_camera.h).
        char caddxSsid[WIFI_CRED_LEN];
        char caddxPass[WIFI_CRED_LEN];
    };

    static constexpr uint32_t DEFAULT_DISARM_STOP_DELAY_MS = 0;
    static constexpr bool     DEFAULT_STOP_ON_DISARM       = true;
    static constexpr uint8_t  DEFAULT_AUX_CHANNEL          = 0;
    static constexpr uint8_t  DEFAULT_AUX_MODE             = 0x00;  // slow motion
    static constexpr uint8_t  DEFAULT_CAMERA_TYPE          = 0;     // DJI
    static constexpr bool     DEFAULT_STRICT_CAMERA        = false;
    static constexpr bool     DEFAULT_DEBUG_BLE            = false;
    static constexpr const char *DEFAULT_OSD1_TPL = "CAM:{bat}";
    static constexpr const char *DEFAULT_OSD2_TPL = "{rec}";
    static constexpr const char *DEFAULT_OSD3_TPL = "{mode} {res}/{fps} {eis}";
    static constexpr const char *DEFAULT_OSD4_TPL = "{rleft} {rcap}";
    static constexpr const char *DEFAULT_CADDX_SSID = "";
    // Orca ships with this fixed factory Wi-Fi password on every unit
    // (user-confirmed); the SSID is still per-camera and must be set.
    static constexpr const char *DEFAULT_CADDX_PASS = "12345678";

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
    void setStrictCamera(bool v);
    void setDebugBle(bool v);
    void setOsdTemplate(uint8_t n, const char *tpl);  // n = 1..4
    void setCaddxSsid(const char *ssid);
    void setCaddxPass(const char *pass);

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
