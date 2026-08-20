#pragma once
#include <stdint.h>

struct CameraData {
    bool     valid;
    uint8_t  percent;        // battery 0–100 %
    bool     recording;      // true while camera_status == 0x03
    uint8_t  camera_mode;    // 0x01=video, 0x05=photo, 0x0A=hyperlapse, …
    uint8_t  eis_mode;       // 0=off, 1=RS, 2=HS, 3=RS+, 4=HB
    uint8_t  temp_over;      // 0=ok, 1=warn(hot but recording), 2=hot(can't record), 3=shutdown
    uint16_t record_time;    // seconds currently recording (or burst ms)
    uint32_t remain_cap_mb;  // SD card remaining (MB)
    uint32_t remain_time;    // recording seconds remaining on SD card
};
