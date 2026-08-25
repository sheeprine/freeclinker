#pragma once
#include <stdint.h>
#include <string.h>

// Insta360 BLE camera-control protocol — the channel the official phone app
// uses for simple commands (shutter, mode, wipe SD, reboot); a separate,
// simpler channel from the WiFi API used for streaming/media transfer.
//
// Sources, independently cross-checked and agreeing byte-for-byte on the
// header layout below:
//   - Flo, "Insta360 X3 BLE remote control with ESP32"
//     (https://hackaday.io/project/188975-insta360-x3-ble-remote-control-with-esp32/details) —
//     real captured traffic against an X3, with hex dumps of commands and
//     their responses.
//   - Insta360's own packet-format developer docs
//     (https://insta360.whitebox.aero/developer-guide/packet-anatomy/),
//     describing the same "Phone Command" / "Keep Alive" / "Sync" packet
//     shapes seen in the capture above.
//
// Wire format for a Phone Command (all commands sent here — recording
// start/stop — carry no protobuf payload, so the packet is always exactly
// 16 bytes):
//   [0:4]   packet length, uint32 LE, includes this header (16 here)
//   [4:7]   message type, 3 bytes: 0x04,0x00,0x00 = "Phone Command" (used
//           for both the outgoing command and the camera's ack/response)
//   [7:9]   command code, uint16 LE
//   [9]     0x02 — constant in every captured sample; meaning undocumented
//   [10:13] sequence number, 3 bytes LE — free-running counter the camera
//           echoes back in its response but does not appear to validate
//           ("seems to be not relevant" per the original reverse-engineer)
//   [13:16] 0x80,0x00,0x00 — constant in every captured sample
//   [16:]   optional protobuf payload (unused by the commands sent here)
//
// A bare 7-byte [length=7][0x05,0x00,0x00] "Keep Alive"-shaped packet is
// sometimes received after a command, carrying no response code — just an
// ack. A full Phone Command response carries a response code at [7:9]:
// 200 (0x00C8 LE) means success; anything else (or a protobuf error string
// in the payload) means the command failed. The response protobuf body
// itself (e.g. a CAMERA_NOTIFICATION_CURRENT_CAPTURE_STATUS push) is Protocol
// Buffers whose .proto schema ships only inside the official app's libOne.so
// — not available here — so it's never decoded; recording state is tracked
// from the command we last sent and confirmed only via the 200 ack above,
// not from camera-pushed telemetry.
#define INSTA_SERVICE_UUID  "0000be80-0000-1000-8000-00805f9b34fb"
#define INSTA_CHAR_WRITE    "0000be81-0000-1000-8000-00805f9b34fb"  // ESP32  → camera (Write)
#define INSTA_CHAR_NOTIFY   "0000be82-0000-1000-8000-00805f9b34fb"  // camera → ESP32  (Notify)

#define INSTA_MSGTYPE_COMMAND   0x04  // byte [4]; bytes [5:7] are always 0x00,0x00
#define INSTA_MSGTYPE_KEEPALIVE 0x05  // byte [4] of a bare 7-byte ack packet

#define INSTA_CMD_TAKE_PHOTO   0x0003
#define INSTA_CMD_START_VIDEO  0x0004  // starts video in whatever mode is currently set on the camera
#define INSTA_CMD_STOP_VIDEO   0x0005

#define INSTA_RESP_OK  200

#define INSTA_PACKET_LEN  16
#define INSTA_SEQ_START   0x000200  // starting value used in the reference capture; camera doesn't validate it

// Builds a 16-byte Phone Command packet (no protobuf payload) for the given
// command code and sequence number. Returns the packet length.
inline uint8_t instaBuildCommand(uint16_t cmd, uint32_t seq, uint8_t out[INSTA_PACKET_LEN]) {
    memset(out, 0, INSTA_PACKET_LEN);
    out[0]  = INSTA_PACKET_LEN;  // length LE — top 3 bytes stay 0, packet is always 16 bytes
    out[4]  = INSTA_MSGTYPE_COMMAND;
    out[7]  = (uint8_t)(cmd & 0xFF);
    out[8]  = (uint8_t)(cmd >> 8);
    out[9]  = 0x02;
    out[10] = (uint8_t)(seq & 0xFF);
    out[11] = (uint8_t)((seq >> 8) & 0xFF);
    out[12] = (uint8_t)((seq >> 16) & 0xFF);
    out[13] = 0x80;
    return INSTA_PACKET_LEN;
}
