/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║                  ShellOS — ESP32-CAM Edition                 ║
 * ║                                                              ║
 * ║  A bare-metal style OS shell for the ESP32-CAM AI Thinker   ║
 * ║  Built on top of ESP-IDF (no Arduino)                        ║
 * ║                                                              ║
 * ║  Phase 1: Boot → UART Shell → Built-in Commands             ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include "kernel.h"
#include "shell.h"
#include "commands.h"
#include "uart_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

/*
 * Task layout — dual-core ESP32 (SMP FreeRTOS):
 *
 *   Core 0  — WiFi stack (ESP-IDF internal) + netsh_server + httpd
 *   Core 1  — shell_task (interactive UART + TCP command dispatch)
 *
 * Keeping shell on Core 1 means UART input / command execution is never
 * preempted by WiFi interrupts, giving snappy interactive response.
 * Network tasks on Core 0 share the same core as the WiFi driver so
 * socket callbacks need no cross-core synchronisation.
 *
 * Stack sizes:
 *   shell_task  — 16 KB: runs every built-in command, file I/O, Lua scripts
 *   (netsh_srv) — 16 KB: see netsh_server.c — handles TCP + full command set
 *   (httpd)     —  8 KB: see http_upload.c  — JSON + chunked upload
 *   app_main    — 16 KB: kernel_boot does FS + WiFi before this task exits
 */

/* ─────────────────────────────────────────
   shell_task — interactive OS shell
   Stack: 16 KB   Core: 1   Priority: 5
   ───────────────────────────────────────── */
static void shell_task(void *pvParam)
{
    shell_init();
    commands_register_all();
    shell_autorun_from_config();
    shell_run();          /* never returns */
    vTaskDelete(NULL);
}

/* ─────────────────────────────────────────
   app_main — ESP-IDF entry point
   ───────────────────────────────────────── */
void app_main(void)
{
#if CONFIG_IDF_TARGET_ESP32C6
    /* Before any WiFi/log activity: default LOG can block on USJ until COM is opened. */
    shellos_c6_install_nonblocking_log();
#endif
    /*
     * Suppress noisy IDF INFO logs during boot so they don't splice into
     * the kernel banner. Re-enable after the banner is printed.
     */
    esp_log_level_set("*", ESP_LOG_WARN);

    /* 1. Init NVS (needed by WiFi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* 2. Boot the kernel (UART init, FS mount, banner, WiFi autoconnect) */
    kernel_boot();
    shell_serial_flush_tx();
    esp_log_level_set("*", ESP_LOG_INFO);

    /* 3. Spawn shell task
     *    Dual-core (ESP32/S3/S2): pin to Core 1 so WiFi stays on Core 0.
     *    Single-core (ESP32-C6/C3): use xTaskCreate (no core affinity). */
#if CONFIG_FREERTOS_UNICORE
    xTaskCreate(
        shell_task, "shell_task",
        16384,          /* 16 KB stack */
        NULL, 5, NULL);
#else
    xTaskCreatePinnedToCore(
        shell_task, "shell_task",
        16384,          /* 16 KB stack */
        NULL, 5, NULL,
        1);             /* Core 1 — away from WiFi driver on Core 0 */
#endif
}
