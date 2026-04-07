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

/* ─────────────────────────────────────────
   shell_task — runs the shell in a FreeRTOS task
   Stack: 8KB  Priority: 5
   ───────────────────────────────────────── */
static void shell_task(void *pvParam)
{
    shell_init();
    /* Register all built-in commands */
    commands_register_all();

    /* Phase 4: optional boot script from config/autorun.cfg */
    shell_autorun_from_config();

    /* Start the shell loop — never returns */
    shell_run();

    /* Should never reach here */
    vTaskDelete(NULL);
}

/* ─────────────────────────────────────────
   app_main — ESP-IDF entry point
   This is the equivalent of "main()" in our OS
   ───────────────────────────────────────── */
void app_main(void)
{
#if CONFIG_IDF_TARGET_ESP32C6
    /* Before any WiFi/log activity: default LOG can block on USJ until COM is opened. */
    shellos_c6_install_nonblocking_log();
#endif
    /*
     * ESP_LOG* and our uart_* both use UART0 — INFO logs (e.g. "Returned from app_main()")
     * byte-splice into kernel lines. Suppress global INFO until boot banner is done.
     */
    esp_log_level_set("*", ESP_LOG_WARN);

    /* 1. Init NVS (needed by WiFi, we'll use later) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* 2. Boot the kernel (inits UART + prints banner) */
    kernel_boot();
    shell_serial_flush_tx();
    esp_log_level_set("*", ESP_LOG_INFO);

    /* 3. Spawn the shell task (unicore targets e.g. esp32c6: no core affinity) */
#if CONFIG_FREERTOS_UNICORE
    xTaskCreate(shell_task, "shell_task", 8192, NULL, 5, NULL);
#else
    xTaskCreatePinnedToCore(shell_task, "shell_task", 8192, NULL, 5, NULL, 1);
#endif

    /* app_main returns — that is fine in ESP-IDF */
}
