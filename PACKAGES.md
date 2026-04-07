# ShellOS Package System — Quick Start

## Step 1: Fetch Lua 5.4 source (one-time setup)

```
python tools/get_lua.py
```

This downloads Lua 5.4.7 into `components/lua_runtime/lua/` automatically.

## Step 2: Build the firmware

```
idf.py build
idf.py -p COM<N> flash monitor
```

After booting with WiFi connected, you'll see:
- TCP shell on port 2323
- HTTP Package API on port 8080

## Step 3: Write a package (Arduino C)

Create `blink_led.ino`:
```c
#define LED_PIN 2
void setup() { pinMode(LED_PIN, OUTPUT); }
void loop() {
    digitalWrite(LED_PIN, HIGH);
    delay(500);
    digitalWrite(LED_PIN, LOW);
    delay(500);
}
```

## Step 4: Build the package

```
python tools/shellos_compiler/shellos_compiler.py build blink_led.ino
```

Output: `blink_led.shpkg`

## Step 5: Upload and run

```
python tools/shellos_compiler/shellos_compiler.py upload 192.168.x.x blink_led.shpkg
python tools/shellos_compiler/shellos_compiler.py run    192.168.x.x blink_led
```

Or use the ShellOS Imager Package tab (GUI).

---

## Shell Commands (TCP shell on port 2323)

| Command | Description |
|---|---|
| `pkg list` | List installed packages |
| `pkg run <name>` | Start a package |
| `pkg stop <name>` | Stop a running package |
| `pkg logs <name>` | Show package log tail |
| `pkg info <name>` | Show package manifest |
| `pkg remove <name>` | Uninstall package |
| `pkg autorun <name> on\|off` | Toggle boot autorun |
| `pkg deploy <path>` | Install from local .shpkg |

---

## HTTP API (port 8080)

| Method | Endpoint | Description |
|---|---|---|
| POST | `/pkg/upload` | Raw .shpkg body — installs package |
| GET | `/pkg/list` | JSON array of packages |
| POST | `/pkg/run/<name>` | Start package |
| POST | `/pkg/stop/<name>` | Stop package |

---

## Lua API Available in Packages

```lua
-- GPIO (Arduino: pinMode / digitalWrite / digitalRead / analogRead)
gpio.mode(pin, gpio.OUTPUT)
gpio.mode(pin, gpio.INPUT)
gpio.write(pin, gpio.HIGH)
gpio.write(pin, gpio.LOW)
local val = gpio.read(pin)
local raw = gpio.analog_read(channel)

-- Timing (Arduino: delay / millis / micros)
delay(500)
local ms = millis()
local us = micros()

-- Serial output (Arduino: Serial.print / Serial.println)
serial.print("hello ")
serial.println("world")

-- File I/O (sandboxed to /data/<pkg>/)
local data = file.read("config.json")
file.write("config.json", '{"key":"val"}')
local ok = file.exists("config.json")
file.remove("temp.txt")

-- Logging (appends to /data/<pkg>/logs/app.log)
log("sensor value:", val)

-- HTTP requests
local body, err = http.get("http://api.example.com/data")
local resp, err  = http.post("http://api.example.com/send", "payload")
```

---

## Filesystem Layout

```
/root/packages/<name>/manifest.json   -- package metadata
/root/packages/<name>/main.lua        -- entry script
/root/data/<name>/                    -- package private storage
/root/data/<name>/logs/app.log        -- runtime log
```

---

## .shpkg Format

A minimal binary archive (no zip dependency):

```
[4 bytes]  magic "SHPK"
[1 byte]   version 0x01
[2 bytes]  file count (little-endian)
For each file:
  [1 byte]   filename length
  [N bytes]  filename (UTF-8)
  [4 bytes]  data length (little-endian)
  [N bytes]  file data
```
