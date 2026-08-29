#include "config_manager.h"
#include "camera_registry.h"
#include "camera.h"
#include "config.h"
#include <WiFi.h>
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
static constexpr const char *KEY_CMM  = "cam_match";
static constexpr const char *KEY_WAKE = "wake_guard";
static constexpr const char *KEY_DBG  = "debug_ble";
static constexpr const char *KEY_LPM  = "low_power";
static constexpr const char *KEY_WAP_DELAY = "wifi_ap_delay";
static constexpr const char *KEY_WAP_EN    = "wifi_ap_en";
static constexpr const char *KEY_BF45  = "bf45_compat";
static constexpr const char *KEY_PILOT_EN = "pilot_en";
static constexpr const char *KEY_PILOT = "pilot_tpl";
static constexpr const char *KEY_CRAFT_EN = "craft_en";
static constexpr const char *KEY_CRAFT = "craft_tpl";

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
    _cfg.cameraMatchMode   = static_cast<uint8_t>(_prefs.getUInt(KEY_CMM, DEFAULT_CAMERA_MATCH_MODE));
    _cfg.cameraWakeGuard   = _prefs.getBool(KEY_WAKE, DEFAULT_CAMERA_WAKE_GUARD);
    _cfg.debugBle          = _prefs.getBool(KEY_DBG, DEFAULT_DEBUG_BLE);
    _cfg.lowPowerMode      = _prefs.getBool(KEY_LPM, DEFAULT_LOW_POWER_MODE);
    _cfg.wifiApStartDelaySec = _prefs.getUInt(KEY_WAP_DELAY, DEFAULT_WIFI_AP_START_DELAY_SEC);
    _cfg.wifiApEnabled       = _prefs.getBool(KEY_WAP_EN, DEFAULT_WIFI_AP_ENABLED);
    loadStr(_prefs, KEY_OSD1, _cfg.osd1Tpl, sizeof(_cfg.osd1Tpl), DEFAULT_OSD1_TPL);
    loadStr(_prefs, KEY_OSD2, _cfg.osd2Tpl, sizeof(_cfg.osd2Tpl), DEFAULT_OSD2_TPL);
    loadStr(_prefs, KEY_OSD3, _cfg.osd3Tpl, sizeof(_cfg.osd3Tpl), DEFAULT_OSD3_TPL);
    loadStr(_prefs, KEY_OSD4, _cfg.osd4Tpl, sizeof(_cfg.osd4Tpl), DEFAULT_OSD4_TPL);
    _cfg.bf45Compat        = _prefs.getBool(KEY_BF45, DEFAULT_BF45_COMPAT);
    _cfg.pilotNameEnabled  = _prefs.getBool(KEY_PILOT_EN, DEFAULT_PILOT_NAME_ENABLED);
    loadStr(_prefs, KEY_PILOT, _cfg.pilotNameTpl, sizeof(_cfg.pilotNameTpl), DEFAULT_PILOT_NAME_TPL);
    _cfg.craftNameEnabled  = _prefs.getBool(KEY_CRAFT_EN, DEFAULT_CRAFT_NAME_ENABLED);
    loadStr(_prefs, KEY_CRAFT, _cfg.craftNameTpl, sizeof(_cfg.craftNameTpl), DEFAULT_CRAFT_NAME_TPL);
}

