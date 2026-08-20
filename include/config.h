#pragma once

// ─── BLE ─────────────────────────────────────────────────────────────────────

// Fallback scan filter used when a device lacks manufacturer data.
// The primary filter is the DJI manufacturer ID (0x08AA / pattern 0xFA).
#define DJI_DEVICE_NAME_PREFIX      "DJI Action"

// Duration of each BLE scan pass (seconds)
#define BLE_SCAN_DURATION_SECS      5

// Delay before retrying connect / rescan after any failure (ms)
#define BLE_RECONNECT_DELAY_MS      3000

// Service UUID, characteristic UUIDs, packet structs, and CRC constants are
// in src/dji_protocol.h — no separate configuration needed.

// Print all services/characteristics on first connect (set 0 in production)
#define DEBUG_PRINT_SERVICES        1

// ─── Betaflight serial (MSP port) ────────────────────────────────────────────
//
// Wire ESP32 TX17 → FC UART RX.  Only TX is needed.
//
// Betaflight CLI setup (replace N with the actual UART number):
//   serial N 0 115200 8 0 0 0   # set UART N to MSP
//   save
//
#define BF_SERIAL                   Serial2
#define BF_BAUD                     115200
#define BF_TX_PIN                   17
#define BF_RX_PIN                   16   // used to receive MSP responses from FC

// ─── Debug serial (USB) ──────────────────────────────────────────────────────
#define DBG_SERIAL                  Serial
#define DBG_BAUD                    115200

// How often to re-send the last battery frame when no new data arrives (ms).
// Camera pushes status at 2 Hz once subscribed; this keepalive only fires
// if the camera goes silent (sleep mode, etc.).
#define MSP_BATTERY_KEEPALIVE_MS    2000

// ─── WiFi AP (configuration web interface) ───────────────────────────────────
//
// The AP starts automatically this many ms after boot if no camera has
// connected yet (or after a camera disconnects).  Set to 0 to disable.
//
#define WIFI_AP_SSID              "FreeCLinker"
#define WIFI_AP_PASSWORD          ""              // empty = open network
#define WIFI_AP_CHANNEL           1
#define WIFI_AP_START_DELAY_MS    30000           // 30 s
