#pragma once

#include <WebServer.h>
#include "config_manager.h"

// WiFi AP + HTTP configuration server.
//
// Call begin() to start the AP and web server.
// Call update() every loop iteration while running.
// Call stop() when a camera connects.
class WebConfigServer {
public:
    void begin(ConfigManager &cfg, Stream *dbg = nullptr);
    void stop();
    bool isRunning() const { return _running; }
    void update();

private:
    WebServer      _server{80};
    ConfigManager *_cfg     = nullptr;
    Stream        *_dbg     = nullptr;
    bool           _running = false;

    void handleRoot();
    void handleGetConfig();
    void handlePostConfig();
    void handleCli();
};
