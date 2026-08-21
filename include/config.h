#pragma once

#define FIRMWARE_VERSION "0.0.2"

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
// Betaflight CLI setup (replace N with the actual UART number):
//   serial N 0 115200 8 0 0 0   # set UART N to MSP
//   save
//
#define BF_BAUD                     115200

#ifdef CONFIG_IDF_TARGET_ESP32C3
// ESP32-C3 has only UART0 + UART1; Serial2 does not exist.
// Wire GPIO4 (TX) → FC UART RX.
#define BF_SERIAL                   Serial1
#define BF_TX_PIN                   4
#define BF_RX_PIN                   5
#else
// ESP32: use UART2 so UART0 (USB) stays free for the debug console.
// Wire GPIO17 (TX) → FC UART RX.
#define BF_SERIAL                   Serial2
#define BF_TX_PIN                   17
#define BF_RX_PIN                   16   // used to receive MSP responses from FC
#endif

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

// ─── BOOT-button force-AP ────────────────────────────────────────────────────
//
// Hold this GPIO LOW for WIFI_FORCE_AP_HOLD_MS to force the WiFi AP on
// until the next reboot, regardless of camera connection state.
// This is the BOOT/FLASH button present on most ESP32 dev boards.
//
#ifdef CONFIG_IDF_TARGET_ESP32C3
#define WIFI_FORCE_AP_PIN         9       // BOOT button on ESP32-C3 Super Mini
#else
#define WIFI_FORCE_AP_PIN         0       // BOOT/FLASH button on ESP32 DevKit
#endif
#define WIFI_FORCE_AP_HOLD_MS     5000    // hold duration in ms (default 5 s)

// ─── Status LED ──────────────────────────────────────────────────────────────
//
// Flashes while the WiFi AP is active.
// ESP32-C3 Super Mini: WS2812B RGB LED on GPIO8, driven via neopixelWrite().
// ESP32 DevKit:        simple GPIO LED on GPIO2 (LED_BUILTIN).
//
#ifdef CONFIG_IDF_TARGET_ESP32C3
#define STATUS_LED_PIN        8     // WS2812B RGB LED
#define STATUS_LED_RGB        1     // 1 = use neopixelWrite(), 0 = digitalWrite()
#else
#define STATUS_LED_PIN        2     // onboard LED (LED_BUILTIN)
#define STATUS_LED_RGB        0
#endif
#define STATUS_LED_INTERVAL_MS  250 // toggle interval in ms → 2 Hz flash (250 on / 250 off)
