#include "kernel.h"
#include "uart_driver.h"
#include "shell_fs.h"
#include "wifi_driver.h"
#include "netsh_server.h"
#include "http_upload.h"
#include "pkg_manager.h"
#include "lua_runtime.h"
#include "shell_theme.h"
#include <stdio.h>
#include <string.h>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ─────────────────────────────────────────
   WIFI AUTO-CONNECT
   Reads /root/config/wifi.cfg, parses
   ssid=<...> and pass=<...> lines, connects.
   ───────────────────────────────────────── */
void kernel_wifi_autoconnect(void)
{
    if (!shell_fs_ok()) return;

    char cfg_path[SHELL_FS_PATH_MAX];
    if (shell_fs_resolve("config/wifi.cfg", cfg_path, sizeof(cfg_path)) != ESP_OK) return;

    FILE *f = fopen(cfg_path, "r");
    if (!f) return;

    char ssid[WIFI_SSID_MAX_LEN] = {0};
    char pass[WIFI_PASS_MAX_LEN] = {0};

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        /* strip trailing newline / carriage return */
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) {
            line[--n] = '\0';
        }

        /* skip comments and blank lines */
        if (line[0] == '#' || line[0] == '\0') continue;

        /* support both:
         *   ssid=MyNet               (key=value, one per line)
         *   ssid=MyNet, pass=secret  (inline, comma-separated)
         */
        char *tok = line;
        char *next;
        do {
            /* skip leading spaces */
            while (*tok == ' ' || *tok == '\t') tok++;
            next = strchr(tok, ',');
            if (next) *next = '\0';

            char *eq = strchr(tok, '=');
            if (eq) {
                *eq = '\0';
                char *key = tok;
                char *val = eq + 1;
                /* trim spaces around key/val */
                while (*key == ' ') key++;
                while (*val == ' ') val++;
                char *ke = key + strlen(key) - 1;
                while (ke > key && (*ke == ' ' || *ke == '\t')) { *ke-- = '\0'; }

                if (strcmp(key, "ssid") == 0) {
                    strncpy(ssid, val, sizeof(ssid) - 1);
                } else if (strcmp(key, "pass") == 0) {
                    strncpy(pass, val, sizeof(pass) - 1);
                }
            }
            tok = next ? next + 1 : NULL;
        } while (tok);
    }
    fclose(f);

    if (ssid[0] == '\0') {
        uart_printf("  " TERM_FG_MUTED "[WiFi]" TERM_RESET " No SSID in " TERM_FG_CYAN "config/wifi.cfg" TERM_RESET " — skipping.\r\n");
        return;
    }

    uart_printf("  " TERM_FG_MUTED "[WiFi]" TERM_RESET " Connecting to " TERM_BOLD TERM_FG_YELLOW "%s" TERM_RESET " ...\r\n", ssid);
    wifi_driver_init();
    if (wifi_driver_connect(ssid, pass)) {
        char ip[WIFI_IP_MAX_LEN];
        wifi_driver_get_ip(ip);
        uart_printf("  " TERM_FG_GREEN TERM_BOLD "[OK]" TERM_RESET " " TERM_FG_CYAN "[WiFi]" TERM_RESET " Connected — IP " TERM_BOLD TERM_FG_NEON_GOLD "%s" TERM_RESET "\r\n", ip);
        if (netsh_server_start(NETSH_DEFAULT_PORT) == ESP_OK) {
            uart_printf("  " TERM_FG_GREEN TERM_BOLD "[OK]" TERM_RESET " " TERM_FG_MAGENTA "[TCP]" TERM_RESET " Shell listening on " TERM_BOLD TERM_FG_NEON_CYAN ":%u" TERM_RESET "\r\n", (unsigned)NETSH_DEFAULT_PORT);
        }
        if (http_upload_server_start() == ESP_OK) {
            uart_printf("  " TERM_FG_GREEN TERM_BOLD "[OK]" TERM_RESET " " TERM_FG_CYAN "[HTTP]" TERM_RESET " Package API on " TERM_BOLD TERM_FG_NEON_CYAN ":%u" TERM_RESET "\r\n", (unsigned)HTTP_UPLOAD_PORT);
        }
    } else {
        uart_printf("  " THEME_TAG_FAIL " " TERM_FG_RED "[WiFi]" TERM_RESET " Connection failed. Check " TERM_FG_CYAN "config/wifi.cfg" TERM_RESET ".\r\n");
    }
}

/* ─────────────────────────────────────────
   KERNEL BOOT
   Initializes hardware and prints banner
   ───────────────────────────────────────── */
