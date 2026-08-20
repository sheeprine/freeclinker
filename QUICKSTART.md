# Quickstart Guide

This guide walks you through wiring an ESP32 Super Mini to a Betaflight flight controller, building and flashing the firmware, and configuring the bridge via the web interface.

---

## 1. Hardware wiring

Four wires connect the ESP32 to the FC. The ESP32 is powered from the FC's 5 V rail — no separate USB power supply is needed on the drone.

### ESP32-C3 Super Mini (compact build)

```
                ESP32-C3 Super Mini
               ┌──────────────────┐
               ┤ 3V3         GND  ├── GND ─────────────────────┐
               ┤ IO2         IO0  ├                             │
               ┤ IO3         IO1  ├                             │
         TX ──┤ IO4          5V  ├── 5V ──────────────────────┼──── FC 5V (BEC)
         RX ──┤ IO5         IO21 ├                             │
               ┤ IO6         IO20 ├                             │
               ┤ IO7         IO19 ├                             │
               ┤ IO8         IO18 ├                             │
        BOOT ──┤ IO9         IO10 ├                             │
         USB ──┤ USB              ├                             │
               └──────────────────┘                             │
                                                                │
                     Betaflight FC                              │
                    ┌──────────────────┐                       │
                    │ UARTx RX  ←──────────── ESP32-C3 IO4 TX  │
                    │ UARTx TX  ──────────→   ESP32-C3 IO5 RX  │
                    │ GND       ←────────────────────────────────┘
                    │ 5V (BEC)  ──────────→   ESP32-C3 5V      │
                    └──────────────────┘
```

| ESP32-C3 pin | FC pin        | Wire colour | Note                          |
|--------------|---------------|-------------|-------------------------------|
| 5V           | 5V (BEC out)  | Red         | Powers the ESP32-C3           |
| GND          | GND           | Black       | Common ground (required)      |
| IO4 (TX)     | UARTx **RX**  | Orange      | MSP data ESP32-C3 → FC        |
| IO5 (RX)     | UARTx **TX**  | Yellow      | MSP responses FC → ESP32-C3   |

> **Download mode (ESP32-C3):** Hold the **BOOT** (IO9) button while plugging in USB to enter the ROM bootloader for flashing. Not needed for normal operation.

### Standard ESP32 Dev Board (38-pin)

```
                  ESP32 Dev Board
                 ┌──────────────────┐
                 ┤ EN          GPIO23├
                 ┤ GPIO36      GPIO22├
                 ┤ GPIO39      GPIO1 ├
                 ┤ GPIO34      GPIO3 ├
                 ┤ GPIO35      GPIO21├
                 ┤ GPIO32      GND   ├── GND ───────────────────┐
                 ┤ GPIO33      GPIO19├                           │
                 ┤ GPIO25      GPIO18├                           │
                 ┤ GPIO26      GPIO5 ├                           │
                 ┤ GPIO27      GPIO17├── TX ────────────────────┼──── FC UARTx RX
                 ┤ GPIO14      GPIO16├── RX ←───────────────────┼──── FC UARTx TX
                 ┤ GPIO12      GPIO4 ├                           │
                 ┤ GND         GPIO0 ├                           │
                 ┤ GPIO13      GPIO2 ├                           │
                 ┤ GPIO9       GPIO15├                           │
                 ┤ GPIO10      GPIO8 ├                           │
                 ┤ GPIO11      GPIO7 ├                           │
           USB ──┤ VIN(5V)    GPIO6  ├── 5V ───────────────────┼──── FC 5V (BEC)
                 └──────────────────┘                           │
                                                                │
                     Betaflight FC                              │
                    ┌──────────────────┐                       │
                    │ UARTx RX  ←──────────── ESP32 GPIO17 TX  │
                    │ UARTx TX  ──────────→   ESP32 GPIO16 RX  │
                    │ GND       ←────────────────────────────────┘
                    │ 5V (BEC)  ──────────→   ESP32 VIN        │
                    └──────────────────┘
```

| ESP32 pin    | FC pin        | Wire colour | Note                       |
|--------------|---------------|-------------|----------------------------|
| VIN (5V)     | 5V (BEC out)  | Red         | Powers the ESP32           |
| GND          | GND           | Black       | Common ground (required)   |
| GPIO17 (TX)  | UARTx **RX**  | Orange      | MSP data ESP32 → FC        |
| GPIO16 (RX)  | UARTx **TX**  | Yellow      | MSP responses FC → ESP32   |

> **5 V BEC:** Use the regulated 5 V pad on the FC — do **not** connect to the battery rail (VBAT). Both ESP32 variants draw under 200 mA at peak, well within a typical BEC budget.
>
> **USB during flashing:** The ESP32's USB port can stay plugged in while the 5 V wire is connected. Both sources are 5 V and will not conflict.

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
git clone https://github.com/sheeprine/freeclinker.git
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

There are two ways to open the configuration UI — choose whichever is more convenient.

### Option A — Built-in WiFi AP (easiest, any browser, no cable)

If no camera connects within **30 seconds** of boot, the ESP32 automatically starts a WiFi access point:

| Setting  | Value            |
|----------|------------------|
| SSID     | `FreeCLinker`    |
| Password | *(none — open)*  |
| URL      | `http://192.168.4.1` |

1. On your phone or laptop, join the `FreeCLinker` WiFi network.
2. Open `http://192.168.4.1` in any browser.

The AP stops as soon as a camera connects (BLE pairing takes priority). It restarts automatically 30 s after a camera disconnects, so you can always reconfigure between flights without a USB cable.

### Option B — USB Serial (Chrome / Edge desktop only)

**Hosted:** open **[sheeprine.github.io/freeclinker](https://sheeprine.github.io/freeclinker/)** in Chrome or Edge.

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
