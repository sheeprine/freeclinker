#pragma once
#include <stdint.h>
#include <string.h>

// ─── BLE service / characteristic UUIDs ──────────────────────────────────────

#define GP_SERVICE_UUID          0xFEA6  // 16-bit, used for scan filtering

// 128-bit characteristic UUIDs (base: b5f9XXXX-aa8d-11e3-9046-0002a5d5c51b)
#define GP_CHAR_CMD_WRITE   "b5f90072-aa8d-11e3-9046-0002a5d5c51b"  // ESP32 → camera
#define GP_CHAR_CMD_NOTIFY  "b5f90073-aa8d-11e3-9046-0002a5d5c51b"  // camera → ESP32 (cmd ack)
#define GP_CHAR_QUERY_WRITE "b5f90076-aa8d-11e3-9046-0002a5d5c51b"  // ESP32 → camera (query)
#define GP_CHAR_QUERY_NOTIFY "b5f90077-aa8d-11e3-9046-0002a5d5c51b" // camera → ESP32 (status)

// Manufacturer company ID for scan identification (little-endian: 0x02, 0xF2)
#define GP_MANUFACTURER_ID_LO  0x02
#define GP_MANUFACTURER_ID_HI  0xF2
#define GP_DEVICE_NAME_PREFIX  "GoPro"

// ─── Command IDs (sent to GP_CHAR_CMD_WRITE, ack on GP_CHAR_CMD_NOTIFY) ──────

#define GP_CMD_SET_SHUTTER       0x01
#define GP_CMD_GET_HARDWARE_INFO 0x3C
#define GP_CMD_LOAD_PRESET_GROUP 0x3E

// Preset group values (parameter to GP_CMD_LOAD_PRESET_GROUP)
#define GP_PRESET_VIDEO       0x00
#define GP_PRESET_PHOTO       0x01
#define GP_PRESET_TIMELAPSE   0x02

// ─── Query IDs (sent to GP_CHAR_QUERY_WRITE, response on GP_CHAR_QUERY_NOTIFY) ─

#define GP_QUERY_GET_STATUS        0x12  // one-shot status read
#define GP_QUERY_REGISTER_STATUS   0x52  // register for change notifications
#define GP_QUERY_UNREGISTER_STATUS 0x72

// ─── Status IDs ───────────────────────────────────────────────────────────────

#define GP_STATUS_OVERHEATING    6    // uint8: 0=no, 1=yes
#define GP_STATUS_ENCODING       10   // uint8: 0=idle, 1=recording
#define GP_STATUS_ENC_DURATION   13   // uint32 big-endian, current recording seconds
#define GP_STATUS_REMAINING_TIME 35   // uint32 big-endian, seconds remaining on SD
#define GP_STATUS_SD_REMAINING   54   // uint32 big-endian, MB remaining on SD
#define GP_STATUS_BATTERY_PCT    70   // uint8: 0-100 %
#define GP_STATUS_PRESET_GROUP   96   // uint8: 0=video, 1=photo, 2=timelapse

// ─── Setting IDs (queried via GP-0076, same channel as statuses) ──────────────
// These are registered with 0x52 alongside status IDs; no numeric overlap with
// the status IDs above, so they can be demultiplexed by ID in the TLV parser.

#define GP_SETTING_RESOLUTION    2    // uint8: VIDEO_RESOLUTION value (see below)
#define GP_SETTING_FPS           3    // uint8: FRAMES_PER_SECOND value (see below)
#define GP_SETTING_HYPERSMOOTH   135  // uint8: 0=OFF,1=LOW,2=HIGH,3=BOOST,4=AUTO_BOOST,100=STD

// ─── Packet framing helpers ───────────────────────────────────────────────────
//
// Outbound (ESP32 → GoPro):
//   Byte 0: general 5-bit header = payload_length & 0x1F  (for messages ≤ 31 bytes)
//   Bytes 1+: payload
//
// Inbound start packet:
//   Bits [7]=0, [6:5]=00  → general: length = bits[4:0], payload at byte 1
//   Bits [7]=0, [6:5]=01  → extended 13-bit: length = bits[4:0]<<8 | byte1, payload at byte 2
//
// Inbound continuation packet:
//   Bit [7]=1, bits[3:0]=counter

// Reassembly state for one characteristic's incoming stream.
struct GpRxAssembler {
    uint8_t  buf[512];
    uint16_t expected = 0;
    uint16_t pos      = 0;

    // Feed one BLE notification. Returns true when the full message is ready in buf[0..expected-1].
    bool feed(const uint8_t *data, size_t len) {
        if (len == 0) return false;

        if (data[0] & 0x80) {
            // Continuation packet
            size_t copy = len - 1;
            if (pos + copy > expected) copy = expected - pos;
            memcpy(buf + pos, data + 1, copy);
            pos += (uint16_t)copy;
        } else {
            // Start packet
            uint16_t offset;
            if ((data[0] & 0x60) == 0x00) {
                expected = data[0] & 0x1F;
                offset   = 1;
            } else if ((data[0] & 0x60) == 0x20) {
                if (len < 2) return false;
                expected = ((uint16_t)(data[0] & 0x1F) << 8) | data[1];
                offset   = 2;
            } else {
                return false;
            }
            pos = 0;
            size_t copy = len - offset;
            if (copy > expected) copy = expected;
            memcpy(buf, data + offset, copy);
            pos = (uint16_t)copy;
        }

        return expected > 0 && pos >= expected;
    }

    void reset() { expected = 0; pos = 0; }
};
