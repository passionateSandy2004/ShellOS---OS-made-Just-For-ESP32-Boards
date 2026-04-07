#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ─────────────────────────────────────────
   OS Identity
   ───────────────────────────────────────── */
#define OS_NAME        "ShellOS"
#define OS_VERSION     "1.0.0"
#if CONFIG_IDF_TARGET_ESP32C6
#define OS_ARCH        "RISC-V / ESP32-C6"
#define OS_EDITION     "ESP32-C6"
#else
#define OS_ARCH        "Xtensa LX6 / ESP32"
#define OS_EDITION     "ESP32-CAM"
#endif
#define OS_AUTHOR      "Built from scratch"

/* ─────────────────────────────────────────
   Kernel Boot
   ───────────────────────────────────────── */
void kernel_boot(void);
void kernel_print_banner(void);

/* Auto-WiFi: reads config/wifi.cfg and connects; called inside kernel_boot */
void kernel_wifi_autoconnect(void);

/* ─────────────────────────────────────────
   System Info
   ───────────────────────────────────────── */
typedef struct {
    uint32_t free_heap;
    uint32_t total_heap;
    uint32_t free_psram;
    uint32_t total_psram;
    uint32_t flash_size;
    uint8_t  cpu_cores;
    uint32_t cpu_freq_mhz;
    uint32_t uptime_ms;
} sys_info_t;

void kernel_get_sysinfo(sys_info_t *info);

#endif // KERNEL_H
