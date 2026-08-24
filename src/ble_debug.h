#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

// Raw BLE packet logger, gated at runtime by Config::debugBle (see
// config_manager.h). Shared by ble_camera.cpp and gopro_camera.cpp so TX/RX
// dumps read consistently in the serial log regardless of camera type.
inline void bleDebugDump(Stream &out, const char *dir, const char *tag,
                          const uint8_t *data, size_t len) {
    out.printf("[BLE-DBG] %s %s (%u bytes):", dir, tag, (unsigned)len);
    for (size_t i = 0; i < len; i++)
        out.printf(" %02X", data[i]);
    out.println();
}
