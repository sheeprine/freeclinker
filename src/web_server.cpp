#include "web_server.h"
#include "camera_registry.h"
#include "web_content.h"
#include "config.h"

#include <WiFi.h>
#include <ArduinoJson.h>

// Accumulates print() output into a String so CLI commands can be
// processed synchronously and their output returned as HTTP body text.
class StringStream : public Stream {
public:
    int    available() override { return 0; }
    int    read()      override { return -1; }
    int    peek()      override { return -1; }
    size_t write(uint8_t c) override { _buf += (char)c; return 1; }
    size_t write(const uint8_t *b, size_t n) override {
        for (size_t i = 0; i < n; i++) _buf += (char)b[i];
        return n;
    }
    const String &str() const { return _buf; }
private:
    String _buf;
};

// ── Public interface ──────────────────────────────────────────────────────────

void WebConfigServer::begin(ConfigManager &cfg, CameraRegistry *reg, Stream *dbg) {
    _cfg = &cfg;
    _reg = reg;
    _dbg = dbg;

    WiFi.softAP(WIFI_AP_SSID, strlen(WIFI_AP_PASSWORD) ? WIFI_AP_PASSWORD : nullptr,
                WIFI_AP_CHANNEL);

    if (_dbg) {
        _dbg->printf("[wifi] AP started — SSID: %s  IP: %s\n",
                     WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
    }

    _server.on("/",            HTTP_GET,  [this]() { handleRoot();        });
    _server.on("/api/config",  HTTP_GET,  [this]() { handleGetConfig();   });
    _server.on("/api/config",  HTTP_POST, [this]() { handlePostConfig();  });
    _server.on("/api/cameras", HTTP_GET,  [this]() { handleGetCameras();  });
    _server.on("/api/cli",     HTTP_POST, [this]() { handleCli();         });

    _server.begin();
    _running = true;
}

void WebConfigServer::stop() {
    if (!_running) return;
    _server.stop();
    WiFi.softAPdisconnect(true);
    _running = false;
    if (_dbg) _dbg->println("[wifi] AP stopped");
}

void WebConfigServer::update() {
    if (_running) _server.handleClient();
}

// ── Route handlers ────────────────────────────────────────────────────────────

void WebConfigServer::handleRoot() {
    _server.send_P(200, "text/html", WEB_INDEX_HTML);
}

void WebConfigServer::handleGetConfig() {
    const auto &c = _cfg->config();
    JsonDocument doc;
    doc["camera_type"]    = c.cameraType;
    doc["disarm_delay"]   = c.disarmStopDelayMs;
    doc["stop_on_disarm"] = c.stopOnDisarm;
    doc["aux_channel"]    = c.auxChannel;
    doc["aux_mode"]       = c.auxMode;
    doc["osd1"]           = c.osd1Tpl;
    doc["osd2"]           = c.osd2Tpl;
    doc["osd3"]           = c.osd3Tpl;
    doc["osd4"]           = c.osd4Tpl;
    String json;
    serializeJson(doc, json);
    _server.send(200, "application/json", json);
}

void WebConfigServer::handlePostConfig() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "text/plain", "No body");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, _server.arg("plain"))) {
        _server.send(400, "text/plain", "JSON parse error");
        return;
    }

    if (doc["camera_type"].is<int>())     _cfg->setCameraType(doc["camera_type"].as<uint8_t>());
    if (doc["disarm_delay"].is<int>())    _cfg->setDisarmDelay(doc["disarm_delay"].as<uint32_t>());
    if (doc["stop_on_disarm"].is<bool>()) _cfg->setStopOnDisarm(doc["stop_on_disarm"].as<bool>());
    if (doc["aux_channel"].is<int>())     _cfg->setAuxChannel(doc["aux_channel"].as<uint8_t>());
    if (doc["aux_mode"].is<int>())        _cfg->setAuxMode(doc["aux_mode"].as<uint8_t>());
    if (doc["osd1"].is<const char *>())   _cfg->setOsdTemplate(1, doc["osd1"].as<const char *>());
    if (doc["osd2"].is<const char *>())   _cfg->setOsdTemplate(2, doc["osd2"].as<const char *>());
    if (doc["osd3"].is<const char *>())   _cfg->setOsdTemplate(3, doc["osd3"].as<const char *>());
    if (doc["osd4"].is<const char *>())   _cfg->setOsdTemplate(4, doc["osd4"].as<const char *>());

    handleGetConfig();  // return the updated state
}

void WebConfigServer::handleGetCameras() {
    String json = _reg ? _reg->toJson() : "[]";
    _server.send(200, "application/json", json);
}

void WebConfigServer::handleCli() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "text/plain", "No body");
        return;
    }
    StringStream out;
    _cfg->processCommand(_server.arg("plain").c_str(), out);
    _server.send(200, "text/plain", out.str());
}