void ConfigManager::save() {
    _prefs.putUInt(KEY_DSD, _cfg.disarmStopDelayMs);
    _prefs.putBool(KEY_SOD, _cfg.stopOnDisarm);
    _prefs.putUInt(KEY_ACH, _cfg.auxChannel);
    _prefs.putUInt(KEY_AMD, _cfg.auxMode);
    _prefs.putUInt(KEY_CAM, _cfg.cameraType);
    _prefs.putUInt(KEY_CMM, _cfg.cameraMatchMode);
    _prefs.putBool(KEY_WAKE, _cfg.cameraWakeGuard);
    _prefs.putBool(KEY_DBG, _cfg.debugBle);
    _prefs.putBool(KEY_LPM, _cfg.lowPowerMode);
    _prefs.putUInt(KEY_WAP_DELAY, _cfg.wifiApStartDelaySec);
    _prefs.putBool(KEY_WAP_EN, _cfg.wifiApEnabled);
    _prefs.putString(KEY_OSD1, _cfg.osd1Tpl);
    _prefs.putString(KEY_OSD2, _cfg.osd2Tpl);
    _prefs.putString(KEY_OSD3, _cfg.osd3Tpl);
    _prefs.putString(KEY_OSD4, _cfg.osd4Tpl);
    _prefs.putBool(KEY_BF45, _cfg.bf45Compat);
    _prefs.putBool(KEY_PILOT_EN, _cfg.pilotNameEnabled);
    _prefs.putString(KEY_PILOT, _cfg.pilotNameTpl);
    _prefs.putBool(KEY_CRAFT_EN, _cfg.craftNameEnabled);
    _prefs.putString(KEY_CRAFT, _cfg.craftNameTpl);
}

static const char *cameraTypeName(uint8_t t) {
    switch (t) {
        case 1:  return "GoPro";
        case 2:  return "Caddx";
        case 3:  return "Sony";
        case 4:  return "Blackmagic";
        case 5:  return "Insta360";
        default: return "DJI";
    }
}

