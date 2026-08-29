#include <Arduino.h>
#include "config.h"
#include "config_manager.h"
#include "camera_registry.h"
#include "camera.h"
#include "ble_camera.h"
#include "gopro_camera.h"
#include "caddx_camera.h"
#include "sony_camera.h"
#include "blackmagic_camera.h"
#include "insta360_camera.h"
#include "msp_serial.h"
#include "dji_protocol.h"
#include "web_server.h"

static BLECamera       djiCamera;
static GoProCamera     goProCamera;
static CaddxCamera     caddxCamera;
static SonyCamera      sonyCamera;
static BlackmagicCamera blackmagicCamera;
static Insta360Camera  insta360Camera;
static Camera         *activeCamera = nullptr;

static CameraRegistry   cameraRegistry;
static MSPSerial        mspSerial;
static ConfigManager    configManager;
static WebConfigServer  webServer;

static volatile bool hasCamera = false;
static CameraData    currentCamera{};
static uint32_t      lastBattMs = 0;

static bool     pendingStop = false;
static uint32_t disarmMs   = 0;

// Timestamp used to compute the WiFi AP start delay.
// Reset to millis() whenever a camera disconnects so the countdown restarts.
static uint32_t wifiDelayOriginMs  = 0;
static bool     cameraWasConnected = false;

// Force-AP: set when BOOT button is held for WIFI_FORCE_AP_HOLD_MS.
// Once set, the AP stays on until the next reboot even if a camera connects.
static bool     forceAP        = false;
static uint32_t bootBtnPressMs = 0;  // millis() when button first went LOW
static uint32_t bootBtnLogSec  = 0;  // last second logged during hold

// Status LED: solid when camera connected, single blink every
// STATUS_LED_FLASH_PERIOD_MS when the WiFi AP is active, off otherwise.
static bool ledState = false;

static void updateStatusLed(uint32_t now, bool camConnected, bool apRunning) {
    bool shouldBeOn;
    if (apRunning)
        shouldBeOn = (now % STATUS_LED_AP_PERIOD_MS) < STATUS_LED_AP_ON_MS;
    else if (camConnected)
        shouldBeOn = true;
    else
        shouldBeOn = (now % STATUS_LED_SCAN_PERIOD_MS) < STATUS_LED_SCAN_ON_MS;

    if (shouldBeOn == ledState) return;
    ledState = shouldBeOn;
#if STATUS_LED_ACTIVE_LOW
    digitalWrite(STATUS_LED_PIN, ledState ? LOW : HIGH);
#else
    digitalWrite(STATUS_LED_PIN, ledState ? HIGH : LOW);
#endif
}

static void onAuxSwitch(bool high) {
    const bool isGoPro = (configManager.config().cameraType == 1);
    if (isGoPro && currentCamera.recording) {
        if (high) {
            DBG_SERIAL.println("[main] AUX high + GoPro recording → Burst Slo-Mo");
            activeCamera->triggerBurstSloMo();
        } else {
            DBG_SERIAL.println("[main] AUX low + GoPro recording → Standard");
            activeCamera->exitBurstSloMo();
        }
        return;
    }
    const uint8_t mode = high ? configManager.config().auxMode : DJI_MODE_VIDEO;
    DBG_SERIAL.printf("[main] AUX %s → camera mode 0x%02X\n", high ? "high" : "low", mode);
    activeCamera->switchCameraMode(mode);
}

// Called from the BLE stack task — copy + flag only; MSP output on main task.
static void onCameraData(const CameraData &data) {
    currentCamera = data;
    hasCamera     = true;
}

// Called from mspSerial.update() whenever the FC arm state changes.
static void onArmStateChange(bool armed) {
    if (armed) {
        pendingStop = false;
        DBG_SERIAL.println("[main] FC armed — starting recording");
        activeCamera->startRecording();
    } else {
        DBG_SERIAL.println("[main] FC disarmed");
        if (!configManager.config().stopOnDisarm) {
            DBG_SERIAL.println("[main] stop_on_disarm disabled — keeping recording");
        } else {
            const uint32_t delay = configManager.config().disarmStopDelayMs;
            if (delay == 0) {
                DBG_SERIAL.println("[main] stopping recording");
                activeCamera->stopRecording();
            } else {
                DBG_SERIAL.printf("[main] stopping recording in %u ms\n", delay);
                pendingStop = true;
                disarmMs   = millis();
            }
        }
    }
}

