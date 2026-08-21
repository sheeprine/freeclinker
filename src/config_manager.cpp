#include "config_manager.h"
#include "camera_registry.h"
#include "config.h"
#include <cstring>
#include <cstdlib>

static constexpr const char *NVS_NS   = "bridge";
static constexpr const char *KEY_DSD  = "disarm_delay";
static constexpr const char *KEY_SOD  = "stop_on_disarm";
static constexpr const char *KEY_ACH  = "aux_channel";
static constexpr const char *KEY_AMD  = "aux_mode";
static constexpr const char *KEY_CAM  = "camera_type";
static constexpr const char *KEY_OSD1 = "osd1_tpl";
static constexpr const char *KEY_OSD2 = "osd2_tpl";
static constexpr const char *KEY_OSD3 = "osd3_tpl";
static constexpr const char *KEY_OSD4 = "osd4_tpl";
static constexpr const char *KEY_SCM  = "strict_cam";

void ConfigManager::begin(Stream &serial) {
    _serial = &serial;
    _prefs.begin(NVS_NS, false);
    load();
    _serial->println("[cfg] Type 'help' for configuration commands");
    printAll(*_serial);
}

static void loadStr(Preferences &p, const char *key, char *dst, size_t dstLen, const char *def) {
    String v = p.getString(key, def);
    strlcpy(dst, v.c_str(), dstLen);
}

void ConfigManager::load() {
    _cfg.disarmStopDelayMs = _prefs.getUInt(KEY_DSD, DEFAULT_DISARM_STOP_DELAY_MS);
    _cfg.stopOnDisarm      = _prefs.getBool(KEY_SOD, DEFAULT_STOP_ON_DISARM);
    _cfg.auxChannel        = static_cast<uint8_t>(_prefs.getUInt(KEY_ACH, DEFAULT_AUX_CHANNEL));
    _cfg.auxMode           = static_cast<uint8_t>(_prefs.getUInt(KEY_AMD, DEFAULT_AUX_MODE));
    _cfg.cameraType        = static_cast<uint8_t>(_prefs.getUInt(KEY_CAM, DEFAULT_CAMERA_TYPE));
    _cfg.strictCamera      = _prefs.getBool(KEY_SCM, DEFAULT_STRICT_CAMERA);
    loadStr(_prefs, KEY_OSD1, _cfg.osd1Tpl, sizeof(_cfg.osd1Tpl), DEFAULT_OSD1_TPL);
    loadStr(_prefs, KEY_OSD2, _cfg.osd2Tpl, sizeof(_cfg.osd2Tpl), DEFAULT_OSD2_TPL);
    loadStr(_prefs, KEY_OSD3, _cfg.osd3Tpl, sizeof(_cfg.osd3Tpl), DEFAULT_OSD3_TPL);
    loadStr(_prefs, KEY_OSD4, _cfg.osd4Tpl, sizeof(_cfg.osd4Tpl), DEFAULT_OSD4_TPL);
}

void ConfigManager::save() {
    _prefs.putUInt(KEY_DSD, _cfg.disarmStopDelayMs);
    _prefs.putBool(KEY_SOD, _cfg.stopOnDisarm);
    _prefs.putUInt(KEY_ACH, _cfg.auxChannel);
    _prefs.putUInt(KEY_AMD, _cfg.auxMode);
    _prefs.putUInt(KEY_CAM, _cfg.cameraType);
    _prefs.putBool(KEY_SCM, _cfg.strictCamera);
    _prefs.putString(KEY_OSD1, _cfg.osd1Tpl);
    _prefs.putString(KEY_OSD2, _cfg.osd2Tpl);
    _prefs.putString(KEY_OSD3, _cfg.osd3Tpl);
    _prefs.putString(KEY_OSD4, _cfg.osd4Tpl);
}

void ConfigManager::printAll(Stream &out) {
    out.printf("[cfg] camera_type     = %s\n", _cfg.cameraType == 1 ? "GoPro" : "DJI");
    out.printf("[cfg] strict_camera   = %s\n", _cfg.strictCamera ? "true" : "false");
    out.printf("[cfg] disarm_delay    = %u ms\n", _cfg.disarmStopDelayMs);
    out.printf("[cfg] stop_on_disarm  = %s\n", _cfg.stopOnDisarm ? "true" : "false");
    if (_cfg.auxChannel == 0)
        out.println("[cfg] aux_channel     = disabled");
    else
        out.printf("[cfg] aux_channel     = AUX%u\n", _cfg.auxChannel);
    out.printf("[cfg] aux_mode        = 0x%02X\n", _cfg.auxMode);
    out.printf("[cfg] osd1            = %s\n", _cfg.osd1Tpl);
    out.printf("[cfg] osd2            = %s\n", _cfg.osd2Tpl);
    out.printf("[cfg] osd3            = %s\n", _cfg.osd3Tpl);
    out.printf("[cfg] osd4            = %s\n", _cfg.osd4Tpl);
}

