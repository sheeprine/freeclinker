# Quickstart Guide

This guide walks you through wiring an ESP32 Super Mini to a Betaflight flight controller, building and flashing the firmware, and configuring the bridge via the web interface.

---

## 1. Hardware wiring

Four wires are needed between the ESP32 and the FC. The ESP32 is powered directly from the FC's 5 V (BEC) rail — no separate USB power supply is required on the drone.

```
                    ESP32 Super Mini
                   ┌──────────────────┐
                   ┤ 3V3          GND ├── GND ────────────────┐
                   ┤ GND          IO1 ├                        │
                   ┤ IO2          IO2 ├                        │
                   ┤ IO3          IO4 ├                        │
                   ┤ IO5          IO5 ├                        │
                   ┤ IO6          IO6 ├                        │
                   ┤ IO7          IO7 ├                        │
                   ┤ IO8          IO8 ├                        │
                   ┤ IO9         IO16 ├── RX ←─────────────────┼──── FC UARTx TX
                   ┤ IO10        IO17 ├── TX ──────────────────┼──── FC UARTx RX
                   ┤ IO18        IO18 ├                        │
                   ┤ IO19        IO19 ├                        │
             USB ──┤ USB          5V  ├── 5V ──────────────────┼──── FC 5V (BEC)
                   └──────────────────┘                        │
                                                               │
                         Betaflight FC                         │
                        ┌─────────────────┐                   │
                        │ UARTx RX  ←────────────── ESP32 TX  │
                        │ UARTx TX  ─────────────→  ESP32 RX  │
                        │ GND       ←──────────────────────────┘
                        │ 5V (BEC)  ──────────────→  ESP32 5V │
                        └─────────────────┘
```

| ESP32 pin | FC pin        | Wire colour | Note                         |
|-----------|---------------|-------------|------------------------------|
| 5V        | 5V (BEC out)  | Red         | Powers the ESP32             |
| GND       | GND           | Black       | Common ground (required)     |
| GPIO 17   | UARTx **RX**  | Orange      | MSP data ESP32 → FC          |
| GPIO 16   | UARTx **TX**  | Yellow      | MSP responses FC → ESP32     |

> **5 V BEC:** Most FCs expose a regulated 5 V pin. Use that pad — do **not** connect to the battery rail (VBAT). The ESP32 Super Mini draws under 200 mA at peak, well within a typical BEC budget.
>
> **USB during flashing:** You can still plug the ESP32's USB-C port in for programming while the 5 V wire is connected. The two power sources will not conflict — the USB 5 V and the BEC 5 V are the same voltage.

---

## 2. Betaflight configuration

On the FC side, tell Betaflight to use the connected UART as an MSP port.

In the Betaflight CLI, replace `N` with the actual UART number:

```
serial N 0 115200 8 0 0 0
save
```

Alternatively use the Betaflight Configurator **Ports** tab: set the UART to **MSP** at **115200** baud.

To display camera telemetry in the OSD, go to the **OSD** tab and enable:

| OSD element      | Source              |
|------------------|---------------------|
| Custom Message 1 | Battery % (`CAM:###%`) |
| Custom Message 2 | Recording state / time |
| Custom Message 3 | Mode / resolution / FPS / EIS |
| Custom Message 4 | Remaining record time and SD space |

---

## 3. Build and flash

### Flash from the browser (easiest)

No toolchain needed. Open **[sheeprine.github.io/freeclinker/flash.html](https://sheeprine.github.io/freeclinker/flash.html)** in Chrome or Edge, select your board, click **Connect & Flash**, and pick the serial port.

> **ESP32-C3**: hold the **BOOT** button while plugging in to enter download mode.

### Build from source

#### Prerequisites

Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) (CLI) or the PlatformIO extension for VS Code.

#### Clone and build

```bash
git clone https://github.com/your-org/freeclinker.git
cd freeclinker
pio run                        # compile
pio run --target upload        # flash (auto-detects USB port)
pio device monitor             # open serial console at 115200 baud
```

> If you have multiple serial ports, specify the port explicitly:
> ```bash
> pio run --target upload --upload-port /dev/ttyUSB0
> pio device monitor --port /dev/ttyUSB0
> ```

On first boot you should see output similar to:

```
[cfg] camera_type     = DJI
[cfg] disarm_delay    = 0 ms
[cfg] stop_on_disarm  = true
[cfg] aux_channel     = disabled
[cfg] aux_mode        = 0x00
[main] Camera type: DJI Action
[main] MSP output: UART2 TX=GPIO17 @ 115200 baud
```

---

## 4. Select your camera

The firmware defaults to **DJI Action**. To switch to GoPro, type in the serial console:

```
set camera_type 1
```

Then reboot the ESP32. To revert to DJI:

```
set camera_type 0
```

The change is persisted across reboots. A reboot is required for `camera_type` to take effect.

---

## 5. Web interface

The configuration UI uses the **Web Serial API** and works in **Chrome** or **Edge** (desktop). Firefox does not support Web Serial.

### Open it

**Hosted:** open **[sheeprine.github.io/freeclinker](https://sheeprine.github.io/freeclinker/)** directly in Chrome or Edge.

**Local:** open `web/index.html` from the cloned repo:

```bash
open web/index.html          # macOS
start web/index.html         # Windows
xdg-open web/index.html      # Linux
```

Click **Connect**, select the ESP32 serial port from the browser dialog, and confirm at **115200** baud.

### Easy Config tab

Once connected the page reads the current settings automatically and presents them as controls:

| Control | Description |
|---------|-------------|
| Stop recording on disarm | Toggle whether the camera stops when the FC disarms |
| Disarm delay | Wait time (ms) between disarm and recording stop |
| AUX Channel | RC channel used for in-flight camera mode switching |
| Mode when high | Camera mode activated when the AUX channel exceeds 1500 µs |

Click **Apply** to write changes to the device. Settings are saved to flash and survive power cycles.

### CLI tab

The **CLI** tab gives direct access to the serial console with command history (arrow keys). All commands available via `pio device monitor` work here too:

```
help                        list all commands
show                        print current settings
set camera_type <0|1>       0 = DJI Action, 1 = GoPro
set stop_on_disarm <0|1>    enable / disable stop on disarm
set disarm_delay <ms>       delay before stopping (0 = immediate)
set aux_channel <0-12>      AUX channel number (0 = disabled)
set aux_mode <0x##>         0x00 slow-motion  0x01 video  0x0A hyperlapse
reset                       restore all defaults
```

---

## 6. Verify operation

1. Power on the FC and the ESP32.
2. Bring the camera within BLE range — the ESP32 will scan and connect automatically.
3. Arm the FC. The camera should start recording within a second.
4. Disarm. Recording stops (after the configured delay if set).
5. Check the Betaflight OSD — Custom Messages 1–4 should update in real time.
