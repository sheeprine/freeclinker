#pragma once
#include <stdint.h>

// Sony Alpha BLE protocol, as reverse-engineered by the freemote project
// (https://github.com/coral/freemote), Greg Leeds
// (https://gregleeds.com/reverse-engineering-sony-camera-bluetooth/), and
// rock3r/CameraSync's protocol docs (https://github.com/rock3r/CameraSync).
//
// Sony cameras expose two independent BLE services used here:
//   • 8000FF00 "Remote Control" — a virtual remote-button protocol. Commands
//     simulate physical remote buttons (shutter, record, AF-on, C1); the
//     camera only reports focus/shutter/recording state in return.
//   • 8000CC00 "Camera Control" — read/notify monitoring service (battery,
//     storage, Wi-Fi handoff). Not required for button control, and may be
//     reduced/unavailable on some models while "Bluetooth Rmt Ctrl" is
//     active, so it's treated as best-effort.
// No resolution/fps/EIS telemetry is available from either service.
//
// Both require an encrypted, bonded BLE link (Just Works pairing) before the
// camera will accept writes or disclose notifications — the camera's
// Bluetooth Rmt Ctrl menu must be opened to allow the first-time pairing.

// ─── BLE service / characteristic UUIDs (128-bit custom, not SIG-assigned) ──
#define SONY_SERVICE_UUID  "8000ff00-ff00-ffff-ffff-ffffffffffff"
#define SONY_CHAR_COMMAND  "0000ff01-ff00-ffff-ffff-ffffffffffff"  // ESP32  → camera (Write)
#define SONY_CHAR_NOTIFY   "0000ff02-ff00-ffff-ffff-ffffffffffff"  // camera → ESP32  (Notify)

// ─── Scan identification ──────────────────────────────────────────────────────
// Manufacturer data prefix: Sony Corporation company ID 0x012D (LE bytes
// 0x2D,0x01), followed by device type 0x0300 ("this is a camera").
#define SONY_MFR_ID_LO       0x2D
#define SONY_MFR_ID_HI       0x01
#define SONY_MFR_DEVTYPE_LO  0x03
#define SONY_MFR_DEVTYPE_HI  0x00
#define SONY_DEVICE_NAME_PREFIX  "ILCE"   // most Alpha bodies advertise as "ILCE-xxxx"

// Pairing-status tag inside manufacturer data: byte 0x22 followed by a
// bitmask byte. Bit 0x40 = camera has its pairing menu open, bit 0x02 =
// remote control function enabled. Diagnostic only (logged during scan) —
// actual bonding is negotiated over the GAP link, not gated on this.
#define SONY_PAIRING_TAG          0x22
#define SONY_PAIRING_ENABLED_BIT  0x40
#define SONY_REMOTE_ENABLED_BIT   0x02

// ─── Remote command codes (written to SONY_CHAR_COMMAND) ────────────────────
// Wire format: [length=0x01, command]. Momentary "button" commands must be
// sent in down/up pairs to simulate a physical press-and-release.
#define SONY_CMD_SHUTTER_HALF_UP    0x06
#define SONY_CMD_SHUTTER_HALF_DOWN  0x07
#define SONY_CMD_SHUTTER_FULL_UP    0x08
#define SONY_CMD_SHUTTER_FULL_DOWN  0x09
#define SONY_CMD_RECORD_UP          0x0E   // release — sent second
#define SONY_CMD_RECORD_DOWN        0x0F   // press — sent first; down+up toggles recording
#define SONY_CMD_AF_ON_UP           0x14
#define SONY_CMD_AF_ON_DOWN         0x15
#define SONY_CMD_C1_UP              0x20
#define SONY_CMD_C1_DOWN            0x21

// ─── Notifications (from SONY_CHAR_NOTIFY) ───────────────────────────────────
// Wire format: [0x02, tag, value]
#define SONY_NOTIFY_TAG_FOCUS      0x3F
#define SONY_NOTIFY_TAG_SHUTTER    0xA0
#define SONY_NOTIFY_TAG_RECORDING  0xD5
#define SONY_NOTIFY_VALUE_ACTIVE   0x20   // focus acquired / shutter active / recording started

// ─── Camera Control service (monitoring — battery, storage, Wi-Fi handoff) ──
#define SONY_CC_SERVICE_UUID  "8000cc00-cc00-ffff-ffff-ffffffffffff"
#define SONY_CC_CHAR_BATTERY  "0000cc10-cc00-ffff-ffff-ffffffffffff"  // Read+Notify

// Battery Info (0xCC10) payload, big-endian multi-byte fields:
//   [0]     total length (excludes this byte)
//   [1:2]   data type — 0x0000 = battery info
//   [3]     battery pack count (usually 1; 2 with a vertical grip)
//   [4..]   packs, each 7 bytes:
//     +0  bit0=slot enabled, bit1=InfoLithium
//     +1  position: 0x00=unknown, 0x01=body, 0x02=grip1, 0x03=grip2
//     +2  coarse level: 0x01=PreEnd .. 0x05=Level4 (not used here)
//     +3..+6  remaining percentage, big-endian uint32 (0-100)
// This byte layout is sourced from a third-party protocol writeup, not
// independently verified against a camera — if percent never populates,
// enable debug_ble and compare the raw 0xCC10 dump against this layout.
#define SONY_BATTERY_PACK_SIZE   7
#define SONY_BATTERY_POS_BODY    0x01
