<div align="center">

```
███████╗██╗  ██╗███████╗██╗     ██╗      ██████╗ ███████╗
██╔════╝██║  ██║██╔════╝██║     ██║     ██╔═══██╗██╔════╝
███████╗███████║█████╗  ██║     ██║     ██║   ██║███████╗
╚════██║██╔══██║██╔══╝  ██║     ██║     ██║   ██║╚════██║
███████║██║  ██║███████╗███████╗███████╗╚██████╔╝███████║
╚══════╝╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝ ╚═════╝ ╚══════╝
```

# ShellOS — A Real Operating System for a $5 Microcontroller

**Shell. WiFi. Filesystem. Packages. Lua runtime. TCP remote shell. Camera. HTTP API.**  
**All running on an ESP32 — built entirely from scratch, no Arduino, no Linux.**

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-ESP32%20%7C%20ESP32--C6-blue)](https://www.espressif.com/)
[![Built with ESP-IDF](https://img.shields.io/badge/Built%20with-ESP--IDF%20v5.5-orange)](https://docs.espressif.com/projects/esp-idf/)
[![Lua](https://img.shields.io/badge/Lua-5.4-blueviolet)](https://www.lua.org/)
[![Stars](https://img.shields.io/github/stars/yourusername/shellos?style=social)](https://github.com/yourusername/shellos)

</div>

---

## What is ShellOS?

### The gap that has always existed

Linux changed everything for computing — not because it was powerful, but because it gave you **one shell to control everything**: processes, files, I/O, network, storage. You could run ten programs at once, watch each one's output, kill any of them, and store their data — all from a single terminal. That model became the foundation of every server, every cloud machine, every embedded Linux board like Raspberry Pi.

But Linux needs an MMU. It needs megabytes of RAM. It needs a kernel that takes seconds to boot.

So for decades, microcontrollers — the **real** embedded world, boards that cost $2–$10, run forever on a coin cell, and live inside every product you use — were left out. You flash one monolithic image. It does one job. It has no shell. No filesystem. No way to update behavior without reflashing the device. No way to run two programs and manage them. No concept of a "process" or "package". You write C, compile, flash, and pray.

**That gap has never been filled. Until now.**

---

### What ShellOS actually is

ShellOS is a **real operating system** for the ESP32 — a $5 microcontroller — built entirely from scratch on ESP-IDF and FreeRTOS.

Not “just firmware”. Not Arduino. Not Linux ported down. A purpose-built OS that brings the core ideas of Unix — shell, filesystem, process management, isolated I/O — to bare metal hardware that was never supposed to support any of this.

Here is what that means concretely:

**A real shell — one terminal to control everything**
```
ShellOS /root > help

  Filesystem  : ls, cat, write, mkdir, rm, cp
  Network     : wifi connect, wifi scan, wifi status
  Packages    : pkg run, pkg stop, pkg list, pkg logs, pkg remove
  System      : sysinfo, ps, uptime, reboot
  Camera      : cam init, cam capture, cam info

ShellOS /root > ps

  TASK            STATE     STACK USED
  shell_task      running   3.1 KB
  blink_led       running   1.8 KB
  temp_logger     running   2.2 KB
  mqtt_client     running   2.9 KB
  netsh_server    running   2.4 KB
```

Four packages running simultaneously. Each in its own FreeRTOS task. Each with its own isolated storage. All visible and controllable from one shell — exactly like Linux.

**A filesystem nobody expected on this hardware**

```
ShellOS /root > ls

  config/     packages/     data/     logs/

ShellOS /root > ls data/

  blink_led/     temp_logger/     mqtt_client/

ShellOS /root > cat data/temp_logger/logs/app.log

  [00:01:12] temp: 27.4°C  humidity: 61%
  [00:02:12] temp: 27.6°C  humidity: 60%
  [00:03:12] temp: 27.5°C  humidity: 61%
```

LittleFS on the 4 MB flash chip. Persistent across reboots. Every package gets its own sandboxed directory under `/root/data/<name>/`. Its config lives there. Its logs go there. No package touches another's files. This is proper filesystem isolation on a chip that ships with no OS at all.

**A package manager that runs code without reflashing**

Every "package" is a program — GPIO control, sensor polling, MQTT publishing, HTTP fetching — deployed **over WiFi** from your laptop to the running device. No USB. No recompile. No reflash. The OS receives the package, extracts it to flash, spawns a FreeRTOS task for it, and it starts running immediately alongside everything else.

```
ShellOS /root > pkg list

  NAME            VERSION    STATE      AUTORUN
  blink_led       v1.0.0     running    off
  temp_logger     v1.2.0     running    on
  mqtt_client     v2.0.0     stopped    on

ShellOS /root > pkg stop blink_led
  [OK] blink_led stopped

ShellOS /root > pkg autorun temp_logger on
  [OK] temp_logger will start on next boot

ShellOS /root > pkg logs mqtt_client
  [00:04:31] Connected to broker.hivemq.com
  [00:04:32] Published: /home/temp → 27.5
```

**Process isolation with per-package I/O**

Each package's `Serial.println()` and `log()` calls go to its own log file at `/root/data/<name>/logs/app.log`. Not mixed into a global console. Not lost. Persistent. Readable any time from the shell with `pkg logs <name>`. This is what makes multi-package management usable — every process has its own output, just like Linux.

**A TCP remote shell over WiFi**

```bash
nc 192.168.1.42 2323
```

That's it. You're inside the ShellOS shell from your PC — no USB, no serial adapter. Full interactive terminal. Same commands. Same filesystem. Same package control. From anywhere on the network.

```
ShellOS /root > sysinfo

  ┌──────────────────────────────────────┐
  │   System Information                 │
  ├──────────────────────────────────────┤
  │  Chip    : ESP32-CAM (AI Thinker)    │
  │  Arch    : Xtensa LX6 / ESP32        │
  │  Heap    : 268 KB free / 327 KB      │
  │  PSRAM   : 3.9 MB free / 4.0 MB      │
  │  Flash   : 4 MB                      │
  │  Uptime  : 3m 42s                    │
  │  WiFi    : 192.168.1.42              │
  │  Camera  : Ready (VGA)               │
  └──────────────────────────────────────┘
```

This is what Linux gave the world for servers. ShellOS brings the same model to the microcontroller — the hardware that actually runs the real world.

---

## The Moment It Clicks

Imagine this. You write a `.ino`-style C sketch on your laptop:

```c
void loop() {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("Blink!");
    delay(1000);
}
```

You open **ShellOS Imager**, go to the **Packages tab**, point it at your `.ino` file and your device IP — and click **Deploy**.

```
┌─────────────────────────────────────────────┐
│         ShellOS Imager  — Packages          │
├─────────────────────────────────────────────┤
│  Device IP :  192.168.1.42                  │
│  Package   :  blink_led.ino        [Browse] │
│                                             │
│  [ Build ]   [ Deploy ]   [ Run ]  [ Stop ] │
├─────────────────────────────────────────────┤
│  Installed packages:                        │
│  ● blink_led    v1.0.0    running           │
│  ○ temp_logger  v1.2.0    stopped           │
└─────────────────────────────────────────────┘
```

Your code is **live on the device. Over WiFi. Without reflashing. Without a USB cable. Without a single command.**

That's the ShellOS package system.

---

## Feature Map

| Feature | Description |
|---|---|
| **UART Shell** | Interactive terminal over serial — like a real UNIX shell |
| **TCP Remote Shell** | `nc <ip> 2323` — SSH-like remote access over WiFi |
| **LittleFS Filesystem** | Persistent files, config, logs at `/root/` on flash |
| **WiFi Driver** | Connect, scan, manage networks from the shell |
| **Package Manager** | Install / run / stop / remove Lua packages wirelessly |
| **Lua 5.4 Runtime** | Full Lua interpreter embedded — runs packages in isolated tasks |
| **Arduino-style Lua API** | `gpio`, `delay`, `serial`, `file`, `http`, `log` — feels like Arduino |
| **HTTP Package API** | `POST /pkg/upload` — deploy packages from the web or imager GUI |
| **Camera Driver** | OV2640 camera capture (ESP32-CAM), JPEG frames, resolution control |
| **Autorun** | Packages with `autorun=true` start automatically on every boot |
| **ShellOS Imager** | Official desktop app — flash the ShellOS OS image + build/deploy/manage packages over WiFi, no CLI needed |
| **Transpiler** | `.ino` → Lua transpiler built into the Imager — write Arduino C, deploy wirelessly |
| **Multi-board** | Targets: **ESP32-CAM** (Xtensa) and **ESP32-C6** (RISC-V) |

---

## The “Android moment” for microcontrollers

Android didn’t change phones by making them faster at one thing — it changed them by turning a phone into a **platform**:
- **One OS** that boots reliably on millions of devices
- **A real app model** (install, run, stop, update)
- **Storage + sandboxing** (apps don’t step on each other)
- **A consistent runtime + APIs** (developers build on the platform, not on one-off vendor builds)

Before Android, a phone was mostly **fixed vendor firmware**. After Android, the same class of hardware became an **ecosystem**.

**ShellOS is that same platform jump — but for microcontrollers.**

Before ShellOS, an ESP32 project is usually:
- **One monolithic image**
- **One “main loop”**
- **One global log**
- Update = **reflash**

With ShellOS, the ESP32 becomes a tiny always-on platform:
- **Multiple packages running simultaneously** (each as its own managed task)
- **Per-package storage + logs** under `/root/data/<pkg>/` (isolated by design)
- **One shell** to control everything locally or over WiFi (TCP shell)
- **Deploy new behavior wirelessly** without reflashing the OS image (Imager Packages tab / HTTP API)

## Supported Hardware

| Board | Architecture | Flash | PSRAM | Camera | Status |
|---|---|---|---|---|---|
| **ESP32-CAM (AI Thinker)** | Xtensa LX6 | 4 MB | 4 MB | OV2640 | ✅ Full support |
| **Seeed XIAO ESP32-C6** | RISC-V | 4 MB | — | None | ✅ Full support |
| Any ESP32 (4 MB flash) | Xtensa LX6 | 4 MB | optional | optional | ✅ Should work |

> Camera is hardware-dependent. On boards without OV2640, `cam_driver_stub.c` replaces the driver — all other features remain fully functional.

---

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│                          ShellOS                               │
│                                                                │
│  ┌─────────────┐   ┌──────────────┐   ┌────────────────────┐  │
│  │   kernel.c  │   │   shell.c    │   │   commands.c       │  │
│  │  (boot,WiFi │   │ (REPL loop,  │   │ (ls,cat,mkdir,     │  │
│  │   banner)   │   │  TCP server) │   │  sysinfo,pkg,cam)  │  │
│  └─────────────┘   └──────────────┘   └────────────────────┘  │
│                                                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    Package System                        │  │
│  │  pkg_manager.c → Lua 5.4 runtime → lua_api_gpio/fs/http │  │
│  │  shpkg.c       → custom .shpkg binary archive format     │  │
│  │  http_upload.c → POST /pkg/upload REST endpoint          │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                      Drivers                             │  │
│  │  uart_driver.c   wifi_driver.c   cam_driver.c            │  │
│  │  (UART0 / USJ)   (esp_wifi)      (esp32-camera)          │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              Storage: LittleFS on SPI Flash              │  │
│  │  /root/config/  /root/packages/  /root/data/  /root/logs │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                │
│                   ESP-IDF v5.x  +  FreeRTOS                    │
└────────────────────────────────────────────────────────────────┘
```

---

## Getting Started

### Prerequisites

- **ESP-IDF v5.x** installed ([Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/))
- **Python 3.10+** (for tools / imager)
- An **ESP32-CAM** (AI Thinker) or **Seeed XIAO ESP32-C6**

### 1. Clone

```bash
git clone https://github.com/yourusername/shellos.git
cd shellos
```

### 2. Fetch Lua 5.4 source (one-time)

```bash
python tools/get_lua.py
```

### 3. Set your target and build

**For ESP32-CAM:**
```bash
idf.py set-target esp32
idf.py build
```

**For ESP32-C6 (XIAO or similar):**
```bash
idf.py set-target esp32c6
idf.py build
```

### 4. Flash

```bash
idf.py -p COM<N> flash monitor
```

### 5. Boot

```
  ███████╗██╗  ██╗███████╗██╗     ██╗      ██████╗ ███████╗
  ...

  ┌─────────────────────────────────────────────────────┐
  │   ShellOS v1.0.0 — ESP32-CAM Edition                │
  │   Arch : Xtensa LX6 / ESP32                         │
  │   Built from scratch                                │
  └─────────────────────────────────────────────────────┘

  [OK] UART driver initialized
  [OK] LittleFS mounted at /root
  [OK] WiFi Connected — IP 192.168.1.42
  [OK] TCP shell listening on :2323
  [OK] HTTP Package API on :8080

ShellOS /root >
```

---

## WiFi Setup

Create `/root/config/wifi.cfg` on the filesystem:

```bash
ShellOS /root > mkdir config
ShellOS /root > write config/wifi.cfg "ssid=YourNetwork,pass=YourPassword"
```

Or put the file there before first boot using the ShellOS Imager tool. On next boot, ShellOS auto-connects.

---

## Remote Shell (TCP)

Once WiFi is up, connect from your PC:

```bash
# Linux / macOS
nc 192.168.1.42 2323

# Windows (PuTTY)
# Connection type: Raw  Host: 192.168.1.42  Port: 2323
```

You now have a **real interactive shell** running inside your $5 chip.

---

## Package System

### Write a package (Arduino C style)

```c
// blink_led.ino
#define LED_PIN 4

void setup() {
    pinMode(LED_PIN, OUTPUT);
    Serial.println("blink_led: started");
}

void loop() {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED ON");
    delay(1000);
    digitalWrite(LED_PIN, LOW);
    Serial.println("LED OFF");
    delay(1000);
}
```

### Deploy wirelessly — ShellOS Imager (recommended)

**ShellOS Imager** is the official desktop app for ShellOS. It handles everything — no command line needed.

1. Open **ShellOS Imager** (`imager/shellos_imager.py` or the pre-built `.exe`)
2. Go to the **Packages** tab
3. Enter your device IP (shown in the boot log after WiFi connects)
4. Click **Browse** → select your `.ino` file
5. Click **Build → Deploy → Run**

Done. Your package is running on the device. You can see it live in the Installed Packages list, start/stop it, and view its logs — all from the same window.

```bash
# Start the Imager
cd imager
pip install -r requirements.txt
python shellos_imager.py

# Windows: just double-click
imager/dist/ShellOSImager/ShellOSImager.exe
```

### Deploy wirelessly — CLI (for automation / CI / scripting)

If you prefer the command line or want to script deployments:

```bash
python tools/shellos_compiler/shellos_compiler.py build  blink_led.ino
python tools/shellos_compiler/shellos_compiler.py upload 192.168.1.42 blink_led.shpkg
python tools/shellos_compiler/shellos_compiler.py run    192.168.1.42 blink_led
```

### Manage from the shell

```
ShellOS /root > pkg list

  NAME          VERSION     STATE
  blink_led     v1.0.0      running
  temp_logger   v1.2.0      stopped

ShellOS /root > pkg stop blink_led
ShellOS /root > pkg autorun blink_led on
ShellOS /root > pkg logs temp_logger
```

### Lua API available in packages

```lua
-- GPIO
gpio.mode(4, gpio.OUTPUT)
gpio.write(4, gpio.HIGH)
local val = gpio.read(0)

-- Timing
delay(500)
local t = millis()

-- Serial output
serial.println("hello from Lua!")

-- Persistent file I/O (sandboxed to /root/data/<pkgname>/)
file.write("config.json", '{"interval":1000}')
local cfg = file.read("config.json")

-- HTTP client
local body, err = http.get("http://api.example.com/data")
local res, err  = http.post("http://api.example.com/send", "payload")

-- Logging
log("sensor:", value)   -- appends to /root/data/<pkg>/logs/app.log
```

---

## HTTP Package API

Deploy and manage packages from any REST client or the ShellOS Imager GUI:

| Method | Endpoint | Description |
|---|---|---|
| `POST` | `/pkg/upload` | Raw `.shpkg` body — installs package |
| `GET` | `/pkg/list` | JSON list of installed packages |
| `POST` | `/pkg/run/<name>` | Start a package |
| `POST` | `/pkg/stop/<name>` | Stop a running package |

```bash
# Deploy a package using curl
curl -X POST http://192.168.1.42:8080/pkg/upload \
     --data-binary @blink_led.shpkg

# List packages
curl http://192.168.1.42:8080/pkg/list
```

---

## ShellOS Imager — The Official Desktop App

**ShellOS Imager** is the complete management tool for ShellOS. If you're not building the OS image from source, this is all you need.

```
┌──────────────────────────────────────────────────────┐
│                   ShellOS Imager                     │
├────────────────┬─────────────────────────────────────┤
│   Flash tab    │  Select port → Select OS image        │
│                │  → Click Flash. Done.                │
├────────────────┼─────────────────────────────────────┤
│  Packages tab  │  Enter device IP                     │
│                │  Browse .ino → Build → Deploy → Run  │
│                │  Live list of installed packages      │
│                │  Start / Stop / View logs per package │
└────────────────┴─────────────────────────────────────┘
```

**What it does:**
- **Flashes** the full ShellOS OS image (bootloader + partition table + app) over USB — no ESP-IDF required
- **Builds** `.ino` packages using the built-in transpiler
- **Deploys** packages to the device over WiFi in one click
- **Manages** running packages — start, stop, view live logs
- Works with both **ESP32-CAM** and **ESP32-C6**

**Run from source:**
```bash
cd imager
pip install -r requirements.txt
python shellos_imager.py
```

**Windows pre-built (no Python needed):**
```
imager/dist/ShellOSImager/ShellOSImager.exe
```
Zip the entire `dist/ShellOSImager/` folder and send it to anyone — they can flash and manage ShellOS without installing anything.

---

## Shell Commands Reference

| Command | Description |
|---|---|
| `help` | List all commands |
| `sysinfo` | CPU, heap, flash, uptime, WiFi info |
| `ls [path]` | List directory |
| `cat <file>` | Print file contents |
| `write <file> <text>` | Write text to file |
| `mkdir <dir>` | Create directory |
| `rm <path>` | Delete file or directory |
| `wifi connect <ssid> <pass>` | Connect to WiFi |
| `wifi scan` | Scan nearby networks |
| `wifi status` | Show connection status |
| `cam init` | Initialize camera |
| `cam capture` | Capture a JPEG frame |
| `cam info` | Show camera status |
| `pkg list` | List installed packages |
| `pkg run <name>` | Start a package |
| `pkg stop <name>` | Stop a package |
| `pkg logs <name>` | Show package logs |
| `pkg remove <name>` | Uninstall a package |
| `pkg autorun <name> on\|off` | Enable/disable autostart |
| `pkg deploy <path>` | Install from local .shpkg |
| `ps` | List running FreeRTOS tasks |
| `uptime` | Show system uptime |
| `reboot` | Restart the device |

---

## Filesystem Layout

```
/root/
├── config/
│   ├── wifi.cfg          ← WiFi credentials (ssid=x,pass=y)
│   └── autorun.cfg       ← Commands to run at boot
├── packages/
│   └── <name>/
│       ├── manifest.json ← Name, version, author, autorun flag
│       └── main.lua      ← Package entry script
└── data/
    └── <name>/
        ├── logs/
        │   └── app.log   ← Package runtime log
        └── ...           ← Package private storage
```

---

## .shpkg Format

A tiny custom binary archive — no zip, no external dependencies:

```
[4 bytes]  magic: "SHPK"
[1 byte]   version: 0x01
[2 bytes]  file count (little-endian)
Per file:
  [1 byte]   filename length
  [N bytes]  filename (UTF-8)
  [4 bytes]  data length (little-endian)
  [N bytes]  file data
```

---

## Multi-Board: ESP32-CAM vs ESP32-C6

ShellOS uses compile-time conditionals to support multiple chips from the same source tree.

| | ESP32-CAM | Seeed XIAO ESP32-C6 |
|---|---|---|
| **Target** | `idf.py set-target esp32` | `idf.py set-target esp32c6` |
| **Console** | UART0 GPIO1/GPIO3 | USB Serial/JTAG (built-in) |
| **Camera** | OV2640 full driver | Stub (no hardware) |
| **PSRAM** | 4 MB | None (uses internal RAM) |
| **CPU** | Xtensa LX6 @ 240 MHz | RISC-V @ 160 MHz |
| **WiFi** | 802.11b/g/n | 802.11b/g/n/ax (WiFi 6) |
| **All other features** | ✅ | ✅ |

---

## Project Structure

```
esp32_shell_os/
├── main/                        ← app_main entry point
├── components/
│   ├── shell/                   ← kernel, shell, commands, TCP server, HTTP
│   ├── drivers/                 ← UART, WiFi, Camera (real + stub)
│   ├── fs/                      ← LittleFS VFS wrapper
│   ├── pkg_manager/             ← Package registry, .shpkg extractor
│   └── lua_runtime/             ← Lua 5.4 + GPIO/serial/file/http/log APIs
├── tools/
│   ├── shellos_compiler/        ← .ino → Lua transpiler + .shpkg builder + uploader CLI
│   ├── example_packages/        ← blink_led.ino, hello_pkg.ino, etc.
│   └── get_lua.py               ← downloads Lua 5.4 source
├── imager/                      ← PyQt6 GUI flasher + package manager
├── partitions.csv               ← 4 MB layout: nvs / phy / factory / storage (LittleFS)
├── sdkconfig.defaults           ← common sdkconfig
├── sdkconfig.defaults.esp32     ← ESP32-CAM extras (PSRAM, camera SCCB, 240MHz)
├── sdkconfig.defaults.esp32c6   ← ESP32-C6 extras (USB console, 160MHz)
└── CMakeLists.txt
```

---

## Why This Matters

### For embedded developers
You get to see **how an OS is built from scratch** — kernel boot, shell REPL, VFS, task isolation, package management — all in C, without abstractions hiding the good parts.

### For OS / systems developers
ShellOS is a case study in **porting a userspace-style system to bare metal** — no MMU, no process isolation at hardware level, but all the OS concepts implemented in software over FreeRTOS.

### For VLSI / chip architects
The port to **ESP32-C6 (RISC-V)** demonstrates real cross-architecture challenges: **USB-Serial-JTAG console blocking behavior**, **unicore task scheduling**, **no PSRAM fallback**, 802.11ax negotiation — problems you only encounter when you actually run an OS on the silicon.

### For hobbyists
It's a **$5 chip that you remote-shell into, deploy apps to over WiFi, and use as a tiny Linux-like computer** — except it boots in 2 seconds, runs for days on a power bank, and you built the whole OS yourself.

---

## Contributing

ShellOS is fully open source. Contributions welcome.

**Ideas for contribution:**
- New shell commands
- More Lua API bindings (I2C, SPI, ADC, PWM, BLE)
- New board ports (ESP32-S3, ESP32-C3, ESP32-H2)
- MQTT package
- WebSocket support in HTTP server
- Package registry / cloud index
- OTA OS image updates
- REPL syntax highlighting
- More example packages

Please open an issue before large changes to discuss direction.

---

## License

MIT License — see [LICENSE](LICENSE).

Use it. Fork it. Build on it. Ship it. Teach it.

---

## Acknowledgements

- **Espressif Systems** — ESP-IDF and the ESP32 family
- **Lua.org** — Lua 5.4 language and runtime
- **joltwallet/littlefs** — LittleFS port for ESP-IDF
- **Seeed Studio** — XIAO ESP32-C6 board

---

<div align="center">

**Built from scratch. Runs on $5. Open to everyone.**

*If this project sparked something for you — star it, share it, build on it.*

</div>
