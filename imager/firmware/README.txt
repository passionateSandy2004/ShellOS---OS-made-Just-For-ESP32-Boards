ShellOS — firmware files for the imager
========================================

This folder contains per-board firmware subfolders.

Copy these three files from your ESP-IDF build output into the matching board folder:

  bootloader.bin           <- from build/bootloader/bootloader.bin
  partition-table.bin      <- from build/partition_table/partition-table.bin
  esp32_shell_os.bin       <- from build/esp32_shell_os.bin

After a successful `idf.py build` in the ShellOS project:

  REM Example: ESP32-CAM build outputs
  copy build\bootloader\bootloader.bin              firmware\esp32-cam\bootloader.bin
  copy build\partition_table\partition-table.bin    firmware\esp32-cam\partition-table.bin
  copy build\esp32_shell_os.bin                     firmware\esp32-cam\esp32_shell_os.bin

  REM Example: ESP32-C6 build outputs
  copy build\bootloader\bootloader.bin              firmware\esp32-c6\bootloader.bin
  copy build\partition_table\partition-table.bin    firmware\esp32-c6\partition-table.bin
  copy build\esp32_shell_os.bin                     firmware\esp32-c6\esp32_shell_os.bin

Targets:
  - esp32-cam  (ESP32-CAM AI Thinker class boards)
  - esp32-c6   (ESP32-C6 class boards)

Distribute this entire `imager` folder (with firmware filled in) as a ZIP to end users.
