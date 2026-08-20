#include <Arduino.h>
#include "config.h"
#include "config_manager.h"
#include "ble_camera.h"
#include "msp_serial.h"
#include "dji_protocol.h"

static BLECamera     bleCamera;
static MSPSerial     mspSerial;
static ConfigManager configManager;

static volatile bool hasCamera = false;
static CameraData    currentCamera{};
static uint32_t      lastBattMs = 0;

static bool     pendingStop = false;
static uint32_t disarmMs   = 0;

static void onAuxSwitch(bool high) {
    const uint8_t mode = high ? configManager.config().auxMode : DJI_MODE_VIDEO;
    DBG_SERIAL.printf("[main] AUX %s → camera mode 0x%02X\n", high ? "high" : "low", mode);
    bleCamera.switchCameraMode(mode);
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
        bleCamera.startRecording();
    } else {
        DBG_SERIAL.println("[main] FC disarmed");
        if (!configManager.config().stopOnDisarm) {
            DBG_SERIAL.println("[main] stop_on_disarm disabled — keeping recording");
        } else {
            const uint32_t delay = configManager.config().disarmStopDelayMs;
            if (delay == 0) {
                DBG_SERIAL.println("[main] stopping recording");
                bleCamera.stopRecording();
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

    DBG_SERIAL.println("\n[main] DJI Action → Betaflight MSP bridge");
    DBG_SERIAL.printf ("[main] BLE target prefix: \"%s\"\n", DJI_DEVICE_NAME_PREFIX);
    DBG_SERIAL.printf ("[main] MSP output: UART2 TX=GPIO%d @ %u baud\n",
                       BF_TX_PIN, BF_BAUD);

    configManager.begin(DBG_SERIAL);

    mspSerial.begin(BF_SERIAL);
    mspSerial.setArmCallback(onArmStateChange);
    mspSerial.setAuxSwitchCallback(onAuxSwitch);

    bleCamera.setCameraCallback(onCameraData);
    bleCamera.begin();
}

void loop() {
    configManager.update();
    mspSerial.setAuxChannel(configManager.config().auxChannel);
    bleCamera.update();
    mspSerial.update();

    const uint32_t now = millis();

    // ── Delayed recording stop ─────────────────────────────────────────────
    if (pendingStop && (now - disarmMs) >= configManager.config().disarmStopDelayMs) {
        pendingStop = false;
        DBG_SERIAL.println("[main] stopping recording (delayed)");
        bleCamera.stopRecording();
    }

    // ── Camera telemetry ───────────────────────────────────────────────────
    const bool battKeepalive =
        bleCamera.isConnected() &&
        MSP_BATTERY_KEEPALIVE_MS > 0 &&
        (now - lastBattMs) >= MSP_BATTERY_KEEPALIVE_MS;

    if (hasCamera || battKeepalive) {
        hasCamera  = false;
        mspSerial.sendCameraStatus(currentCamera);
        mspSerial.sendBatteryMsg(currentCamera);
        mspSerial.sendRecordingMsg(currentCamera);
        lastBattMs = now;

        DBG_SERIAL.printf("[cam] bat=%u%%  mode=0x%02X  rec=%s  eis=%u  "
                          "time=%us  sd=%uMB  remain=%us  temp=%u\n",
                          currentCamera.percent, currentCamera.camera_mode,
                          currentCamera.recording ? "yes" : "no",
                          currentCamera.eis_mode, currentCamera.record_time,
                          currentCamera.remain_cap_mb, currentCamera.remain_time,
                          currentCamera.temp_over);
    }

    delay(10);
}