void kernel_boot(void)
{
    /* 1. Init UART terminal first so we can print */
    uart_driver_init();

    /* 2. Mount LittleFS (Phase 1 — persistent storage) */
    if (!shell_fs_init()) {
        uart_println("  [WARN] LittleFS mount failed — file commands disabled");
    }

    /* 3a. Initialize Lua runtime + package manager */
    lua_runtime_init();
    pkg_manager_init();

    /* 3b. Small delay for serial monitor to connect */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 4. Print the OS banner */
    kernel_print_banner();

    /* 5. Auto-connect WiFi from config/wifi.cfg */
    kernel_wifi_autoconnect();
}

/* ─────────────────────────────────────────
   BANNER
   Printed once at boot — the "face" of the OS
   ───────────────────────────────────────── */
void kernel_print_banner(void)
{
    uart_println("");
    uart_println("  " BANNER_L1 "███████╗██╗  ██╗███████╗██╗     ██╗      ██████╗ ███████╗" TERM_RESET);
    uart_println("  " BANNER_L2 "██╔════╝██║  ██║██╔════╝██║     ██║     ██╔═══██╗██╔════╝" TERM_RESET);
    uart_println("  " BANNER_L3 "███████╗███████║█████╗  ██║     ██║     ██║   ██║███████╗" TERM_RESET);
    uart_println("  " BANNER_L4 "╚════██║██╔══██║██╔══╝  ██║     ██║     ██║   ██║╚════██║" TERM_RESET);
    uart_println("  " BANNER_L5 "███████║██║  ██║███████╗███████╗███████╗╚██████╔╝███████║" TERM_RESET);
    uart_println("  " BANNER_L6 "╚══════╝╚═╝  ╚═╝╚══════╝╚══════╝╚══════╝ ╚═════╝ ╚══════╝" TERM_RESET);
    uart_println("");
    uart_println("  " THEME_BORDER "┌─────────────────────────────────────────────────────┐" TERM_RESET);
    uart_println("  " THEME_BORDER "│" TERM_RESET "   " THEME_TITLE OS_NAME " v" OS_VERSION TERM_RESET TERM_DIM " — " OS_EDITION TERM_RESET "                  " THEME_BORDER "│" TERM_RESET);
    uart_println("  " THEME_BORDER "│" TERM_RESET "   " THEME_KEY "Arch" TERM_RESET " : " THEME_VAL OS_ARCH "                 " THEME_BORDER "│" TERM_RESET);
    uart_println("  " THEME_BORDER "│" TERM_RESET "   " THEME_SUBTITLE OS_AUTHOR TERM_RESET "                          " THEME_BORDER "│" TERM_RESET);
    uart_println("  " THEME_BORDER "└─────────────────────────────────────────────────────┘" TERM_RESET);
    uart_println("");
    uart_printf("  " TERM_FG_YELLOW TERM_BOLD "* " TERM_RESET TERM_FG_MUTED "Kernel booting" TERM_RESET " ...\r\n");
    uart_printf("  " THEME_TAG_OK " " TERM_FG_MUTED "UART driver" TERM_RESET " initialized\r\n");
    if (shell_fs_ok()) {
        uart_printf("  " THEME_TAG_OK " " TERM_FG_MUTED "LittleFS mounted at" TERM_RESET " " TERM_FG_NEON_CYAN "/root" TERM_RESET "\r\n");
    } else {
        uart_printf("  " THEME_TAG_WARN " " TERM_FG_RED "Storage unavailable" TERM_RESET "\r\n");
    }
    uart_printf("  " THEME_TAG_OK " " TERM_FG_MUTED "Heap ready" TERM_RESET "\r\n");
    uart_printf("  " THEME_TAG_OK " " TERM_FG_MUTED "Shell loaded" TERM_RESET "\r\n");
    uart_println("");
    uart_printf("  " TERM_FG_NEON_GOLD "->" TERM_RESET " Type " TERM_BOLD TERM_FG_CYAN "help" TERM_RESET " for commands  " TERM_FG_MUTED "|" TERM_RESET "  " TERM_FG_MUTED "TCP shell when WiFi is up" TERM_RESET "\r\n");
    uart_println("");
}

/* ─────────────────────────────────────────
   SYSINFO
   Fills a sys_info_t struct with live data
   ───────────────────────────────────────── */
void kernel_get_sysinfo(sys_info_t *info)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    info->free_heap     = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    info->total_heap    = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    info->free_psram    = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    info->total_psram   = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    info->flash_size    = flash_size / (1024 * 1024);   /* in MB */
    info->cpu_cores     = chip.cores;
    info->cpu_freq_mhz  = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    info->uptime_ms     = (uint32_t)(esp_timer_get_time() / 1000);
}