void ConfigManager::handleLine(const char *line, Stream &out) {
    while (*line == ' ') line++;

    if (strcmp(line, "version") == 0) {
        out.printf("[cfg] FreeCLinker firmware v%s\n", FIRMWARE_VERSION);
        return;
    }

    if (strcmp(line, "help") == 0) {
        out.println("Commands:");
        out.println("  version                    - print firmware version");
        out.println("  show                       - print all settings");
        out.println("  set camera_type <0|1>      - 0=DJI, 1=GoPro (reboot required)");
        out.println("  set strict_camera <0|1>    - 1=only connect to preferred camera, skip unknown ones");
        out.println("  set disarm_delay <ms>      - delay before stopping recording after disarm");
        out.println("  set stop_on_disarm <0|1>   - disable (0) or enable (1) stop on disarm");
        out.println("  set aux_channel <0-12>     - AUX channel for camera mode switch (0=off)");
        out.println("  set aux_mode <0x00-0xFF>   - camera mode when AUX high (0x00=slow_motion 0x01=video 0x0A=hyperlapse)");
        out.println("  set osd1 <template>        - OSD Custom Message 1 template (default: battery)");
        out.println("  set osd2 <template>        - OSD Custom Message 2 template (default: recording)");
        out.println("  set osd3 <template>        - OSD Custom Message 3 template (default: settings)");
        out.println("  set osd4 <template>        - OSD Custom Message 4 template (default: storage)");
        out.println("  Tokens: {bat} {rec} {mode} {res} {fps} {eis} {rleft} {rcap}");
        out.println("  reset                      - restore defaults");
        out.println("Camera list commands:");
        out.println("  cameras list               - list saved cameras");
        out.println("  cameras connect <idx>      - select camera for next connection");
        out.println("  cameras remove <idx>       - remove camera from list");
        out.println("  cameras clear              - remove all saved cameras");
        return;
    }

    if (strcmp(line, "show") == 0) {
        printAll(out);
        return;
    }

    if (strcmp(line, "reset") == 0) {
        _prefs.clear();
        load();
        out.println("[cfg] Reset to defaults");
        printAll(out);
        return;
    }

    if (strncmp(line, "set ", 4) == 0) {
        const char *rest = line + 4;
        while (*rest == ' ') rest++;

        if (strncmp(rest, "camera_type ", 12) == 0) {
            const char *val = rest + 12;
            while (*val == ' ') val++;
            uint8_t t = static_cast<uint8_t>(strtoul(val, nullptr, 10));
            if (t > 1) {
                out.println("[cfg] camera_type must be 0 (DJI) or 1 (GoPro)");
                return;
            }
            setCameraType(t);
            out.printf("[cfg] camera_type = %s (saved — reboot to apply)\n",
                       t == 1 ? "GoPro" : "DJI");
            return;
        }

        if (strncmp(rest, "strict_camera ", 14) == 0) {
            const char *val = rest + 14;
            while (*val == ' ') val++;
            setStrictCamera(strtoul(val, nullptr, 10) != 0);
            out.printf("[cfg] strict_camera = %s (saved)\n", _cfg.strictCamera ? "true" : "false");
            return;
        }

        if (strncmp(rest, "disarm_delay ", 13) == 0) {
            const char *val = rest + 13;
            while (*val == ' ') val++;
            setDisarmDelay(static_cast<uint32_t>(strtoul(val, nullptr, 10)));
            out.printf("[cfg] disarm_delay = %u ms (saved)\n", _cfg.disarmStopDelayMs);
            return;
        }

        if (strncmp(rest, "stop_on_disarm ", 15) == 0) {
            const char *val = rest + 15;
            while (*val == ' ') val++;
            setStopOnDisarm(strtoul(val, nullptr, 10) != 0);
            out.printf("[cfg] stop_on_disarm = %s (saved)\n", _cfg.stopOnDisarm ? "true" : "false");
            return;
        }

        if (strncmp(rest, "aux_channel ", 12) == 0) {
            const char *val = rest + 12;
            while (*val == ' ') val++;
            uint8_t ch = static_cast<uint8_t>(strtoul(val, nullptr, 10));
            if (ch > 12) {
                out.println("[cfg] aux_channel must be 0-12");
                return;
            }
            setAuxChannel(ch);
            if (ch == 0)
                out.println("[cfg] aux_channel = disabled (saved)");
            else
                out.printf("[cfg] aux_channel = AUX%u (saved)\n", ch);
            return;
        }

        if (strncmp(rest, "aux_mode ", 9) == 0) {
            const char *val = rest + 9;
            while (*val == ' ') val++;
            setAuxMode(static_cast<uint8_t>(strtoul(val, nullptr, 0)));
            out.printf("[cfg] aux_mode = 0x%02X (saved)\n", _cfg.auxMode);
            return;
        }

        if (strncmp(rest, "osd1 ", 5) == 0) {
            setOsdTemplate(1, rest + 5);
            out.printf("[cfg] osd1 = %s (saved)\n", _cfg.osd1Tpl);
            return;
        }

        if (strncmp(rest, "osd2 ", 5) == 0) {
            setOsdTemplate(2, rest + 5);
            out.printf("[cfg] osd2 = %s (saved)\n", _cfg.osd2Tpl);
            return;
        }

        if (strncmp(rest, "osd3 ", 5) == 0) {
            setOsdTemplate(3, rest + 5);
            out.printf("[cfg] osd3 = %s (saved)\n", _cfg.osd3Tpl);
            return;
        }

        if (strncmp(rest, "osd4 ", 5) == 0) {
            setOsdTemplate(4, rest + 5);
            out.printf("[cfg] osd4 = %s (saved)\n", _cfg.osd4Tpl);
            return;
        }

        out.printf("[cfg] Unknown setting: %s\n", rest);
        return;
    }

    if (strncmp(line, "cameras", 7) == 0) {
        const char *sub = line + 7;
        while (*sub == ' ') sub++;
        handleCamerasCmd(sub, out);
        return;
    }

    if (strlen(line) > 0)
        out.printf("[cfg] Unknown command: %s\n", line);
}

