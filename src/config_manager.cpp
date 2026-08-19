#include "config_manager.h"
#include <cstring>
#include <cstdlib>

static constexpr const char *NVS_NS  = "bridge";
static constexpr const char *KEY_DSD = "disarm_delay";
static constexpr const char *KEY_SOD = "stop_on_disarm";

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
}

void ConfigManager::save() {
    _prefs.putUInt(KEY_DSD, _cfg.disarmStopDelayMs);
    _prefs.putBool(KEY_SOD, _cfg.stopOnDisarm);
}

void ConfigManager::printAll() {
    _serial->printf("[cfg] disarm_delay    = %u ms\n", _cfg.disarmStopDelayMs);
    _serial->printf("[cfg] stop_on_disarm  = %s\n", _cfg.stopOnDisarm ? "true" : "false");
}

void ConfigManager::handleLine(const char *line) {
    while (*line == ' ') line++;

    if (strcmp(line, "help") == 0) {
        _serial->println("Commands:");
        _serial->println("  show                   - print all settings");
        _serial->println("  set disarm_delay <ms>    - delay before stopping recording after disarm");
        _serial->println("  set stop_on_disarm <0|1> - disable (0) or enable (1) stop on disarm");
        _serial->println("  reset                  - restore defaults");
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
