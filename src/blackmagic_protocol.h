#pragma once
#include <stdint.h>

// Blackmagic Bluetooth Camera Control protocol, per the official "Blackmagic
// Camera Control" developer PDF (documents.blackmagicdesign.com) and
// cross-checked against community implementations coral/blackmagic-camera-
// control, coral/blackmagic-camera-protocol, schoolpost/BlueMagic32, and
// marklysze/Magic-Pocket-Control-ESP32 — all mutually consistent on the wire
// format below.
//
// The BLE characteristics carry the same "SDI Camera Control Protocol"
// packets the cameras also speak over the video cable — Bluetooth just adds
// pairing and a Camera Status flag byte on top.

// ─── BLE service / characteristic UUIDs ──────────────────────────────────────
#define BMD_SERVICE_UUID           "291d567a-6d75-11e6-8b77-86f30ca893d3"
#define BMD_CHAR_OUTGOING_CONTROL  "5dd3465f-1aee-4299-8493-d2eca2f8e1bb"  // ESP32  → camera (Write, encrypted)
#define BMD_CHAR_INCOMING_CONTROL  "b864e140-76a0-416a-bf30-5876504537d9"  // camera → ESP32  (Notify, encrypted)
#define BMD_CHAR_TIMECODE          "6d8f2110-86f1-41bf-9afb-451d87e976c8"  // camera → ESP32  (Notify, encrypted) — unused
#define BMD_CHAR_CAMERA_STATUS     "7fe8691d-95dc-4fc5-8abd-ca74339b51b9"  // camera → ESP32  (Notify, encrypted)
#define BMD_CHAR_DEVICE_NAME       "ffac0c52-c9fb-41a0-b063-cc76282eb89c"  // ESP32  → camera (Write, plaintext)
#define BMD_CHAR_PROTOCOL_VERSION  "8f1fd018-b508-456f-8f82-3d392bee2706"  // camera → ESP32  (Read, plaintext)

#define BMD_DEVICE_NAME  "FreeCLinker"  // shown in the camera's Bluetooth Setup Menu

// ─── Camera Status characteristic — 8-bit flag byte ─────────────────────────
#define BMD_STATUS_POWERED_ON         0x01
#define BMD_STATUS_CONNECTED          0x02
#define BMD_STATUS_PAIRED             0x04
#define BMD_STATUS_VERSIONS_VERIFIED  0x08
#define BMD_STATUS_INITIAL_PAYLOAD    0x10
#define BMD_STATUS_CAMERA_READY       0x20

// ─── SDI/Bluetooth Camera Control Protocol — packet header ───────────────────
// [0] destination (0xFF = broadcast)
// [1] command length — byte count from [4] (category) onward, NOT including
//     the 4-byte outer header or trailing padding
// [2] command id (0 = "change configuration", the only command used here)
// [3] reserved (0)
// [4] category
// [5] parameter
// [6] data type
// [7] operation
// [8..] data, zero-padded so the total packet length rounds up to a
//       multiple of 4 bytes (the padding is not counted in [1])
#define BMD_CMD_CHANGE_CONFIGURATION  0x00
#define BMD_DEST_BROADCAST            0xFF

#define BMD_DATA_TYPE_INT8    1
#define BMD_OP_ASSIGN_VALUE   0

// ─── Media category (10) — recording control ────────────────────────────────
#define BMD_CATEGORY_MEDIA              10
#define BMD_MEDIA_PARAM_TRANSPORT_MODE  1

// Transport mode data: [mode, speed, flags, slot1 medium, slot2 medium] —
// only [0] (mode) is ever sent here; the rest are left as padding, which
// matches the exact byte sequence used by BlueMagic32/Magic-Pocket-Control
// (both proven against real cameras) rather than guessing at speed/flags.
#define BMD_TRANSPORT_MODE_PREVIEW  0
#define BMD_TRANSPORT_MODE_PLAY     1
#define BMD_TRANSPORT_MODE_RECORD   2

// Builds a "Transport Mode" assign-value command (category 10, parameter 1)
// requesting the given mode. Always 12 bytes: 8-byte header + 1 real data
// byte padded to a 4-byte boundary, per protocol. Returns the packet length.
inline uint8_t bmdBuildTransportModeCommand(uint8_t mode, uint8_t out[12]) {
    out[0] = BMD_DEST_BROADCAST;
    out[1] = 5;  // length: 4 header bytes (category..operation) + 1 data byte
    out[2] = BMD_CMD_CHANGE_CONFIGURATION;
    out[3] = 0;  // reserved
    out[4] = BMD_CATEGORY_MEDIA;
    out[5] = BMD_MEDIA_PARAM_TRANSPORT_MODE;
    out[6] = BMD_DATA_TYPE_INT8;
    out[7] = BMD_OP_ASSIGN_VALUE;
    out[8]  = mode;
    out[9]  = 0;  // padding
    out[10] = 0;  // padding
    out[11] = 0;  // padding
    return 12;
}
