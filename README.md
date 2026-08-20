# FreeCLinker a DJI Action / GoPro → Betaflight Bridge

FreeCLinker as in Free Cam Linker. Pronounced *freak linker* because we know
you bunch of freaks like to film yourself doing *illegal* stuff.
ESP32 firmware that connects a DJI Action or GoPro camera to a Betaflight flight controller.

**→ [Quickstart guide](QUICKSTART.md)** — wiring, build, flash, and web interface setup. The ESP32 bridges the two devices: it receives camera telemetry over BLE and forwards it to the FC via MSP serial, and automatically starts/stops camera recording when the FC arms or disarms.

## How it works

```
DJI Action / GoPro Camera ←—BLE—→ ESP32 ←—MSP Serial—→ Betaflight FC
```

1. The ESP32 scans for a supported camera and establishes a BLE connection.
2. It subscribes to camera status updates, receiving battery percentage, recording state, and more.
3. It polls the flight controller every 100 ms for arm state via `MSP_STATUS`.
4. On arm, it sends a recording-start command to the camera. On disarm, recording stops.
5. Battery and recording state are continuously forwarded to Betaflight as OSD telemetry.

## Telemetry sent to Betaflight

| Data | MSP message | OSD location |
|------|-------------|--------------|
| Battery voltage, current, capacity, temperature | `MSP2_CAMERA_BATTERY` (0x3001) | — |
| Battery percentage | `MSP2_SET_TEXT` | Custom Message 1 (`CAM:###%`) |
| Recording state / time | `MSP2_SET_TEXT` | Custom Message 2 (`REC  0:42` / `IDLE` / `CAM:HOT`) |
| Mode, resolution, FPS, stabilisation | `MSP2_SET_TEXT` | Custom Message 3 (e.g. `VID 4K/60 RS+`) |
| Remaining record time and SD free space | `MSP2_SET_TEXT` | Custom Message 4 (e.g. `30m    15.2G`) |

## Hardware setup

Connect the ESP32 to the Betaflight FC UART:

| ESP32 | FC |
|-------|----|
| 5V / VCC | 5V |
| TX (GPIO 17) | RX (any free UART) |
| RX (GPIO 16) | TX (same UART) |
| GND | GND |

Configure the FC UART for **MSP** at **115200 baud**.

## Configuration

All tunable parameters are in `include/config.h`:

| Constant | Default | Description |
|----------|---------|-------------|
| `BF_TX_PIN` / `BF_RX_PIN` | 17 / 16 | GPIO pins for MSP serial |
| `BF_BAUD` | 115200 | MSP serial baud rate |
| `BLE_SCAN_DURATION_SECS` | 5 | BLE scan window |
| `BLE_RECONNECT_DELAY_MS` | 3000 | Backoff after failed connection |
| `MSP_BATTERY_KEEPALIVE_MS` | 2000 | Battery telemetry resend interval |

Runtime settings are changed via the USB serial console and persisted across reboots:

| Command | Default | Description |
|---------|---------|-------------|
| `set camera_type <0\|1>` | 0 | Camera protocol: 0 = DJI, 1 = GoPro (reboot to apply) |
| `set stop_on_disarm <0\|1>` | 1 | Stop recording on FC disarm |
| `set disarm_delay <ms>` | 0 | Delay between disarm and recording stop |
| `set aux_channel <0-12>` | 0 | AUX channel for camera mode switch (0 = off) |
| `set aux_mode <0x##>` | 0x00 | Camera mode when AUX is high (`0x00`=slow-motion, `0x01`=video, `0x0A`=hyperlapse). On GoPro, when already recording, AUX high/low triggers Burst Slo-Mo instead of switching presets. |

Type `help` in the serial console to list all commands, `show` to print current settings.

## Web interface

Open `web/index.html` directly in **Chrome or Edge** (no server required). Connect to the ESP32 via the browser's Web Serial dialog to get a graphical config panel and an interactive CLI terminal.

## Building and flashing

Requires [PlatformIO](https://platformio.org/).

```bash
pio run                    # Build
pio run --target upload    # Flash to ESP32
pio device monitor         # View serial debug output
```

## Supported cameras

### DJI Action (default)

Any DJI Action camera that advertises BLE manufacturer ID `0x08AA` with marker byte `0xFA`, or whose device name contains `"DJI Action"`. Uses the DJI proprietary BLE protocol (GATT service `0xFFF0`). Status is pushed at 2 Hz once subscribed.

### GoPro (Open GoPro BLE API)

Any GoPro camera supporting the [Open GoPro BLE API](https://gopro.github.io/OpenGoPro/docs/ble/) (HERO 9 and later). Identified by advertised service UUID `0xFEA6`. Uses standard TLV-encoded commands and registers for push notifications on status changes.

To use a GoPro, connect via serial and run:

```
set camera_type 1
```

Then reboot the ESP32. To switch back to DJI: `set camera_type 0` and reboot.
