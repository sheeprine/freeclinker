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

- **`src/camera.h`** — Abstract `Camera` base class with the shared interface (`begin`, `update`, `isConnected`, `startRecording`, `stopRecording`, `switchCameraMode`) and the `CameraCallback` typedef. Also holds `setRegistry`, `setStrictCamera`, and `setCameraCallback` wiring methods used by `main.cpp`. Both camera implementations inherit from this.

- **`src/ble_camera.cpp`** — `BLECamera : public Camera`. BLE client for DJI Action cameras. Scans for manufacturer ID `0x08AA` / marker `0xFA`, connects, subscribes to GATT notifications on service `0xFFF0` / char `0xFFF4`, writes commands on char `0xFFF5` (or `0xFFF3` on Action 5 Pro). Calls registered callback on every status push.

- **`src/dji_protocol.cpp`** — Encodes/decodes DJI BLE frames. DJI uses a non-standard CRC-16 (seed `0x3AA3`) for the header and CRC-32 for the payload. Key command IDs: CmdSet `0x00` CmdID `0x19` (connect), CmdSet `0x1D` CmdID `0x05` (subscribe to status at 2 Hz), CmdSet `0x1D` CmdID `0x02` (status push with battery % and recording state).

- **`src/gopro_camera.cpp`** — `GoProCamera : public Camera`. BLE client for GoPro cameras (Open GoPro BLE API). Scans for service UUID `0xFEA6`, subscribes to GP-0073 (command ack) and GP-0077 (status notify), performs the hardware-info handshake, then registers for push notifications on battery %, encoding state, recording duration, SD remaining, overheating, and preset group. `switchCameraMode` maps DJI_MODE_* constants to GoPro preset groups. `triggerBurstSloMo` / `exitBurstSloMo` change the sub-mode setting without switching preset groups (used by the AUX switch when already recording).

- **`src/gopro_protocol.h`** — GoPro BLE constants (service/char UUIDs, command IDs, status IDs) and the `GpRxAssembler` struct for reassembling multi-packet BLE messages.

- **`src/sony_camera.cpp`** — `SonyCamera : public Camera`. BLE client for Sony Alpha cameras, implementing two of Sony's undocumented BLE services (see `sony_protocol.h`; reverse-engineered by the [freemote](https://github.com/coral/freemote) project and [rock3r/CameraSync](https://github.com/rock3r/CameraSync)'s protocol docs). Scans for manufacturer ID `0x012D` / device type `0x0300`, connects, then requests an encrypted+bonded link via `esp_ble_set_encryption()` — the camera rejects writes/notifications until bonded (Just Works pairing; first-time pairing requires the camera's Bluetooth Rmt Ctrl menu to be open). Implements `BLESecurityCallbacks`; once `onAuthenticationComplete()` reports success, discovers the "Remote Control" service `8000FF00`, subscribes to notify char `0xFF02`, and writes button commands to `0xFF01`. Recording start/stop both send the same record-button down/up toggle (`0x0F` then `0x0E`), guarded by last-known recording state since the protocol has no explicit start/stop command. `switchCameraMode` is unsupported (no mode-switch command exists in this protocol) and always returns false. Separately, best-effort discovers the "Camera Control" service `8000CC00` and subscribes to battery notify char `0xCC10` (some models reduce/disable this service while Bluetooth Rmt Ctrl is active — a missing service just means no battery telemetry, not a connection failure). No resolution/fps/EIS/storage telemetry is available from either service — only focus/shutter/recording state and battery %.

- **`src/sony_protocol.h`** — Sony BLE remote constants (service/characteristic UUIDs, manufacturer-data scan prefix, button command codes, notification tag codes).

- **`src/msp_serial.cpp`** — MSP v2 protocol over UART. Polls `MSP_STATUS` (cmd 101) every 100 ms to detect arm state (bit 0 of flight mode flags bytes 6–9). Sends telemetry to Betaflight:
  - `MSP2_CAMERA_BATTERY` (`0x3001`): voltage, current, capacity, temperature
  - `MSP2_SET_TEXT` Custom Messages 1–4 via `sendCustomOSD1`–`sendCustomOSD4`, each delegating to the private `sendCustomOSD(textType, data, tpl)` helper. The helper expands a user-configurable template string using `expandTemplate()` and `resolveToken()`. Supported tokens: `{bat}`, `{rec}`, `{mode}`, `{res}`, `{fps}`, `{eis}`, `{rleft}`, `{rcap}`. Default templates reproduce the original hardcoded formats.

- **`src/main.cpp`** — Wires the layers together. Selects the active camera (`BLECamera` or `GoProCamera`) at startup based on the `cameraType` NVS config, then uses it via the `Camera*` interface. On arm state change, triggers camera recording start/stop. Re-sends telemetry on battery update or keepalive timeout. AUX switch: when GoPro is active and recording, high/low triggers `triggerBurstSloMo` / `exitBurstSloMo`; otherwise calls `switchCameraMode` with the configured `auxMode`.

- **`src/camera_registry.cpp`** — Persists a list of up to 64 previously connected cameras in NVS namespace `cam_reg`. Each entry stores name, BLE address, address type, and camera type (DJI/GoPro). On startup both camera implementations query the registry for the preferred address (last connected, or manually selected via `cameras connect <idx>`); during scan they stop early when that address is found and fall back to the first available device (unless `strict_camera` is enabled). `onConnected()` is called after every successful BLE connection to update the list and mark the last-connected entry.

- **`src/config_manager.cpp`** — Runtime configuration via NVS. Parses serial commands (`help`, `show`, `set <key> <val>`, `reset`, `cameras list/connect/remove/clear`). Persisted keys: `camera_type` (0=DJI, 1=GoPro, 2=Caddx Orca, 3=Sony), `strict_camera` (false=fallback to any camera found, true=only connect to the preferred address), `disarm_delay`, `stop_on_disarm`, `aux_channel`, `aux_mode` (default `0x00` = slow motion), `osd1_tpl`–`osd4_tpl` (OSD template strings, defaults reproduce the original hardcoded formats).

- **`src/telemetry.h`** — Shared `CameraData` struct used by both camera implementations and MSP output.

- **`include/config.h`** — Compile-time constants: GPIO pins, baud rates, scan duration, reconnect delay, keepalive intervals.

### Key design patterns

- `BLECamera`, `GoProCamera`, and `SonyCamera` each use a static singleton (`_instance`) for BLE stack callbacks (trampoline pattern). Only one is active at a time.
- BLE writes must not happen inside a notify callback — both camera classes defer writes to their `update()` method using pending-action flags.
- All DJI wire-format structs use `__attribute__((packed))` for direct serialization.
- `MSPSerial` parses incoming bytes with an explicit state machine.
- GoPro uses TLV-encoded commands and registers for status push notifications rather than polling. Responses may span multiple 20-byte BLE packets; `GpRxAssembler` handles reassembly.
