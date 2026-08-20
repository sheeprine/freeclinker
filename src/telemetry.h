#pragma once
#include <stdint.h>

// ─── Normalized camera settings codes ────────────────────────────────────────
// Both DJI and GoPro drivers map their native indices to these values so that
// msp_serial can use a single display table regardless of camera type.
// 0xFF means the camera has not reported that field.

// Resolution
#define CAM_RES_UNKNOWN  0xFF
#define CAM_RES_480P     0
#define CAM_RES_720P     1
#define CAM_RES_1080P    2
#define CAM_RES_1440P    3
#define CAM_RES_2_7K     4
#define CAM_RES_4K       5
#define CAM_RES_4K_WIDE  6   // 4K 4:3 / SuperView variants
#define CAM_RES_5_1K     7
#define CAM_RES_5_3K     8
#define CAM_RES_8K       9

// Frame rate
#define CAM_FPS_UNKNOWN  0xFF
#define CAM_FPS_24       0
#define CAM_FPS_25       1
#define CAM_FPS_30       2
#define CAM_FPS_48       3
#define CAM_FPS_50       4
#define CAM_FPS_60       5
#define CAM_FPS_90       6
#define CAM_FPS_100      7
#define CAM_FPS_120      8
#define CAM_FPS_200      9
#define CAM_FPS_240      10
#define CAM_FPS_400      11

// EIS / stabilization — values 0-4 match legacy DJI eis_mode byte exactly.
// Values 5-9 cover GoPro HyperSmooth levels. 0xFF = not reported.
#define CAM_EIS_UNKNOWN  0xFF
#define CAM_EIS_OFF      0   // DJI off  / GoPro OFF
#define CAM_EIS_RS       1   // DJI RockSteady
#define CAM_EIS_HS       2   // DJI HorizonSteady
#define CAM_EIS_RS_PLUS  3   // DJI RockSteady+
#define CAM_EIS_HB       4   // DJI HorizonBalance
#define CAM_EIS_LOW      5   // GoPro LOW
#define CAM_EIS_HIGH     6   // GoPro HIGH
#define CAM_EIS_BOOST    7   // GoPro BOOST
#define CAM_EIS_AUTO     8   // GoPro AUTO_BOOST
#define CAM_EIS_STD      9   // GoPro STANDARD

// ─── Shared camera telemetry snapshot ────────────────────────────────────────

struct CameraData {
    bool     valid        = false;
    uint8_t  percent      = 0;      // battery 0–100 %
    bool     recording    = false;  // true while actively recording
    uint8_t  camera_mode  = 0;      // DJI_MODE_* or mapped equivalent
    uint8_t  eis_mode     = 0;      // CAM_EIS_* (0=off, 0xFF=unknown)
    uint8_t  temp_over    = 0;      // 0=ok, 1=warn, 2=hot(can't record), 3=shutdown
    uint8_t  resolution   = CAM_RES_UNKNOWN;  // CAM_RES_*
    uint8_t  fps_idx      = CAM_FPS_UNKNOWN;  // CAM_FPS_*
    uint16_t record_time  = 0;      // seconds currently recording
    uint32_t remain_cap_mb = 0;     // SD card remaining (MB)
    uint32_t remain_time  = 0;      // recording seconds remaining on SD card
};
