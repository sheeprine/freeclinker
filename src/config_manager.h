#pragma once

#include <Preferences.h>
#include <Stream.h>
#include <stdint.h>

class ConfigManager {
public:
    struct Config {
        uint32_t disarmStopDelayMs;  // delay between FC disarm and stopping recording
        bool     stopOnDisarm;       // false = never stop recording on disarm
    };

    static constexpr uint32_t DEFAULT_DISARM_STOP_DELAY_MS = 0;
    static constexpr bool     DEFAULT_STOP_ON_DISARM       = true;

    void begin(Stream &serial);
    void update();

    const Config &config() const { return _cfg; }

private:
    void load();
    void save();
    void printAll();
    void handleLine(const char *line);

    Preferences _prefs;
    Config      _cfg{};
    Stream     *_serial = nullptr;
    char        _buf[80];
    uint8_t     _len = 0;
};