void ConfigManager::printAll(Stream &out) {
    out.printf("[cfg] camera_type     = %s\n", cameraTypeName(_cfg.cameraType));
    static const char *matchModeNames[] = {"fallback", "strict", "best_signal"};
    out.printf("[cfg] camera_match    = %s\n",
               _cfg.cameraMatchMode <= 2 ? matchModeNames[_cfg.cameraMatchMode] : "?");
    out.printf("[cfg] wake_guard      = %s\n", _cfg.cameraWakeGuard ? "true" : "false");
    out.printf("[cfg] disarm_delay    = %u ms\n", _cfg.disarmStopDelayMs);
    out.printf("[cfg] stop_on_disarm  = %s\n", _cfg.stopOnDisarm ? "true" : "false");
    if (_cfg.auxChannel == 0)
        out.println("[cfg] aux_channel     = disabled");
    else
        out.printf("[cfg] aux_channel     = AUX%u\n", _cfg.auxChannel);
    out.printf("[cfg] aux_mode        = 0x%02X\n", _cfg.auxMode);
    out.printf("[cfg] debug_ble       = %s\n", _cfg.debugBle ? "true" : "false");
    out.printf("[cfg] low_power       = %s\n", _cfg.lowPowerMode ? "true" : "false");
    out.printf("[cfg] wifi_ap_enabled = %s\n", _cfg.wifiApEnabled ? "true" : "false");
    out.printf("[cfg] wifi_ap_delay   = %u s\n", _cfg.wifiApStartDelaySec);
    out.printf("[cfg] osd1            = %s\n", _cfg.osd1Tpl);
    out.printf("[cfg] osd2            = %s\n", _cfg.osd2Tpl);
    out.printf("[cfg] osd3            = %s\n", _cfg.osd3Tpl);
    out.printf("[cfg] osd4            = %s\n", _cfg.osd4Tpl);
    out.printf("[cfg] bf45_compat     = %s\n", _cfg.bf45Compat ? "true" : "false");
    out.printf("[cfg] pilot_en        = %s\n", _cfg.pilotNameEnabled ? "true" : "false");
    out.printf("[cfg] pilot_tpl       = %s\n", _cfg.pilotNameTpl);
    out.printf("[cfg] craft_en        = %s\n", _cfg.craftNameEnabled ? "true" : "false");
    out.printf("[cfg] craft_tpl       = %s\n", _cfg.craftNameTpl);

    CameraEntry ce;
    bool haveCaddx = _registry && _registry->preferredEntry(/*Caddx=*/2, ce);
    out.printf("[cfg] caddx_ssid      = %s\n", haveCaddx ? ce.addr : "");
    out.printf("[cfg] caddx_pass      = %s\n",
               (haveCaddx && strlen(ce.pass)) ? "(set)" : "(not set — uses factory default)");
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
        out.println("  set camera_type <0-5>      - 0=DJI, 1=GoPro, 2=Caddx Orca, 3=Sony Alpha, 4=Blackmagic, 5=Insta360 (reboot required)");
        out.println("  set camera_match <0-2>     - 0=fallback (preferred, else any found), 1=strict (preferred only), 2=best_signal (strongest RSSI, ignores preferred)");
        out.println("  set wake_guard <0|1>       - 1=don't connect to a sleeping/powered-down GoPro (avoids waking it); bypassed for a manually selected camera");
        out.println("  set disarm_delay <ms>      - delay before stopping recording after disarm");
        out.println("  set stop_on_disarm <0|1>   - disable (0) or enable (1) stop on disarm");
        out.println("  set aux_channel <0-12>     - AUX channel for camera mode switch (0=off)");
        out.println("  set aux_mode <0x00-0xFF>   - camera mode when AUX high (0x00=slow_motion 0x01=video 0x0A=hyperlapse)");
        out.println("  set debug_ble <0|1>        - log raw BLE TX/RX packets to the serial console");
        out.println("  set low_power <0|1>        - 1=minimum BLE/Wi-Fi TX power (default) to reduce RC receiver interference, shorter range; 0=maximum TX power (reboot required)");
        out.println("  set wifi_ap_enabled <0|1>  - 0=never auto-start the config-portal AP (BOOT-button force-AP still works)");
        out.println("  set wifi_ap_delay <sec>    - seconds after boot/disconnect before the AP auto-starts (default 30)");
        out.println("  set osd1 <template>        - OSD Custom Message 1 template (default: battery)");
        out.println("  set osd2 <template>        - OSD Custom Message 2 template (default: recording)");
        out.println("  set osd3 <template>        - OSD Custom Message 3 template (default: settings)");
        out.println("  set osd4 <template>        - OSD Custom Message 4 template (default: storage)");
        out.println("  Tokens: {bat} {rec} {recdur} {mode} {res} {fps} {eis} {rleft} {rcap}");
        out.println("  set bf45_compat <0|1>      - 1=target Betaflight 4.5: send pilot_tpl/craft_tpl to Pilot Name/Craft Name, stop sending osd1-4 (no Custom Message fields on 4.5)");
        out.println("  set pilot_en <0|1>         - enable (1, default) or disable (0) sending Pilot Name, when bf45_compat=1");
        out.println("  set pilot_tpl <template>   - Pilot Name template, used only when bf45_compat=1 and pilot_en=1 (default: battery)");
        out.println("  set craft_en <0|1>         - enable (1, default) or disable (0) sending Craft Name, when bf45_compat=1");
        out.println("  set craft_tpl <template>   - Craft Name template, used only when bf45_compat=1 and craft_en=1 (default: recording state)");
        out.println("  set caddx_ssid <ssid>      - remember + select a Caddx Orca's Wi-Fi network (camera_type=2)");
        out.println("  set caddx_pass <password>  - password for the SSID above (tries factory default 12345678 first; reboot required)");
        out.println("  reset                      - restore defaults");
        out.println("  reboot                     - restart the ESP32");
        out.println("  status                     - report ESP32 and camera status");
        out.println("  record start               - start camera recording now");
        out.println("  record stop                - stop camera recording now");
        out.println("Camera list commands:");
        out.println("  cameras list               - list saved cameras");
        out.println("  cameras connect <idx>      - select camera for next connection");
        out.println("  cameras remove <idx>       - remove camera from list");
        out.println("  cameras clear              - remove all saved cameras");
        out.println("  wifi scan                  - scan for nearby Wi-Fi networks (find the Caddx Orca's SSID)");
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

    if (strcmp(line, "reboot") == 0) {
        out.println("[cfg] Rebooting...");
        out.flush();
        delay(100);  // let the message go out before the reset lands
        ESP.restart();
        return;
    }

    if (strcmp(line, "status") == 0) {
        const uint32_t upSec = millis() / 1000;
        out.printf("[status] FreeCLinker v%s  uptime=%02u:%02u:%02u  heap=%uB\n",
                   FIRMWARE_VERSION,
                   upSec / 3600, (upSec / 60) % 60, upSec % 60,
                   (unsigned)ESP.getFreeHeap());

        if ((WiFi.getMode() & WIFI_MODE_AP) != 0) {
            out.printf("[status] wifi_ap=running  ssid=%s  ip=%s\n",
                       WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
        } else {
            out.println("[status] wifi_ap=stopped");
        }

        if (!_camera) {
            out.println("[status] camera=unavailable");
            return;
        }
        out.printf("[status] camera_type=%s  connected=%s\n",
                   cameraTypeName(_cfg.cameraType), _camera->isConnected() ? "yes" : "no");
        if (_camera->isConnected() && _cameraData && _cameraData->valid) {
            const CameraData &d = *_cameraData;
            out.printf("[status]   battery=%u%%  recording=%s", d.percent, d.recording ? "yes" : "no");
            if (d.recording) out.printf(" (%us)", d.record_time);
            out.println();
            out.printf("[status]   mode=0x%02X  res=%u  fps=%u  eis=%u  temp_over=%u\n",
                       d.camera_mode, d.resolution, d.fps_idx, d.eis_mode, d.temp_over);
            out.printf("[status]   remain_time=%lus  remain_cap=%luMB\n",
                       (unsigned long)d.remain_time, (unsigned long)d.remain_cap_mb);
        }
        return;
    }

    if (strcmp(line, "record start") == 0 || strcmp(line, "record stop") == 0) {
        if (!_camera) {
            out.println("[status] camera=unavailable");
            return;
        }
        const bool start = (strcmp(line, "record start") == 0);
        const bool ok = start ? _camera->startRecording() : _camera->stopRecording();
        out.printf("[status] record %s: %s\n", start ? "start" : "stop", ok ? "ok" : "failed");
        return;
    }

    if (strncmp(line, "set ", 4) == 0) {
        const char *rest = line + 4;
        while (*rest == ' ') rest++;

        if (strncmp(rest, "camera_type ", 12) == 0) {
            const char *val = rest + 12;
            while (*val == ' ') val++;
            uint8_t t = static_cast<uint8_t>(strtoul(val, nullptr, 10));
            if (t > 5) {
                out.println("[cfg] camera_type must be 0 (DJI), 1 (GoPro), 2 (Caddx), 3 (Sony), 4 (Blackmagic), or 5 (Insta360)");
                return;
            }
            setCameraType(t);
            out.printf("[cfg] camera_type = %s (saved — reboot to apply)\n", cameraTypeName(t));
            return;
        }

        if (strncmp(rest, "camera_match ", 13) == 0) {
            const char *val = rest + 13;
            while (*val == ' ') val++;
            uint8_t m = static_cast<uint8_t>(strtoul(val, nullptr, 10));
            if (m > 2) {
                out.println("[cfg] camera_match must be 0 (fallback), 1 (strict), or 2 (best_signal)");
                return;
            }
            setCameraMatchMode(m);
            static const char *matchModeNames[] = {"fallback", "strict", "best_signal"};
            out.printf("[cfg] camera_match = %s (saved)\n", matchModeNames[m]);
            return;
        }

        if (strncmp(rest, "wake_guard ", 11) == 0) {
            const char *val = rest + 11;
            while (*val == ' ') val++;
            setCameraWakeGuard(strtoul(val, nullptr, 10) != 0);
            out.printf("[cfg] wake_guard = %s (saved)\n", _cfg.cameraWakeGuard ? "true" : "false");
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

        if (strncmp(rest, "debug_ble ", 10) == 0) {
            const char *val = rest + 10;
            while (*val == ' ') val++;
            setDebugBle(strtoul(val, nullptr, 10) != 0);
            out.printf("[cfg] debug_ble = %s (saved)\n", _cfg.debugBle ? "true" : "false");
            return;
        }

        if (strncmp(rest, "low_power ", 10) == 0) {
            const char *val = rest + 10;
            while (*val == ' ') val++;
            setLowPowerMode(strtoul(val, nullptr, 10) != 0);
            out.printf("[cfg] low_power = %s (saved — reboot to apply)\n", _cfg.lowPowerMode ? "true" : "false");
            return;
        }

        if (strncmp(rest, "wifi_ap_enabled ", 16) == 0) {
            const char *val = rest + 16;
            while (*val == ' ') val++;
            setWifiApEnabled(strtoul(val, nullptr, 10) != 0);
            out.printf("[cfg] wifi_ap_enabled = %s (saved)\n", _cfg.wifiApEnabled ? "true" : "false");
            return;
        }

        if (strncmp(rest, "wifi_ap_delay ", 14) == 0) {
            const char *val = rest + 14;
            while (*val == ' ') val++;
            setWifiApStartDelay(static_cast<uint32_t>(strtoul(val, nullptr, 10)));
            out.printf("[cfg] wifi_ap_delay = %u s (saved)\n", _cfg.wifiApStartDelaySec);
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

        if (strncmp(rest, "bf45_compat ", 12) == 0) {
            const char *val = rest + 12;
            while (*val == ' ') val++;
            setBf45Compat(strtoul(val, nullptr, 10) != 0);
            out.printf("[cfg] bf45_compat = %s (saved)\n", _cfg.bf45Compat ? "true" : "false");
            return;
        }

        if (strncmp(rest, "pilot_en ", 9) == 0) {
            const char *val = rest + 9;
            while (*val == ' ') val++;
            setPilotNameEnabled(strtoul(val, nullptr, 10) != 0);
            out.printf("[cfg] pilot_en = %s (saved)\n", _cfg.pilotNameEnabled ? "true" : "false");
            return;
        }

        if (strncmp(rest, "pilot_tpl ", 10) == 0) {
            setPilotNameTemplate(rest + 10);
            out.printf("[cfg] pilot_tpl = %s (saved)\n", _cfg.pilotNameTpl);
            return;
        }

        if (strncmp(rest, "craft_en ", 9) == 0) {
            const char *val = rest + 9;
            while (*val == ' ') val++;
            setCraftNameEnabled(strtoul(val, nullptr, 10) != 0);
            out.printf("[cfg] craft_en = %s (saved)\n", _cfg.craftNameEnabled ? "true" : "false");
            return;
        }

        if (strncmp(rest, "craft_tpl ", 10) == 0) {
            setCraftNameTemplate(rest + 10);
            out.printf("[cfg] craft_tpl = %s (saved)\n", _cfg.craftNameTpl);
            return;
        }

        if (strncmp(rest, "caddx_ssid ", 11) == 0) {
            if (setCaddxSsid(rest + 11))
                out.printf("[cfg] caddx_ssid = %s (saved to camera list — reboot to apply)\n", rest + 11);
            else
                out.println("[cfg] caddx_ssid: value required");
            return;
        }

        if (strncmp(rest, "caddx_pass ", 11) == 0) {
            if (setCaddxPass(rest + 11))
                out.println("[cfg] caddx_pass = (set) (saved to camera list — reboot to apply)");
            else
                out.println("[cfg] caddx_pass: set caddx_ssid first");
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

    if (strncmp(line, "wifi", 4) == 0) {
        const char *sub = line + 4;
        while (*sub == ' ') sub++;
        handleWifiCmd(sub, out);
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
            // Caddx reads the registry's preferred entry directly at
            // begin() — selectCamera() above is all it needs — but unlike
            // BLE/GoPro it has no live rescan to act on a selection while
            // running, so it only takes effect after a reboot.
            out.printf("[reg] Camera %u selected: \"%s\" %s — will connect on next %s\n",
                       idx, e.name, e.addr, e.cameraType == 2 ? "reboot" : "scan");
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

// Blocking scan (a few seconds) — fine for a manual, setup-time "wifi scan"
// command, but will briefly interrupt any Caddx camera Wi-Fi traffic in
// progress at the time. WIFI_MODE_APSTA keeps the config-portal AP (if any)
// alive across the scan, same coexistence mode CaddxCamera itself uses.
void ConfigManager::handleWifiCmd(const char *sub, Stream &out) {
    if (strcmp(sub, "scan") == 0 || strlen(sub) == 0) {
        if (WiFi.getMode() != WIFI_MODE_APSTA) {
            WiFi.mode(WIFI_MODE_APSTA);
            delay(100);  // let the STA interface come up before scanning, or
                         // scanNetworks() can return WIFI_SCAN_FAILED
        }
        out.println("[wifi] Scanning...");
        int n = WiFi.scanNetworks();
        if (n < 0) {  // transient WIFI_SCAN_FAILED/RUNNING — one retry usually clears it
            delay(200);
            n = WiFi.scanNetworks();
        }
        if (n <= 0) {
            out.println("[wifi] No networks found");
        } else {
            out.printf("[wifi] %d network(s) found:\n", n);
            for (int i = 0; i < n; i++) {
                out.printf("[wifi]  %2d: %-32s RSSI=%-4d %s\n", i,
                           WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                           WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "SEC");
            }
        }
        WiFi.scanDelete();
        return;
    }

    out.println("Wi-Fi commands:");
    out.println("  wifi scan   - scan for nearby Wi-Fi networks (find the Caddx Orca's SSID)");
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

void ConfigManager::setCameraMatchMode(uint8_t v) {
    _cfg.cameraMatchMode = v;
    _prefs.putUInt(KEY_CMM, v);
}

void ConfigManager::setCameraWakeGuard(bool v) {
    _cfg.cameraWakeGuard = v;
    _prefs.putBool(KEY_WAKE, v);
}

void ConfigManager::setDebugBle(bool v) {
    _cfg.debugBle = v;
    _prefs.putBool(KEY_DBG, v);
}

void ConfigManager::setLowPowerMode(bool v) {
    _cfg.lowPowerMode = v;
    _prefs.putBool(KEY_LPM, v);
}

void ConfigManager::setWifiApStartDelay(uint32_t sec) {
    _cfg.wifiApStartDelaySec = sec;
    _prefs.putUInt(KEY_WAP_DELAY, sec);
}

void ConfigManager::setWifiApEnabled(bool v) {
    _cfg.wifiApEnabled = v;
    _prefs.putBool(KEY_WAP_EN, v);
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

void ConfigManager::setBf45Compat(bool v) {
    _cfg.bf45Compat = v;
    _prefs.putBool(KEY_BF45, v);
}

void ConfigManager::setPilotNameEnabled(bool v) {
    _cfg.pilotNameEnabled = v;
    _prefs.putBool(KEY_PILOT_EN, v);
}

void ConfigManager::setPilotNameTemplate(const char *tpl) {
    strlcpy(_cfg.pilotNameTpl, tpl, OSD_TPL_LEN);
    _prefs.putString(KEY_PILOT, _cfg.pilotNameTpl);
}

void ConfigManager::setCraftNameEnabled(bool v) {
    _cfg.craftNameEnabled = v;
    _prefs.putBool(KEY_CRAFT_EN, v);
}

void ConfigManager::setCraftNameTemplate(const char *tpl) {
    strlcpy(_cfg.craftNameTpl, tpl, OSD_TPL_LEN);
    _prefs.putString(KEY_CRAFT, _cfg.craftNameTpl);
}

bool ConfigManager::setCaddxSsid(const char *ssid) {
    if (!_registry || !ssid || !ssid[0]) return false;
    // Same upsert-and-select path BLE/GoPro use after a live connection —
    // Caddx just calls it manually since there's no discovery step to
    // trigger it automatically. Omitting `pass` here preserves whatever
    // password (if any) an existing entry for this SSID already has.
    _registry->onConnected(ssid, ssid, /*addrType=*/0, /*Caddx=*/2);
    return true;
}

bool ConfigManager::setCaddxPass(const char *pass) {
    if (!_registry) return false;
    int idx = (_registry->selectedIdx() >= 0) ? _registry->selectedIdx() : _registry->lastIdx();
    CameraEntry e;
    if (idx < 0 || !_registry->getEntry((uint8_t)idx, e) || e.cameraType != 2) return false;
    return _registry->setPassword((uint8_t)idx, pass);
}
