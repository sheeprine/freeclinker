#pragma once
#include <stdint.h>
#include <string.h>

// ─── BLE service / characteristic UUIDs ──────────────────────────────────────

#define GP_SERVICE_UUID          0xFEA6  // 16-bit, used for scan filtering

// 128-bit characteristic UUIDs (base: b5f9XXXX-aa8d-11e3-9046-0002a5d5c51b)
#define GP_CHAR_CMD_WRITE      "b5f90072-aa8d-11e3-9046-0002a5d5c51b"  // ESP32 → camera
#define GP_CHAR_CMD_NOTIFY     "b5f90073-aa8d-11e3-9046-0002a5d5c51b"  // camera → ESP32 (cmd ack)
#define GP_CHAR_SETTING_WRITE  "b5f90074-aa8d-11e3-9046-0002a5d5c51b"  // ESP32 → camera (settings)
#define GP_CHAR_SETTING_NOTIFY "b5f90075-aa8d-11e3-9046-0002a5d5c51b"  // camera → ESP32 (setting ack)
#define GP_CHAR_QUERY_WRITE    "b5f90076-aa8d-11e3-9046-0002a5d5c51b"  // ESP32 → camera (query)
#define GP_CHAR_QUERY_NOTIFY   "b5f90077-aa8d-11e3-9046-0002a5d5c51b"  // camera → ESP32 (status)

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

// ─── Setting IDs written to GP_CHAR_SETTING_WRITE; ack on GP_CHAR_SETTING_NOTIFY ─
// Wire format: [header=(payload_len & 0x1F), setting_id(1), value_length(1), value...]

#define GP_SETTING_GENERAL_SUB_MODE  239  // UInt8; selects sub-mode within current mode

// Values for GP_SETTING_GENERAL_SUB_MODE (setting 239)
// Available options depend on the active preset group.
#define GP_SUB_MODE_STANDARD    12
#define GP_SUB_MODE_STATIONARY  13
#define GP_SUB_MODE_LOOPING     15
#define GP_SUB_MODE_PHOTO       16
#define GP_SUB_MODE_BURST       19
#define GP_SUB_MODE_MOTION      24
#define GP_SUB_MODE_SLOMO       27
#define GP_SUB_MODE_STAR_TRAILS 29
#define GP_SUB_MODE_LIGHT_PAINT 30
#define GP_SUB_MODE_VEHICLE_LTS 31
#define GP_SUB_MODE_BURST_SLOMO 32   // Mission 1 Pro only
#define GP_SUB_MODE_LOW_LIGHT   36
#define GP_SUB_MODE_INTERVAL    100

// ─── Query IDs (sent to GP_CHAR_QUERY_WRITE, response on GP_CHAR_QUERY_NOTIFY) ─

#define GP_QUERY_GET_STATUS           0x12  // one-shot status read
#define GP_QUERY_REGISTER_SETTING     0x52  // register for setting value updates
#define GP_QUERY_REGISTER_STATUS      0x53  // register for status value updates
#define GP_QUERY_UNREGISTER_SETTING   0x72
#define GP_QUERY_UNREGISTER_STATUS    0x73
#define GP_NOTIFY_SETTING_UPDATE      0x92  // pushed setting update notification
#define GP_NOTIFY_STATUS_UPDATE       0x93  // pushed status update notification

// ─── Status IDs ───────────────────────────────────────────────────────────────

#define GP_STATUS_OVERHEATING    6    // uint8: 0=no, 1=yes
#define GP_STATUS_ENCODING       10   // uint8: 0=idle, 1=recording
#define GP_STATUS_ENC_DURATION   13   // uint32 big-endian, current recording seconds
#define GP_STATUS_REMAINING_TIME 35   // uint32 big-endian, seconds remaining on SD
#define GP_STATUS_SD_REMAINING   54   // uint32 big-endian, MB remaining on SD
#define GP_STATUS_BATTERY_PCT    70   // uint8: 0-100 %
#define GP_STATUS_PRESET_GROUP   96   // uint8: 0=video, 1=photo, 2=timelapse

// ─── Setting IDs (queried via GP-0076, same channel as statuses) ──────────────
// These are registered separately from the status IDs above (0x52 vs 0x53),
// but share no numeric overlap with them, so both can be demultiplexed by ID
// in the same TLV parser regardless of which registration produced them.

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
