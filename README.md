# DJI Action → Betaflight Bridge

ESP32 firmware that connects a DJI Action camera to a Betaflight flight controller. The ESP32 bridges the two devices: it receives camera telemetry over BLE and forwards it to the FC via MSP serial, and automatically starts/stops camera recording when the FC arms or disarms.

## How it works

```
DJI Action Camera ←—BLE—→ ESP32 ←—MSP Serial—→ Betaflight FC
```

1. The ESP32 scans for a DJI Action camera and establishes a BLE connection.
2. It subscribes to camera status pushes at 2 Hz, receiving battery percentage and recording state.
3. It polls the flight controller every 100 ms for arm state via `MSP_STATUS`.
4. On arm, it sends a recording-start command to the camera. On disarm, recording stops.
5. Battery and recording state are continuously forwarded to Betaflight as OSD telemetry.

## Telemetry sent to Betaflight

| Data | MSP message | OSD location |
|------|-------------|--------------|
| Battery voltage, current, capacity, temperature | `MSP2_CAMERA_BATTERY` (0x3001) | — |
| Battery percentage | `MSP2_SET_TEXT` | Custom Message 1 (`CAM:###%`) |
| Recording state | `MSP2_SET_TEXT` | Custom Message 2 (`REC` / `IDLE`) |

## Hardware setup

Connect the ESP32 to the Betaflight FC UART:

| ESP32 | FC |
|-------|----|
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
| `MSP_KEEPALIVE_MS` | 500 | GPS telemetry resend interval |
| `MSP_BATTERY_KEEPALIVE_MS` | 2000 | Battery telemetry resend interval |

## Building and flashing

Requires [PlatformIO](https://platformio.org/).

```bash
pio run                    # Build
pio run --target upload    # Flash to ESP32
pio device monitor         # View serial debug output
```

## Supported cameras

Any DJI Action camera that advertises BLE manufacturer ID `0x08AA` with marker byte `0xFA`, or whose device name contains `"DJI Action"`. Tested with DJI Action series cameras using the DJI proprietary BLE protocol (GATT service `0xFFF0`).