void setup() {
    DBG_SERIAL.begin(DBG_BAUD);
    BF_SERIAL.begin(BF_BAUD, SERIAL_8N1, BF_RX_PIN, BF_TX_PIN);

    cameraRegistry.begin();

    configManager.begin(DBG_SERIAL);
    configManager.setRegistry(&cameraRegistry);

    const uint8_t camType = configManager.config().cameraType;
    const char *camTypeName;
    switch (camType) {
        case 1:  activeCamera = &goProCamera; camTypeName = "GoPro";      break;
        case 2:  activeCamera = &caddxCamera; camTypeName = "Caddx Orca"; break;
        case 3:  activeCamera = &sonyCamera;       camTypeName = "Sony Alpha"; break;
        case 4:  activeCamera = &blackmagicCamera; camTypeName = "Blackmagic"; break;
        case 5:  activeCamera = &insta360Camera;   camTypeName = "Insta360";   break;
        default: activeCamera = &djiCamera;        camTypeName = "DJI Action"; break;
    }

    DBG_SERIAL.printf("[main] Camera type: %s\n", camTypeName);
    DBG_SERIAL.printf("[main] MSP output: TX=GPIO%d @ %u baud\n",
                      BF_TX_PIN, BF_BAUD);
    DBG_SERIAL.printf("[main] WiFi AP '%s' starts in %u s if no camera connects\n",
                      WIFI_AP_SSID, WIFI_AP_START_DELAY_MS / 1000);

    pinMode(WIFI_FORCE_AP_PIN, INPUT_PULLUP);
    pinMode(STATUS_LED_PIN, OUTPUT);
#if STATUS_LED_ACTIVE_LOW
    digitalWrite(STATUS_LED_PIN, HIGH); // active-LOW: HIGH = off
#else
    digitalWrite(STATUS_LED_PIN, LOW);
#endif

    mspSerial.begin(BF_SERIAL);
    mspSerial.setArmCallback(onArmStateChange);
    mspSerial.setAuxSwitchCallback(onAuxSwitch);

    // Caddx has no BLE scan/pairing flow or camera-match-mode fallback logic —
    // it just joins a Wi-Fi network directly — but it does still use the
    // registry, to remember multiple Orcas by SSID/password and read
    // whichever is preferred at begin() (see caddx_camera.h).
    djiCamera.setRegistry(&cameraRegistry);
    goProCamera.setRegistry(&cameraRegistry);
    caddxCamera.setRegistry(&cameraRegistry);
    sonyCamera.setRegistry(&cameraRegistry);
    blackmagicCamera.setRegistry(&cameraRegistry);
    insta360Camera.setRegistry(&cameraRegistry);

    const uint8_t matchMode = configManager.config().cameraMatchMode;
    djiCamera.setMatchMode(matchMode);
    goProCamera.setMatchMode(matchMode);
    sonyCamera.setMatchMode(matchMode);
    blackmagicCamera.setMatchMode(matchMode);
    insta360Camera.setMatchMode(matchMode);

    // Wake guard only affects GoPro (the only backend with a known
    // sleep/awake advertisement format), but it's harmless to set on all.
    const bool wakeGuard = configManager.config().cameraWakeGuard;
    djiCamera.setWakeGuard(wakeGuard);
    goProCamera.setWakeGuard(wakeGuard);
    sonyCamera.setWakeGuard(wakeGuard);
    blackmagicCamera.setWakeGuard(wakeGuard);
    insta360Camera.setWakeGuard(wakeGuard);

    const bool debugBle = configManager.config().debugBle;
    djiCamera.setDebugBle(debugBle);
    goProCamera.setDebugBle(debugBle);
    sonyCamera.setDebugBle(debugBle);
    blackmagicCamera.setDebugBle(debugBle);
    insta360Camera.setDebugBle(debugBle);

    // Low power mode sets BLE TX power to its minimum on the BLE backends
    // and, for Caddx (the only Wi-Fi backend), Wi-Fi TX power, to reduce RF
    // interference with the flight controller's RC receiver — harmless to
    // set on all since each begin() only reads its own relevant radio.
    const bool lowPowerMode = configManager.config().lowPowerMode;
    djiCamera.setLowPowerMode(lowPowerMode);
    goProCamera.setLowPowerMode(lowPowerMode);
    caddxCamera.setLowPowerMode(lowPowerMode);
    sonyCamera.setLowPowerMode(lowPowerMode);
    blackmagicCamera.setLowPowerMode(lowPowerMode);
    insta360Camera.setLowPowerMode(lowPowerMode);

    activeCamera->setCameraCallback(onCameraData);
    configManager.setCamera(activeCamera, &currentCamera);
    activeCamera->begin();

    wifiDelayOriginMs = millis();
}

