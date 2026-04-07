# ShellOS Imager

Desktop app (PyQt6) to flash **ShellOS** onto an **ESP32** board over USB — no ESP-IDF required for the person flashing.

## What you ship

1. Copy the three binaries from your ESP-IDF build into `firmware/` (see `firmware/README.txt`).
2. Zip the whole `imager` folder **or** build a standalone `.exe` (below).

## Run from source (developer / friend with Python)

```bash
cd imager
python -m venv .venv
.venv\Scripts\activate          # Windows
# source .venv/bin/activate     # Linux / macOS
pip install -r requirements.txt
python shellos_imager.py
```

Friend needs **Python 3.10+** and USB drivers (CP210x / CH340) for the serial chip.

## Build a Windows `.exe` (optional)

1. Fill `firmware/` with the three `.bin` files.
2. Install PyInstaller: `pip install pyinstaller`
3. From the `imager` folder run:

```bat
build_exe.bat
```

After a successful build, you can double-click **`START_ShellOS_Imager.bat`** in this folder — it always starts the exe from **`dist\`** (never from `build\`).

Output: **`dist\ShellOSImager\ShellOSImager.exe`** (use the whole **`dist\ShellOSImager`** folder — it contains `_internal\` with `python311.dll` and all dependencies).

**Do not** run the `.exe` under **`build\`** — that folder is PyInstaller’s temporary build only and will error with *Failed to load Python DLL*.

Zip **`dist\ShellOSImager`** and send that folder — it includes the embedded `firmware` directory.

## Hardware

- **ESP32-CAM** (AI Thinker) or other **ESP32** with **4MB** flash, same partition layout as this project.
- USB serial adapter in **download** mode if the port does not enumerate (hold **BOOT**, press **RST**, release **BOOT**).

## Troubleshooting

- **No port**: install UART driver; try another USB cable (data, not charge-only).
- **Flash fails**: run again at lower baud — edit `DEFAULT_BAUD` in `shellos_imager.py` to `115200` and rebuild, or use “Browse” if binaries live elsewhere.
