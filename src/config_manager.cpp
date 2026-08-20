#include "config_manager.h"
#include <cstring>
#include <cstdlib>

static constexpr const char *NVS_NS  = "bridge";
static constexpr const char *KEY_DSD = "disarm_delay";
static constexpr const char *KEY_SOD = "stop_on_disarm";
static constexpr const char *KEY_ACH = "aux_channel";
static constexpr const char *KEY_AMD = "aux_mode";
static constexpr const char *KEY_CAM = "camera_type";

void ConfigManager::begin(Stream &serial) {
    _serial = &serial;
    _prefs.begin(NVS_NS, false);
    load();
    _serial->println("[cfg] Type 'help' for configuration commands");
    printAll();
}

void ConfigManager::load() {
    _cfg.disarmStopDelayMs = _prefs.getUInt(KEY_DSD, DEFAULT_DISARM_STOP_DELAY_MS);
    _cfg.stopOnDisarm      = _prefs.getBool(KEY_SOD, DEFAULT_STOP_ON_DISARM);
    _cfg.auxChannel        = static_cast<uint8_t>(_prefs.getUInt(KEY_ACH, DEFAULT_AUX_CHANNEL));
    _cfg.auxMode           = static_cast<uint8_t>(_prefs.getUInt(KEY_AMD, DEFAULT_AUX_MODE));
    _cfg.cameraType        = static_cast<uint8_t>(_prefs.getUInt(KEY_CAM, DEFAULT_CAMERA_TYPE));
}

void ConfigManager::save() {
    _prefs.putUInt(KEY_DSD, _cfg.disarmStopDelayMs);
    _prefs.putBool(KEY_SOD, _cfg.stopOnDisarm);
    _prefs.putUInt(KEY_ACH, _cfg.auxChannel);
    _prefs.putUInt(KEY_AMD, _cfg.auxMode);
    _prefs.putUInt(KEY_CAM, _cfg.cameraType);
}

void ConfigManager::printAll() {
    _serial->printf("[cfg] camera_type     = %s\n", _cfg.cameraType == 1 ? "GoPro" : "DJI");
    _serial->printf("[cfg] disarm_delay    = %u ms\n", _cfg.disarmStopDelayMs);
    _serial->printf("[cfg] stop_on_disarm  = %s\n", _cfg.stopOnDisarm ? "true" : "false");
    if (_cfg.auxChannel == 0)
        _serial->println("[cfg] aux_channel     = disabled");
    else
        _serial->printf("[cfg] aux_channel     = AUX%u\n", _cfg.auxChannel);
    _serial->printf("[cfg] aux_mode        = 0x%02X\n", _cfg.auxMode);
}

void ConfigManager::handleLine(const char *line) {
    while (*line == ' ') line++;

    if (strcmp(line, "help") == 0) {
        _serial->println("Commands:");
        _serial->println("  show                       - print all settings");
        _serial->println("  set camera_type <0|1>      - 0=DJI, 1=GoPro (reboot required)");
        _serial->println("  set disarm_delay <ms>      - delay before stopping recording after disarm");
        _serial->println("  set stop_on_disarm <0|1>   - disable (0) or enable (1) stop on disarm");
        _serial->println("  set aux_channel <0-12>     - AUX channel for camera mode switch (0=off)");
        _serial->println("  set aux_mode <0x00-0xFF>   - camera mode when AUX high (0x00=slow_motion 0x01=video 0x0A=hyperlapse)");
        _serial->println("  reset                      - restore defaults");
        return;
    }

    if (strcmp(line, "show") == 0) {
        printAll();
        return;
    }

    if (strcmp(line, "reset") == 0) {
        _prefs.clear();
        load();
        _serial->println("[cfg] Reset to defaults");
        printAll();
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
                _serial->println("[cfg] camera_type must be 0 (DJI) or 1 (GoPro)");
                return;
            }
            _cfg.cameraType = t;
            save();
            _serial->printf("[cfg] camera_type = %s (saved — reboot to apply)\n",
                            t == 1 ? "GoPro" : "DJI");
            return;
        }

        if (strncmp(rest, "disarm_delay ", 13) == 0) {
            const char *val = rest + 13;
            while (*val == ' ') val++;
            _cfg.disarmStopDelayMs = static_cast<uint32_t>(strtoul(val, nullptr, 10));
            save();
            _serial->printf("[cfg] disarm_delay = %u ms (saved)\n", _cfg.disarmStopDelayMs);
            return;
        }

        if (strncmp(rest, "stop_on_disarm ", 15) == 0) {
            const char *val = rest + 15;
            while (*val == ' ') val++;
            _cfg.stopOnDisarm = (strtoul(val, nullptr, 10) != 0);
            save();
            _serial->printf("[cfg] stop_on_disarm = %s (saved)\n", _cfg.stopOnDisarm ? "true" : "false");
            return;
        }

        if (strncmp(rest, "aux_channel ", 12) == 0) {
            const char *val = rest + 12;
            while (*val == ' ') val++;
            uint8_t ch = static_cast<uint8_t>(strtoul(val, nullptr, 10));
            if (ch > 12) {
                _serial->println("[cfg] aux_channel must be 0-12");
                return;
            }
            _cfg.auxChannel = ch;
            save();
            if (ch == 0)
                _serial->println("[cfg] aux_channel = disabled (saved)");
            else
                _serial->printf("[cfg] aux_channel = AUX%u (saved)\n", ch);
            return;
        }

        if (strncmp(rest, "aux_mode ", 9) == 0) {
            const char *val = rest + 9;
            while (*val == ' ') val++;
            _cfg.auxMode = static_cast<uint8_t>(strtoul(val, nullptr, 0));  // 0=auto-detect base
            save();
            _serial->printf("[cfg] aux_mode = 0x%02X (saved)\n", _cfg.auxMode);
            return;
        }

        _serial->printf("[cfg] Unknown setting: %s\n", rest);
        return;
    }

    if (_len > 0)
        _serial->printf("[cfg] Unknown command: %s\n", line);
}

void ConfigManager::update() {
    if (!_serial) return;
    while (_serial->available()) {
        const char c = static_cast<char>(_serial->read());
        if (c == '\r') continue;
        if (c == '\n') {
            _buf[_len] = '\0';
            handleLine(_buf);
            _len = 0;
        } else if (_len < sizeof(_buf) - 1) {
            _buf[_len++] = c;
        }
    }
}
