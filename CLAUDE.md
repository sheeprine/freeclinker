# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

This is an ESP32 embedded firmware project using **PlatformIO**.

```bash
pio run                        # Build
pio run --target upload        # Flash to ESP32
pio device monitor             # Serial monitor (115200 baud)
pio run --target clean         # Clean build artifacts
```

There are no tests or linters — the project has direct hardware dependencies.

## Architecture

This firmware runs on an ESP32 and bridges a **DJI Action camera** (via BLE) to a **Betaflight flight controller** (via MSP serial). The ESP32 sits between the two devices:

```
DJI Action Camera ←—BLE—→ ESP32 ←—MSP Serial—→ Betaflight FC
```

### Layers

- **`src/ble_camera.cpp`** — BLE client: scans for DJI devices (manufacturer ID `0x08AA` with marker `0xFA`), connects, subscribes to GATT notifications on service `0xFFF0` / char `0xFFF4`, writes commands on char `0xFFF5`. Calls registered callbacks on battery/recording updates.

- **`src/dji_protocol.cpp`** — Encodes/decodes DJI BLE frames. DJI uses a non-standard CRC-16 (seed `0x3AA3`) for the header and CRC-32 for the payload. Key command IDs: CmdSet `0x00` CmdID `0x19` (connect), CmdSet `0x1D` CmdID `0x05` (subscribe to status at 2 Hz), CmdSet `0x1D` CmdID `0x02` (status push with battery % and recording state).

- **`src/msp_serial.cpp`** — MSP v2 protocol over UART. Polls `MSP_STATUS` (cmd 101) every 100 ms to detect arm state (bit 0 of flight mode flags bytes 6–9). Sends telemetry to Betaflight:
  - `MSP2_CAMERA_BATTERY` (`0x3001`): voltage, current, capacity, temperature
  - `MSP2_SET_TEXT`: battery % as `"CAM:###%"` (Custom Message 1), recording state as `"REC"` or `"IDLE"` (Custom Message 2)

- **`src/main.cpp`** — Wires the layers together. On arm state change, triggers camera recording start/stop. Re-sends telemetry on battery update or keepalive timeout.

- **`src/telemetry.h`** — Shared `TelemetryData` and `BatteryData` structs.

- **`include/config.h`** — All tunable constants: GPIO pins, baud rates, scan duration, reconnect delay, keepalive intervals.

### Key design patterns

- `BLECamera` uses a static singleton for BLE stack callbacks (trampoline pattern).
- All wire-format structs use `__attribute__((packed))` for direct serialization.
- `MSPSerial` parses incoming bytes with an explicit state machine.
