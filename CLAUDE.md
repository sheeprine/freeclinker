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

This firmware runs on an ESP32 and bridges a camera (DJI Action or GoPro) to a Betaflight flight controller via MSP serial:

```
DJI Action / GoPro Camera ←—BLE—→ ESP32 ←—MSP Serial—→ Betaflight FC
```

### Layers

- **`src/camera.h`** — Abstract `Camera` base class with the shared interface (`begin`, `update`, `isConnected`, `startRecording`, `stopRecording`, `switchCameraMode`) and the `CameraCallback` typedef. Both camera implementations inherit from this.

- **`src/ble_camera.cpp`** — `BLECamera : public Camera`. BLE client for DJI Action cameras. Scans for manufacturer ID `0x08AA` / marker `0xFA`, connects, subscribes to GATT notifications on service `0xFFF0` / char `0xFFF4`, writes commands on char `0xFFF5` (or `0xFFF3` on Action 5 Pro). Calls registered callback on every status push.

- **`src/dji_protocol.cpp`** — Encodes/decodes DJI BLE frames. DJI uses a non-standard CRC-16 (seed `0x3AA3`) for the header and CRC-32 for the payload. Key command IDs: CmdSet `0x00` CmdID `0x19` (connect), CmdSet `0x1D` CmdID `0x05` (subscribe to status at 2 Hz), CmdSet `0x1D` CmdID `0x02` (status push with battery % and recording state).

- **`src/gopro_camera.cpp`** — `GoProCamera : public Camera`. BLE client for GoPro cameras (Open GoPro BLE API). Scans for service UUID `0xFEA6`, subscribes to GP-0073 (command ack) and GP-0077 (status notify), performs the hardware-info handshake, then registers for push notifications on battery %, encoding state, recording duration, SD remaining, overheating, and preset group. `switchCameraMode` maps DJI_MODE_* constants to GoPro preset groups. `triggerBurstSloMo` / `exitBurstSloMo` change the sub-mode setting without switching preset groups (used by the AUX switch when already recording).

- **`src/gopro_protocol.h`** — GoPro BLE constants (service/char UUIDs, command IDs, status IDs) and the `GpRxAssembler` struct for reassembling multi-packet BLE messages.

- **`src/msp_serial.cpp`** — MSP v2 protocol over UART. Polls `MSP_STATUS` (cmd 101) every 100 ms to detect arm state (bit 0 of flight mode flags bytes 6–9). Sends telemetry to Betaflight:
  - `MSP2_CAMERA_BATTERY` (`0x3001`): voltage, current, capacity, temperature
  - `MSP2_SET_TEXT` Custom Message 1: battery % (`"CAM:###%"`)
  - `MSP2_SET_TEXT` Custom Message 2: recording state — `"REC  M:SS"` (with elapsed time), `"IDLE"`, or `"CAM:HOT"` (overheating)
  - `MSP2_SET_TEXT` Custom Message 3: mode/resolution/FPS/EIS — e.g. `"VID 4K/60 RS+"` (up to 16 chars). Mode codes: `SLO`, `VID`, `TL`, `PHO`, `HYP`.
  - `MSP2_SET_TEXT` Custom Message 4: remaining record time and SD free space — e.g. `"30m    15.2G"`

- **`src/main.cpp`** — Wires the layers together. Selects the active camera (`BLECamera` or `GoProCamera`) at startup based on the `cameraType` NVS config, then uses it via the `Camera*` interface. On arm state change, triggers camera recording start/stop. Re-sends telemetry on battery update or keepalive timeout. AUX switch: when GoPro is active and recording, high/low triggers `triggerBurstSloMo` / `exitBurstSloMo`; otherwise calls `switchCameraMode` with the configured `auxMode`.

- **`src/config_manager.cpp`** — Runtime configuration via NVS. Parses serial commands (`help`, `show`, `set <key> <val>`, `reset`). Persisted keys: `camera_type` (0=DJI, 1=GoPro), `disarm_delay`, `stop_on_disarm`, `aux_channel`, `aux_mode` (default `0x00` = slow motion).

- **`src/telemetry.h`** — Shared `CameraData` struct used by both camera implementations and MSP output.

- **`include/config.h`** — Compile-time constants: GPIO pins, baud rates, scan duration, reconnect delay, keepalive intervals.

### Key design patterns

- Both `BLECamera` and `GoProCamera` use a static singleton (`_instance`) for BLE stack callbacks (trampoline pattern). Only one is active at a time.
- BLE writes must not happen inside a notify callback — both camera classes defer writes to their `update()` method using pending-action flags.
- All DJI wire-format structs use `__attribute__((packed))` for direct serialization.
- `MSPSerial` parses incoming bytes with an explicit state machine.
- GoPro uses TLV-encoded commands and registers for status push notifications rather than polling. Responses may span multiple 20-byte BLE packets; `GpRxAssembler` handles reassembly.
