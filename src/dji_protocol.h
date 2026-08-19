#pragma once
#include <stdint.h>
#include <stdbool.h>

// ─── BLE service / characteristic UUIDs (16-bit short form) ─────────────────
#define DJI_SERVICE_UUID        0xFFF0   // main GATT service
#define DJI_NOTIFY_CHAR_UUID    0xFFF4   // camera → ESP32  (Notify)
#define DJI_WRITE_CHAR_UUID     0xFFF5   // ESP32  → camera (Write)

// ─── Frame layout ─────────────────────────────────────────────────────────────
// Byte  0    : SOF = 0xAA
// Bytes 1-2  : ver_len  bits[15:10]=version(0), bits[9:0]=total frame length  LE
// Byte  3    : cmd_type (bit5: 0=command, 1=ack/response)
// Byte  4    : ENC = 0x00
// Bytes 5-7  : reserved = 0x00
// Bytes 8-9  : sequence number  LE
// Bytes 10-11: CRC-16 over bytes 0-9
// Byte  12   : CmdSet
// Byte  13   : CmdID
// Bytes 14.. : payload (variable)
// Last 4     : CRC-32 over bytes 0..(frame_len-5)
#define DJI_SOF         0xAA
#define DJI_OVERHEAD    18              // SOF…CRC16(12) + CmdSet+CmdID(2) + CRC32(4)
#define DJI_MAX_PAYLOAD 200
#define DJI_MAX_FRAME   (DJI_OVERHEAD + DJI_MAX_PAYLOAD)

// ─── CmdType ──────────────────────────────────────────────────────────────────
#define DJI_CMD         0x00            // command (bit5 = 0)
#define DJI_ACK         0x20            // response/ack (bit5 = 1)
#define DJI_IS_ACK(t)   (((t) & 0x20) != 0)

// ─── Command sets ──────────────────────────────────────────────────────────────
#define DJI_CMDSET_GENERAL  0x00
#define DJI_CMDSET_CAMERA   0x1D

// General command IDs (CmdSet 0x00)
#define DJI_CMD_VERSION     0x00
#define DJI_CMD_KEY_REPORT  0x11
#define DJI_CMD_GPS_PUSH    0x17        // ESP32 → camera: push GPS to camera
#define DJI_CMD_CONNECT     0x19        // ESP32 → camera: connection handshake

// Camera command IDs (CmdSet 0x1D)
#define DJI_CMD_STATUS_PUSH 0x02        // camera → ESP32: battery + mode
#define DJI_CMD_RECORD_CTRL 0x03
#define DJI_CMD_MODE_SWITCH 0x04
#define DJI_CMD_STATUS_SUB  0x05        // ESP32 → camera: subscribe to status push
#define DJI_CMD_NEW_STATUS  0x06

// Record control actions (CmdSet 0x1D, CmdID 0x03)
#define DJI_RECORD_START            0x01
#define DJI_RECORD_STOP             0x00

// Status subscription push modes
#define DJI_PUSH_OFF                0
#define DJI_PUSH_ONCE               1
#define DJI_PUSH_PERIODIC           2
#define DJI_PUSH_PERIODIC_ON_CHANGE 3   // 2 Hz + immediate push on any change

// ─── CRC seeds (non-standard, sourced from DJI SDK) ───────────────────────────
#define DJI_CRC16_SEED  ((uint16_t)0x3AA3)
#define DJI_CRC32_SEED  ((uint32_t)0x3AA3)

// ─── Packed wire-format structs ───────────────────────────────────────────────