void loop() {
    configManager.update();
    mspSerial.setAuxChannel(configManager.config().auxChannel);
    activeCamera->setDebugBle(configManager.config().debugBle);
    activeCamera->update();
    mspSerial.update();

    const uint32_t now          = millis();
    const bool     camConnected = activeCamera->isConnected();

    // ── BOOT button → force AP ─────────────────────────────────────────────────
    if (!forceAP) {
        if (digitalRead(WIFI_FORCE_AP_PIN) == LOW) {
            if (bootBtnPressMs == 0) {
                bootBtnPressMs = now;
                bootBtnLogSec  = 0;
                DBG_SERIAL.printf("[wifi] BOOT held — keep holding for %u s to force AP\n",
                                  WIFI_FORCE_AP_HOLD_MS / 1000);
            }
            uint32_t heldMs  = now - bootBtnPressMs;
            uint32_t heldSec = heldMs / 1000;
            if (heldSec != bootBtnLogSec && heldSec > 0) {
                bootBtnLogSec = heldSec;
                DBG_SERIAL.printf("[wifi] BOOT holding... %u/%u s\n",
                                  heldSec, WIFI_FORCE_AP_HOLD_MS / 1000);
            }
            if (heldMs >= WIFI_FORCE_AP_HOLD_MS) {
                forceAP        = true;
                bootBtnPressMs = 0;
                DBG_SERIAL.println("[wifi] Forcing WiFi AP mode until reboot");
                if (!webServer.isRunning())
                    webServer.begin(configManager, &cameraRegistry, &DBG_SERIAL);
            }
        } else {
            bootBtnPressMs = 0;
        }
    }

    // ── WiFi AP lifecycle ──────────────────────────────────────────────────────
    if (camConnected && !cameraWasConnected) {
        // Camera just connected — tear down the AP unless forced on.
        if (webServer.isRunning() && !forceAP) {
            DBG_SERIAL.println("[wifi] Camera connected — stopping AP");
            webServer.stop();
        }
    }

    if (!camConnected && cameraWasConnected && !forceAP) {
        // Camera just disconnected — restart the countdown.
        DBG_SERIAL.printf("[wifi] Camera disconnected — AP starts in %u s\n",
                          WIFI_AP_START_DELAY_MS / 1000);
        wifiDelayOriginMs = now;
    }

    cameraWasConnected = camConnected;

    if (!webServer.isRunning() &&
        !forceAP &&
        !camConnected &&
        WIFI_AP_START_DELAY_MS > 0 &&
        (now - wifiDelayOriginMs) >= WIFI_AP_START_DELAY_MS) {
        webServer.begin(configManager, &cameraRegistry, &DBG_SERIAL);
    }

    webServer.update();

    // ── Delayed recording stop ─────────────────────────────────────────────────
    if (pendingStop && (now - disarmMs) >= configManager.config().disarmStopDelayMs) {
        pendingStop = false;
        DBG_SERIAL.println("[main] stopping recording (delayed)");
        activeCamera->stopRecording();
    }

    // ── Camera telemetry ───────────────────────────────────────────────────────
    const bool battKeepalive =
        activeCamera->isConnected() &&
        MSP_BATTERY_KEEPALIVE_MS > 0 &&
        (now - lastBattMs) >= MSP_BATTERY_KEEPALIVE_MS;

    if (hasCamera || battKeepalive) {
        hasCamera  = false;
        const auto &cfg = configManager.config();
        mspSerial.sendCameraStatus(currentCamera);
        if (cfg.bf45Compat) {
            // Betaflight 4.5 has no Custom Message 1-4 OSD fields — sending
            // them would just be ignored, so use Pilot Name/Craft Name instead.
            if (cfg.pilotNameEnabled) mspSerial.sendPilotName(currentCamera, cfg.pilotNameTpl);
            if (cfg.craftNameEnabled) mspSerial.sendCraftName(currentCamera, cfg.craftNameTpl);
        } else {
            mspSerial.sendCustomOSD1(currentCamera, cfg.osd1Tpl);
            mspSerial.sendCustomOSD2(currentCamera, cfg.osd2Tpl);
            mspSerial.sendCustomOSD3(currentCamera, cfg.osd3Tpl);
            mspSerial.sendCustomOSD4(currentCamera, cfg.osd4Tpl);
        }
        lastBattMs = now;

        DBG_SERIAL.printf("[cam] bat=%u%%  mode=0x%02X  rec=%s  eis=%u  "
                          "time=%us  sd=%uMB  remain=%us  temp=%u\n",
                          currentCamera.percent, currentCamera.camera_mode,
                          currentCamera.recording ? "yes" : "no",
                          currentCamera.eis_mode, currentCamera.record_time,
                          currentCamera.remain_cap_mb, currentCamera.remain_time,
                          currentCamera.temp_over);
    }

    updateStatusLed(now, camConnected, webServer.isRunning());

    delay(10);
}