void ConfigManager::handleCamerasCmd(const char *sub, Stream &out) {
    if (!_registry) {
        out.println("[reg] Camera registry not available");
        return;
    }

    if (strcmp(sub, "list") == 0 || strlen(sub) == 0) {
        _registry->printList(out);
        return;
    }

    if (strcmp(sub, "clear") == 0) {
        _registry->clear();
        out.println("[reg] Camera list cleared");
        return;
    }

    if (strncmp(sub, "remove ", 7) == 0) {
        const char *arg = sub + 7;
        while (*arg == ' ') arg++;
        uint8_t idx = (uint8_t)strtoul(arg, nullptr, 10);
        if (_registry->remove(idx))
            out.printf("[reg] Camera %u removed\n", idx);
        else
            out.printf("[reg] No camera at index %u\n", idx);
        return;
    }

    if (strncmp(sub, "connect ", 8) == 0) {
        const char *arg = sub + 8;
        while (*arg == ' ') arg++;
        uint8_t idx = (uint8_t)strtoul(arg, nullptr, 10);
        if (idx < _registry->count()) {
            _registry->selectCamera(idx);
            CameraEntry e;
            _registry->getEntry(idx, e);
            out.printf("[reg] Camera %u selected: \"%s\" %s — will connect on next scan\n",
                       idx, e.name, e.addr);
        } else {
            out.printf("[reg] No camera at index %u (list has %u entr%s)\n",
                       idx, _registry->count(),
                       _registry->count() == 1 ? "y" : "ies");
        }
        return;
    }

    out.println("Camera list commands:");
    out.println("  cameras list           - list saved cameras");
    out.println("  cameras connect <idx>  - select camera for next connection");
    out.println("  cameras remove <idx>   - remove camera from list");
    out.println("  cameras clear          - remove all saved cameras");
}

void ConfigManager::processCommand(const char *line, Stream &out) {
    handleLine(line, out);
}

void ConfigManager::update() {
    if (!_serial) return;
    while (_serial->available()) {
        const char c = static_cast<char>(_serial->read());
        if (c == '\r') continue;
        if (c == '\n') {
            _buf[_len] = '\0';
            handleLine(_buf, *_serial);
            _len = 0;
        } else if (_len < sizeof(_buf) - 1) {
            _buf[_len++] = c;
        }
    }
}

// ── Setters ────────────────────────────────────────────────────────────────────

void ConfigManager::setCameraType(uint8_t v) {
    _cfg.cameraType = v;
    _prefs.putUInt(KEY_CAM, v);
}

void ConfigManager::setDisarmDelay(uint32_t ms) {
    _cfg.disarmStopDelayMs = ms;
    _prefs.putUInt(KEY_DSD, ms);
}

void ConfigManager::setStopOnDisarm(bool v) {
    _cfg.stopOnDisarm = v;
    _prefs.putBool(KEY_SOD, v);
}

void ConfigManager::setAuxChannel(uint8_t ch) {
    _cfg.auxChannel = ch;
    _prefs.putUInt(KEY_ACH, ch);
}

void ConfigManager::setAuxMode(uint8_t mode) {
    _cfg.auxMode = mode;
    _prefs.putUInt(KEY_AMD, mode);
}

void ConfigManager::setStrictCamera(bool v) {
    _cfg.strictCamera = v;
    _prefs.putBool(KEY_SCM, v);
}

void ConfigManager::setOsdTemplate(uint8_t n, const char *tpl) {
    char *dst;
    const char *key;
    switch (n) {
        case 1: dst = _cfg.osd1Tpl; key = KEY_OSD1; break;
        case 2: dst = _cfg.osd2Tpl; key = KEY_OSD2; break;
        case 3: dst = _cfg.osd3Tpl; key = KEY_OSD3; break;
        case 4: dst = _cfg.osd4Tpl; key = KEY_OSD4; break;
        default: return;
    }
    strlcpy(dst, tpl, OSD_TPL_LEN);
    _prefs.putString(key, dst);
}