// Camera status push — camera → ESP32 (CmdSet 0x1D, CmdID 0x02)
// Arrives at 2 Hz once subscribed.  bat_percent is at byte offset 37.
struct __attribute__((packed)) DJICameraStatus {
    uint8_t  camera_mode;           // 0x01=video, 0x05=photo, 0x0A=hyperlapse, …
    uint8_t  camera_status;         // 0x01=live-view, 0x03=recording, …
    uint8_t  video_resolution;
    uint8_t  fps_idx;
    uint8_t  eis_mode;              // 0=off, 1=RS, 2=HS, 3=RS+, 4=HB
    uint16_t record_time;           // seconds (ms in burst mode)
    uint8_t  fov_type;
    uint8_t  photo_ratio;           // 0=4:3, 1=16:9
    uint16_t real_time_countdown;   // seconds
    uint16_t timelapse_interval;    // 0.1 s units
    uint16_t timelapse_duration;    // seconds
    uint32_t remain_capacity;       // SD card MB remaining
    uint32_t remain_photo_num;
    uint32_t remain_time;           // recording seconds remaining
    uint8_t  user_mode;
    uint8_t  power_mode;            // 0=normal, 3=sleep
    uint8_t  camera_mode_next_flag;
    uint8_t  temp_over;             // 0=ok, 1=warning, 2=can't record, 3=shutdown
    uint32_t photo_countdown_ms;
    uint16_t loop_record_sends;     // seconds; 0=off, 65535=max
    uint8_t  bat_percent;           // 0–100 %   ← offset 37
};
static_assert(sizeof(DJICameraStatus) == 38, "DJICameraStatus size mismatch");

// Connection request — ESP32 → camera (CmdSet 0x00, CmdID 0x19)
struct __attribute__((packed)) DJIConnectRequest {
    uint32_t device_id;
    uint8_t  mac_addr_len;          // 6
    int8_t   mac_addr[16];          // ESP32 BT MAC in first 6 bytes, rest 0
    uint32_t fw_version;
    uint8_t  conidx;                // reserved
    uint8_t  verify_mode;           // 0 = no auth
    uint16_t verify_data;
    uint8_t  reserved[4];
};
static_assert(sizeof(DJIConnectRequest) == 33, "DJIConnectRequest size mismatch");

// Connection response — camera → ESP32 (CmdSet 0x00, CmdID 0x19, DJI_ACK)
struct __attribute__((packed)) DJIConnectResponse {
    uint32_t device_id;
    uint8_t  ret_code;              // 0 = success
    uint8_t  reserved[4];
};
static_assert(sizeof(DJIConnectResponse) == 9, "DJIConnectResponse size mismatch");

// Status subscription — ESP32 → camera (CmdSet 0x1D, CmdID 0x05)
struct __attribute__((packed)) DJIStatusSubscription {
    uint8_t  push_mode;             // DJI_PUSH_*
    uint8_t  push_freq;             // must be 20 (= 2 Hz, only supported value)
    uint8_t  reserved[4];
};
static_assert(sizeof(DJIStatusSubscription) == 6, "DJIStatusSubscription size mismatch");

// Record control — ESP32 → camera (CmdSet 0x1D, CmdID 0x03)
struct __attribute__((packed)) DJIRecordControl {
    uint8_t  action;                // DJI_RECORD_START / DJI_RECORD_STOP
};
static_assert(sizeof(DJIRecordControl) == 1, "DJIRecordControl size mismatch");

// ─── Protocol API ─────────────────────────────────────────────────────────────

// Build a complete DJI BLE frame into buf[buf_size].
// Returns total bytes written, or 0 if buf is too small.
uint16_t dji_build_frame(uint8_t *buf, uint16_t buf_size,
                          uint8_t cmd_set, uint8_t cmd_id, uint8_t cmd_type,
                          uint16_t seq,
                          const uint8_t *payload, uint16_t payload_len);

// Validate CRC-16 + CRC-32 and parse a received DJI frame.
// *payload points inside buf (not a copy); valid only while buf is live.
// Returns false if any CRC fails or the frame is malformed.
bool dji_parse_frame(const uint8_t *buf, uint16_t buf_len,
                      uint8_t *cmd_type, uint8_t *cmd_set, uint8_t *cmd_id,
                      const uint8_t **payload, uint16_t *payload_len);
