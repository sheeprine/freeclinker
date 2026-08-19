#include <Arduino.h>
#include "config.h"
#include "ble_camera.h"
#include "msp_serial.h"

static BLECamera bleCamera;
static MSPSerial mspSerial;

static volatile bool hasBattery = false;
static BatteryData   currentBattery{};
static uint32_t      lastBattMs = 0;

// Called from the BLE stack task — copy + flag only; MSP output on main task.
static void onBattery(const BatteryData &data) {
    currentBattery = data;
    hasBattery     = true;
}

// Called from mspSerial.update() whenever the FC arm state changes.
static void onArmStateChange(bool armed) {
    if (armed) {
        DBG_SERIAL.println("[main] FC armed — starting recording");
        bleCamera.startRecording();
    } else {
        DBG_SERIAL.println("[main] FC disarmed — stopping recording");
        bleCamera.stopRecording();
    }
}

void setup() {
    DBG_SERIAL.begin(DBG_BAUD);
    BF_SERIAL.begin(BF_BAUD, SERIAL_8N1, BF_RX_PIN, BF_TX_PIN);

    DBG_SERIAL.println("\n[main] DJI Action → Betaflight MSP bridge");
    DBG_SERIAL.printf ("[main] BLE target prefix: \"%s\"\n", DJI_DEVICE_NAME_PREFIX);
    DBG_SERIAL.printf ("[main] MSP output: UART2 TX=GPIO%d @ %u baud\n",
                       BF_TX_PIN, BF_BAUD);

    mspSerial.begin(BF_SERIAL);
    mspSerial.setArmCallback(onArmStateChange);

    bleCamera.setBatteryCallback(onBattery);
    bleCamera.begin();
}

void loop() {
    bleCamera.update();
    mspSerial.update();

    const uint32_t now = millis();

    // ── Battery ────────────────────────────────────────────────────────────
    const bool battKeepalive =
        bleCamera.isConnected() &&
        MSP_BATTERY_KEEPALIVE_MS > 0 &&
        (now - lastBattMs) >= MSP_BATTERY_KEEPALIVE_MS;

    if (hasBattery || battKeepalive) {
        hasBattery = false;
        mspSerial.sendCameraBattery(currentBattery);
        mspSerial.sendBatteryAsCustomMessage(currentBattery);
        lastBattMs = now;

        DBG_SERIAL.printf("[batt] %u%%  %umV  %dmA  %u/%umAh  %d°C  %ucell\n",
                          currentBattery.percent,
                          currentBattery.voltage,
                          currentBattery.current,
                          currentBattery.remaining,
                          currentBattery.capacity,
                          currentBattery.temperature,
                          currentBattery.cellCount);
    }

    delay(10);
}
