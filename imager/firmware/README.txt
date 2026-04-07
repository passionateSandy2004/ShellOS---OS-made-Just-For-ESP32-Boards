ShellOS — firmware files for the imager
========================================

Copy these three files from your ESP-IDF build output into THIS folder:

  bootloader.bin          <- from build/bootloader/bootloader.bin
  partition-table.bin       <- from build/partition_table/partition-table.bin
  esp32_shell_os.bin        <- from build/esp32_shell_os.bin

After a successful `idf.py build` in the ShellOS project:

  copy build\bootloader\bootloader.bin       firmware\bootloader.bin
  copy build\partition_table\partition-table.bin  firmware\partition-table.bin
  copy build\esp32_shell_os.bin            firmware\esp32_shell_os.bin

Target: ESP32 (ESP32-CAM AI Thinker), 4MB flash, DIO 80MHz.
Distribute this entire `imager` folder (with firmware filled in) as a ZIP to end users.
