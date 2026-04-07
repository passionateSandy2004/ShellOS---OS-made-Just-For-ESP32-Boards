#include "commands.h"
#include "shell.h"
#include "kernel.h"
#include "shell_io.h"
#include "netsh_server.h"
#include "http_fetch.h"
#include "wifi_driver.h"
#include "esp_err.h"
#include "cam_driver.h"
#include "shell_fs.h"
#include "shell_theme.h"
#include "pkg_manager.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

extern shell_cmd_t cmd_table[];
extern int         cmd_count;

#define FLASH_LED_PIN 4

/* ═══════════════════════════════════════════
   COMMAND: help
   ═══════════════════════════════════════════ */
static void cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_io_println("");
    shell_io_println("  " THEME_BORDER "┌─────────────────────────────────────────────────┐" TERM_RESET);
    shell_io_println("  " THEME_BORDER "│" TERM_RESET "  " THEME_TITLE "ShellOS" TERM_RESET " " TERM_DIM "command palette" TERM_RESET "                         " THEME_BORDER "│" TERM_RESET);
    shell_io_println("  " THEME_BORDER "└─────────────────────────────────────────────────┘" TERM_RESET);
    shell_io_println("");
    for (int i = 0; i < cmd_count; i++) {
        shell_io_printf("  " THEME_KEY "%-14s" TERM_RESET " " TERM_FG_MUTED "\xC2\xB7" TERM_RESET " %s\r\n", cmd_table[i].name, cmd_table[i].description);
    }
    shell_io_println("");
    shell_io_printf("  " TERM_FG_MUTED "%d commands registered" TERM_RESET "\r\n", cmd_count);
    shell_io_println("");
}

/* ═══════════════════════════════════════════
   COMMAND: sysinfo
   ═══════════════════════════════════════════ */
static void cmd_sysinfo(int argc, char **argv)
{
    sys_info_t info;
    kernel_get_sysinfo(&info);
    char buf[64];

    shell_io_println("");
    shell_io_println("  " THEME_BORDER "┌─────────────────────────────────────────────────┐" TERM_RESET);
    shell_io_println("  " THEME_BORDER "│" TERM_RESET "  " THEME_TITLE "System information" TERM_RESET "                          " THEME_BORDER "│" TERM_RESET);
    shell_io_println("  " THEME_BORDER "└─────────────────────────────────────────────────┘" TERM_RESET);
    shell_io_println("");

    shell_print_table_row("OS Name",   OS_NAME " v" OS_VERSION);
    shell_print_table_row("Board",     "ESP32-CAM AI Thinker");
    shell_print_table_row("CPU",       OS_ARCH);

    snprintf(buf, sizeof(buf), "%lu MHz", info.cpu_freq_mhz);
    shell_print_table_row("CPU Freq",  buf);

    snprintf(buf, sizeof(buf), "%u cores", info.cpu_cores);
    shell_print_table_row("CPU Cores", buf);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    shell_print_table_row("MAC Addr",  buf);

    shell_print_separator();

    snprintf(buf, sizeof(buf), "%lu KB free / %lu KB total",
             info.free_heap / 1024, info.total_heap / 1024);
    shell_print_table_row("Internal RAM", buf);

    if (info.total_psram > 0) {
        snprintf(buf, sizeof(buf), "%lu KB free / %lu KB total",
                 info.free_psram / 1024, info.total_psram / 1024);
        shell_print_table_row("PSRAM", buf);
    }

    snprintf(buf, sizeof(buf), "%lu MB", info.flash_size);
    shell_print_table_row("Flash", buf);

    if (shell_fs_ok()) {
        size_t tot = 0, used = 0;
        if (shell_fs_info(&tot, &used) == ESP_OK) {
            snprintf(buf, sizeof(buf), "%zu / %zu KB (LittleFS)", used / 1024, tot / 1024);
            shell_print_table_row("Storage", buf);
        }
    } else {
        shell_print_table_row("Storage", "LittleFS not mounted");
    }

    shell_print_separator();

    char ip[WIFI_IP_MAX_LEN];
    if (wifi_driver_is_connected() && wifi_driver_get_ip(ip)) {
        shell_print_table_row("WiFi",       "Connected");
        shell_print_table_row("IP Address", ip);
    } else {
        shell_print_table_row("WiFi", "Not connected");
    }
    shell_print_table_row("Camera", cam_driver_is_ready() ? "Ready" : "Not initialized");

    shell_print_separator();

    uint32_t sec = info.uptime_ms / 1000;
    uint32_t min = sec / 60;
    uint32_t hr  = min / 60;
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", hr, min % 60, sec % 60);
    shell_print_table_row("Uptime", buf);
    shell_io_println("");
}

/* ═══════════════════════════════════════════
   COMMAND: mem
   ═══════════════════════════════════════════ */
static void cmd_mem(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    sys_info_t info;
    kernel_get_sysinfo(&info);
    char buf[64];
    shell_io_println("");
    shell_io_printf("  " THEME_TITLE "Memory map" TERM_RESET "\r\n");
    shell_print_separator();
    uint32_t used_heap = info.total_heap - info.free_heap;
    snprintf(buf, sizeof(buf), "%lu / %lu KB used", used_heap / 1024, info.total_heap / 1024);
    shell_print_table_row("Internal RAM", buf);
    if (info.total_psram > 0) {
        uint32_t used_psram = info.total_psram - info.free_psram;
        snprintf(buf, sizeof(buf), "%lu / %lu KB used", used_psram / 1024, info.total_psram / 1024);
        shell_print_table_row("PSRAM", buf);
    }
    shell_io_println("");
}

/* ═══════════════════════════════════════════
   COMMAND: uptime
   ═══════════════════════════════════════════ */
static void cmd_uptime(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t ms  = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t sec = ms / 1000;
    uint32_t min = sec / 60;
    uint32_t hr  = min / 60;
    shell_io_printf("\r\n  " TERM_FG_MUTED "Uptime" TERM_RESET " " TERM_BOLD TERM_FG_NEON_GOLD "%02lu:%02lu:%02lu" TERM_RESET "\r\n\r\n", hr, min % 60, sec % 60);
}

/* ═══════════════════════════════════════════
   COMMAND: echo
   ═══════════════════════════════════════════ */
static void cmd_echo(int argc, char **argv)
{
    shell_io_print("  ");
    for (int i = 1; i < argc; i++) {
        shell_io_print(argv[i]);
        if (i < argc - 1) shell_io_putchar(' ');
    }
    shell_io_println("");
}

/* ═══════════════════════════════════════════
   COMMAND: run — Phase 4 script runner
   ═══════════════════════════════════════════ */
static void cmd_run(int argc, char **argv)
{
    if (argc < 2) {
        shell_io_println("  Usage: run <script>");
        shell_io_println("  Runs commands from a file (one per line, # comments).");
        return;
    }
    if (!shell_fs_ok()) {
        shell_io_println("  Storage not available.");
        return;
    }
    shell_run_script(argv[1]);
}

/* ═══════════════════════════════════════════
   COMMAND: log — append line to logs/shell.log
   ═══════════════════════════════════════════ */
static void cmd_log(int argc, char **argv)
{
    if (argc < 2) {
        shell_io_println("  Usage: log <text...>");
        shell_io_println("  Appends to logs/shell.log with boot-relative timestamp.");
        return;
    }
    if (!shell_fs_ok()) {
        shell_io_println("  Storage not available.");
        return;
    }
    char abs_path[SHELL_FS_PATH_MAX];
    if (shell_fs_resolve("logs/shell.log", abs_path, sizeof(abs_path)) != ESP_OK) {
        shell_io_println("  Invalid path.");
        return;
    }
    shell_fs_lock();
    (void)shell_fs_mkdir("logs", true);
    FILE *fp = fopen(abs_path, "a");
    if (!fp) {
        shell_fs_unlock();
        shell_io_println("  log: cannot open logs/shell.log");
        return;
    }
    uint64_t us = esp_timer_get_time();
    uint32_t sec = (uint32_t)(us / 1000000ULL);
    fprintf(fp, "[%10lu s] ", (unsigned long)sec);
    for (int i = 1; i < argc; i++) {
        fprintf(fp, "%s%s", argv[i], (i < argc - 1) ? " " : "");
    }
    fprintf(fp, "\n");
    fclose(fp);
    shell_fs_unlock();
    shell_io_println("  [OK] Logged.");
}

/* ═══════════════════════════════════════════
   COMMAND: led
   ═══════════════════════════════════════════ */
static void cmd_led(int argc, char **argv)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << FLASH_LED_PIN), .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    if (argc < 2) { shell_io_println("  Usage: led <on|off|blink [n]>"); return; }
    if (strcmp(argv[1], "on") == 0) {
        gpio_set_level(FLASH_LED_PIN, 1); shell_io_println("  Flash LED ON");
    } else if (strcmp(argv[1], "off") == 0) {
        gpio_set_level(FLASH_LED_PIN, 0); shell_io_println("  Flash LED OFF");
    } else if (strcmp(argv[1], "blink") == 0) {
        int count = (argc >= 3) ? atoi(argv[2]) : 3;
        shell_io_printf("  Blinking %d times...\r\n", count);
        for (int i = 0; i < count; i++) {
            gpio_set_level(FLASH_LED_PIN, 1); vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(FLASH_LED_PIN, 0); vTaskDelay(pdMS_TO_TICKS(200));
        }
        shell_io_println("  Done.");
    } else { shell_io_printf("  Unknown: %s\r\n", argv[1]); }
}

/* ═══════════════════════════════════════════
   COMMAND: gpio
   ═══════════════════════════════════════════ */
static void cmd_gpio(int argc, char **argv)
{
    if (argc < 3) { shell_io_println("  Usage: gpio <pin> <high|low|read>"); return; }
    int pin = atoi(argv[1]);
    if (strcmp(argv[2], "high") == 0 || strcmp(argv[2], "low") == 0) {
        gpio_config_t c = { .pin_bit_mask=(1ULL<<pin), .mode=GPIO_MODE_OUTPUT,
            .pull_up_en=GPIO_PULLUP_DISABLE, .pull_down_en=GPIO_PULLDOWN_DISABLE,
            .intr_type=GPIO_INTR_DISABLE };
        gpio_config(&c);
        int level = (strcmp(argv[2], "high") == 0) ? 1 : 0;
        gpio_set_level(pin, level);
        shell_io_printf("  GPIO%d set to %s\r\n", pin, argv[2]);
    } else if (strcmp(argv[2], "read") == 0) {
        gpio_config_t c = { .pin_bit_mask=(1ULL<<pin), .mode=GPIO_MODE_INPUT,
            .pull_up_en=GPIO_PULLUP_ENABLE, .pull_down_en=GPIO_PULLDOWN_DISABLE,
            .intr_type=GPIO_INTR_DISABLE };
        gpio_config(&c);
        int level = gpio_get_level(pin);
        shell_io_printf("  GPIO%d = %s (%d)\r\n", pin, level ? "HIGH" : "LOW", level);
    } else { shell_io_println("  Usage: gpio <pin> <high|low|read>"); }
}

/* ═══════════════════════════════════════════
   COMMAND: tasks
   ═══════════════════════════════════════════ */
static void cmd_tasks(int argc, char **argv)
{
    shell_io_println("");
    shell_io_printf("  Running tasks: %u\r\n", (unsigned)uxTaskGetNumberOfTasks());
    shell_io_println("");
}

/* ═══════════════════════════════════════════
   COMMAND: clear / reset / version
   ═══════════════════════════════════════════ */
static void cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_io_print(TERM_CLS_FULL);
}
static void cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_io_println("");
    shell_io_println("  " THEME_BORDER "+----------------------------------------------+" TERM_RESET);
    shell_io_println("  " THEME_BORDER "|" TERM_RESET "  " THEME_TITLE OS_NAME TERM_RESET " " TERM_DIM "v" OS_VERSION TERM_RESET "  " TERM_FG_MUTED "ESP32-CAM AI Thinker" TERM_RESET "   " THEME_BORDER "|" TERM_RESET);
    shell_io_println("  " THEME_BORDER "+----------------------------------------------+" TERM_RESET);
    shell_io_println("");
}
static void cmd_reset(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_io_printf("  " TERM_FG_YELLOW "*" TERM_RESET " " TERM_FG_RED TERM_BOLD "Rebooting" TERM_RESET " ...\r\n");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

/* ═══════════════════════════════════════════
   COMMAND: wifi
   wifi scan | connect <ssid> <pass> | disconnect | status
   ═══════════════════════════════════════════ */
static void cmd_wifi(int argc, char **argv)
{
    if (argc < 2) {
        shell_io_println("  Usage: wifi <scan|connect <ssid> <pass>|disconnect|status>");
        return;
    }

    if (strcmp(argv[1], "scan") == 0) {
        shell_io_printf("\r\n  " TERM_FG_MUTED "Scanning" TERM_RESET " " TERM_DIM "(~3s)" TERM_RESET " ...\r\n");
        wifi_scan_result_t results[WIFI_MAX_SCAN_RESULTS];
        int count = wifi_driver_scan(results, WIFI_MAX_SCAN_RESULTS);
        shell_io_println("");
        shell_io_printf("  " THEME_TITLE "Found" TERM_RESET " " TERM_BOLD TERM_FG_NEON_CYAN "%d" TERM_RESET " networks\r\n\r\n", count);
        shell_io_printf("  " THEME_KEY "%-5s" TERM_RESET " " THEME_KEY "%-32s" TERM_RESET " " THEME_KEY "Security" TERM_RESET "\r\n", "RSSI", "SSID");
        shell_print_separator();
        for (int i = 0; i < count; i++) {
            const char *auth = (results[i].authmode == 0) ? TERM_FG_GREEN "Open" TERM_RESET : TERM_FG_YELLOW "Secured" TERM_RESET;
            shell_io_printf("  " TERM_FG_NEON_GOLD "%-5d" TERM_RESET " " TERM_FG_WHITE "%-32s" TERM_RESET " %s\r\n", results[i].rssi, results[i].ssid, auth);
        }
        shell_io_println("");
    }
    else if (strcmp(argv[1], "connect") == 0) {
        if (argc < 4) { shell_io_println("  Usage: wifi connect <ssid> <password>"); return; }
        shell_io_printf("\r\n  " TERM_FG_MUTED "Connecting to" TERM_RESET " " TERM_BOLD TERM_FG_YELLOW "%s" TERM_RESET " ...\r\n", argv[2]);
        if (wifi_driver_connect(argv[2], argv[3])) {
            char ip[WIFI_IP_MAX_LEN];
            wifi_driver_get_ip(ip);
            shell_io_printf("  " THEME_TAG_OK " " TERM_FG_CYAN "WiFi" TERM_RESET " connected  " TERM_FG_MUTED "IP" TERM_RESET " " TERM_BOLD TERM_FG_NEON_GOLD "%s" TERM_RESET "\r\n", ip);
            if (!netsh_server_is_running()) {
                if (netsh_server_start(NETSH_DEFAULT_PORT) == ESP_OK) {
                    shell_io_printf("  " THEME_TAG_OK " " TERM_FG_MAGENTA "TCP" TERM_RESET " shell " TERM_FG_NEON_CYAN ":%u" TERM_RESET "\r\n", (unsigned)NETSH_DEFAULT_PORT);
                }
            }
            shell_io_println("");
        } else {
            shell_io_printf("  " THEME_TAG_FAIL " " TERM_FG_RED "Could not connect." TERM_RESET " Check SSID/password.\r\n\r\n");
        }
    }
    else if (strcmp(argv[1], "disconnect") == 0) {
        netsh_server_stop();
        wifi_driver_disconnect();
        shell_io_println("\r\n  Disconnected.\r\n");
    }
    else if (strcmp(argv[1], "status") == 0) {
        shell_io_println("");
        if (wifi_driver_is_connected()) {
            char ip[WIFI_IP_MAX_LEN];
            wifi_driver_get_ip(ip);
            shell_print_table_row("Status",     "Connected");
            shell_print_table_row("IP Address", ip);
        } else {
            shell_print_table_row("Status", "Not connected");
        }
        shell_io_println("");
    }
    else {
        shell_io_printf("  Unknown: %s\r\n", argv[1]);
    }
}

/* ═══════════════════════════════════════════
   COMMAND: cam
   cam init | capture | info | stream
   ═══════════════════════════════════════════ */
static void cmd_cam(int argc, char **argv)
{
    if (argc < 2) {
        shell_io_println("  Usage: cam <init|capture|info|stream>");
        return;
    }

    if (strcmp(argv[1], "init") == 0) {
        shell_io_println("\r\n  Initializing OV2640 camera...");
        if (cam_driver_init()) {
            shell_io_println("  [OK] Camera ready!");
            shell_io_printf("  Resolution : %s\r\n\r\n", cam_resolution_name());
        } else {
            shell_io_println("  [FAIL] Camera init failed.");
            shell_io_println("  Check ribbon cable connection.\r\n");
        }
    }
    else if (strcmp(argv[1], "capture") == 0) {
        if (!cam_driver_is_ready()) {
            shell_io_println("  Camera not initialized. Run: cam init");
            return;
        }
        shell_io_println("\r\n  Capturing photo...");

        /* Flash LED during capture */
        gpio_config_t io_conf = { .pin_bit_mask=(1ULL<<FLASH_LED_PIN),
            .mode=GPIO_MODE_OUTPUT, .pull_up_en=GPIO_PULLUP_DISABLE,
            .pull_down_en=GPIO_PULLDOWN_DISABLE, .intr_type=GPIO_INTR_DISABLE };
        gpio_config(&io_conf);
        gpio_set_level(FLASH_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));

        cam_frame_t *frame = cam_capture();

        gpio_set_level(FLASH_LED_PIN, 0);

        if (!frame) { shell_io_println("  [FAIL] Capture failed.\r\n"); return; }

        shell_io_println("  [OK] Photo captured!");
        shell_io_printf("  Resolution : %s\r\n", cam_resolution_name());
        shell_io_printf("  JPEG size  : %u bytes (%.1f KB)\r\n",
                    (unsigned)frame->len, frame->len / 1024.0f);
        shell_io_println("  Next: use 'cam stream' over WiFi to view it.\r\n");
        cam_frame_free(frame);
    }
    else if (strcmp(argv[1], "stream") == 0) {
        if (!wifi_driver_is_connected()) {
            shell_io_println("  Connect to WiFi first: wifi connect <ssid> <pass>");
            return;
        }
        if (!cam_driver_is_ready()) {
            shell_io_println("  Camera not initialized. Run: cam init");
            return;
        }
        char ip[WIFI_IP_MAX_LEN];
        wifi_driver_get_ip(ip);
        shell_io_println("");
        shell_io_printf("  Stream URL : http://%s/stream\r\n", ip);
        shell_io_println("  HTTP server coming in Phase 3!");
        shell_io_println("");
    }
    else if (strcmp(argv[1], "info") == 0) {
        shell_io_println("");
        shell_print_table_row("Sensor",     "OV2640");
        shell_print_table_row("Resolution", cam_resolution_name());
        shell_print_table_row("Format",     "JPEG");
        shell_print_table_row("Status",     cam_driver_is_ready() ? "Ready" : "Not initialized");
        shell_io_println("");
    }
    else {
        shell_io_printf("  Unknown: %s\r\n", argv[1]);
        shell_io_println("  Usage: cam <init|capture|info|stream>");
    }
}

/* ═══════════════════════════════════════════
   Phase 1 — VFS (LittleFS at /data)
   ═══════════════════════════════════════════ */
static void cmd_ls(int argc, char **argv)
{
    if (!shell_fs_ok()) {
        shell_io_println("  Storage not available.");
        return;
    }
    const char *user = (argc >= 2) ? argv[1] : ".";
    char        abs_path[SHELL_FS_PATH_MAX];
    shell_fs_lock();
    if (shell_fs_resolve(user, abs_path, sizeof(abs_path)) != ESP_OK) {
        shell_io_println("  Invalid path.");
        shell_fs_unlock();
        return;
    }
    DIR *d = opendir(abs_path);
    if (!d) {
        shell_io_printf("  Cannot open: %s\r\n", abs_path);
        shell_fs_unlock();
        return;
    }
    shell_io_println("");
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        char full[SHELL_FS_PATH_MAX];
        size_t al = strlen(abs_path);
        size_t nl = strlen(e->d_name);
        if (al + 1 + nl >= sizeof(full)) {
            continue;
        }
        memcpy(full, abs_path, al);
        full[al] = '/';
        memcpy(full + al + 1, e->d_name, nl + 1);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            shell_io_printf("  %s/\r\n", e->d_name);
        } else {
            shell_io_printf("  %s\r\n", e->d_name);
        }
    }
    closedir(d);
    shell_io_println("");
    shell_fs_unlock();
}

static void cmd_cat(int argc, char **argv)
{
    if (!shell_fs_ok()) {
        shell_io_println("  Storage not available.");
        return;
    }
    if (argc < 2) {
        shell_io_println("  Usage: cat <file>");
        return;
    }
    char abs_path[SHELL_FS_PATH_MAX];
    shell_fs_lock();
    if (shell_fs_resolve(argv[1], abs_path, sizeof(abs_path)) != ESP_OK) {
        shell_io_println("  Invalid path.");
        shell_fs_unlock();
        return;
    }
    FILE *f = fopen(abs_path, "r");
    if (!f) {
        shell_io_printf("  open: %s\r\n", strerror(errno));
        shell_fs_unlock();
        return;
    }
    shell_io_println("");
    char buf[128];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            shell_io_putchar(buf[i]);
        }
    }
    fclose(f);
    shell_io_println("\r\n");
    shell_fs_unlock();
}

static void cmd_write(int argc, char **argv)
{
    if (!shell_fs_ok()) {
        shell_io_println("  Storage not available.");
        return;
    }
    if (argc < 3) {
        shell_io_println("  Usage: write <file> <text...>");
        return;
    }
    char abs_path[SHELL_FS_PATH_MAX];
    shell_fs_lock();
    if (shell_fs_resolve(argv[1], abs_path, sizeof(abs_path)) != ESP_OK) {
        shell_io_println("  Invalid path.");
        shell_fs_unlock();
        return;
    }
    char content[400];
    size_t off = 0;
    for (int i = 2; i < argc && off < sizeof(content) - 1; i++) {
        if (i > 2) {
            content[off++] = ' ';
        }
        const char *s = argv[i];
        while (*s && off < sizeof(content) - 1) {
            content[off++] = *s++;
        }
    }
    content[off] = '\0';

    FILE *f = fopen(abs_path, "w");
    if (!f) {
        shell_io_printf("  write: %s\r\n", strerror(errno));
        shell_fs_unlock();
        return;
    }
    fputs(content, f);
    fclose(f);
    shell_io_println("  [OK] Written.\r\n");
    shell_fs_unlock();
}

static void cmd_rm(int argc, char **argv)
{
    if (!shell_fs_ok()) {
        shell_io_println("  Storage not available.");
        return;
    }
    if (argc < 2) {
        shell_io_println("  Usage: rm <path>");
        return;
    }
    char abs_path[SHELL_FS_PATH_MAX];
    shell_fs_lock();
    if (shell_fs_resolve(argv[1], abs_path, sizeof(abs_path)) != ESP_OK) {
        shell_io_println("  Invalid path.");
        shell_fs_unlock();
        return;
    }
    struct stat st;
    if (stat(abs_path, &st) != 0) {
        shell_io_printf("  %s\r\n", strerror(errno));
        shell_fs_unlock();
        return;
    }
    int err = S_ISDIR(st.st_mode) ? rmdir(abs_path) : unlink(abs_path);
    if (err != 0) {
        shell_io_printf("  rm: %s\r\n", strerror(errno));
    } else {
        shell_io_println("  [OK] Removed.\r\n");
    }
    shell_fs_unlock();
}

static void cmd_cd(int argc, char **argv)
{
    if (!shell_fs_ok()) {
        shell_io_println("  Storage not available.");
        return;
    }
    if (argc < 2) {
        shell_io_println("  Usage: cd <dir>");
        return;
    }
    shell_fs_lock();
    if (shell_fs_chdir(argv[1]) != ESP_OK) {
        shell_io_println("  cd: no such directory or not a directory.");
    }
    shell_fs_unlock();
}

static void cmd_pwd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!shell_fs_ok()) {
        shell_io_println("  Storage not available.");
        return;
    }
    shell_io_println("");
    shell_io_printf("  %s\r\n\r\n", shell_fs_getcwd());
}

static void cmd_mkdir(int argc, char **argv)
{
    if (!shell_fs_ok()) {
        shell_io_println("  Storage not available.");
        return;
    }
    bool parents = false;
    int    pi    = 1;
    if (argc >= 2 && strcmp(argv[1], "-p") == 0) {
        parents = true;
        pi++;
    }
    if (argc <= pi) {
        shell_io_println("  Usage: mkdir [-p] <dir>");
        return;
    }
    shell_fs_lock();
    if (shell_fs_mkdir(argv[pi], parents) != ESP_OK) {
        shell_io_println("  mkdir failed.");
    } else {
        shell_io_println("  [OK]\r\n");
    }
    shell_fs_unlock();
}

static void cmd_mv(int argc, char **argv)
{
    if (!shell_fs_ok()) {
        shell_io_println("  Storage not available.");
        return;
    }
    if (argc < 3) {
        shell_io_println("  Usage: mv <src> <dst>");
        return;
    }
    char a[SHELL_FS_PATH_MAX], b[SHELL_FS_PATH_MAX];
    shell_fs_lock();
    if (shell_fs_resolve(argv[1], a, sizeof(a)) != ESP_OK ||
        shell_fs_resolve(argv[2], b, sizeof(b)) != ESP_OK) {
        shell_io_println("  Invalid path.");
        shell_fs_unlock();
        return;
    }
    if (rename(a, b) != 0) {
        shell_io_printf("  mv: %s\r\n", strerror(errno));
    } else {
        shell_io_println("  [OK]\r\n");
    }
    shell_fs_unlock();
}

static void cmd_cp(int argc, char **argv)
{
    if (!shell_fs_ok()) {
        shell_io_println("  Storage not available.");
        return;
    }
    if (argc < 3) {
        shell_io_println("  Usage: cp <src> <dst>");
        return;
    }
    char a[SHELL_FS_PATH_MAX], b[SHELL_FS_PATH_MAX];
    shell_fs_lock();
    if (shell_fs_resolve(argv[1], a, sizeof(a)) != ESP_OK ||
        shell_fs_resolve(argv[2], b, sizeof(b)) != ESP_OK) {
        shell_io_println("  Invalid path.");
        shell_fs_unlock();
        return;
    }
    FILE *in = fopen(a, "rb");
    if (!in) {
        shell_io_printf("  cp: %s\r\n", strerror(errno));
        shell_fs_unlock();
        return;
    }
    FILE *out = fopen(b, "wb");
    if (!out) {
        fclose(in);
        shell_io_printf("  cp: %s\r\n", strerror(errno));
        shell_fs_unlock();
        return;
    }
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            shell_io_println("  cp: write error.");
            fclose(in);
            fclose(out);
            shell_fs_unlock();
            return;
        }
    }
    fclose(in);
    fclose(out);
    shell_io_println("  [OK]\r\n");
    shell_fs_unlock();
}

static void cmd_touch(int argc, char **argv)
{
    if (!shell_fs_ok()) {
        shell_io_println("  Storage not available.");
        return;
    }
    if (argc < 2) {
        shell_io_println("  Usage: touch <file>");
        return;
    }
    char abs_path[SHELL_FS_PATH_MAX];
    shell_fs_lock();
    if (shell_fs_resolve(argv[1], abs_path, sizeof(abs_path)) != ESP_OK) {
        shell_io_println("  Invalid path.");
        shell_fs_unlock();
        return;
    }
    FILE *f = fopen(abs_path, "a");
    if (!f) {
        shell_io_printf("  touch: %s\r\n", strerror(errno));
        shell_fs_unlock();
        return;
    }
    fclose(f);
    shell_io_println("  [OK]\r\n");
    shell_fs_unlock();
}

static void cmd_df(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!shell_fs_ok()) {
        shell_io_println("  Storage not available.");
        return;
    }
    size_t tot = 0, used = 0;
    shell_fs_lock();
    if (shell_fs_info(&tot, &used) != ESP_OK) {
        shell_io_println("  df: failed.");
        shell_fs_unlock();
        return;
    }
    shell_io_println("");
    shell_io_printf("  Filesystem     Used      Total     Use%%\r\n");
    shell_io_printf("  %-14s %6zu KB  %6zu KB  %3zu%%\r\n",
                "LittleFS", used / 1024, tot / 1024,
                tot ? (used * 100 / tot) : 0);
    shell_io_println("");
    shell_fs_unlock();
}

/* ═══════════════════════════════════════════
   COMMAND: fetch / wget — download URL to LittleFS
   ═══════════════════════════════════════════ */
static void cmd_fetch(int argc, char **argv)
{
    if (argc < 3) {
        shell_io_println("  Usage: fetch <url> <local-path>");
        shell_io_println("  Saves under /root (e.g. downloads/readme.txt).");
        return;
    }
    if (!wifi_driver_is_connected()) {
        shell_io_println("  WiFi not connected.");
        return;
    }
    if (!shell_fs_ok()) {
        shell_io_println("  Storage not available.");
        return;
    }
    char abs_path[SHELL_FS_PATH_MAX];
    if (shell_fs_resolve(argv[2], abs_path, sizeof(abs_path)) != ESP_OK) {
        shell_io_println("  Invalid path.");
        return;
    }
    shell_io_println("  Fetching...");
    esp_err_t err = shell_http_fetch_to_file(argv[1], abs_path);
    if (err == ESP_OK) {
        shell_io_printf("  [OK] Wrote %s\r\n\r\n", argv[2]);
    } else {
        shell_io_printf("  [FAIL] %s\r\n\r\n", esp_err_to_name(err));
    }
}

/* ═══════════════════════════════════════════
   COMMAND: pkg
   Subcommands:
     install <manifest-url>  — legacy manifest-based file download
     deploy  <abs-shpkg>     — install from a .shpkg already on the device
     run     <name>          — start a package as a Lua task
     stop    <name>          — stop a running package
     list                    — list installed packages + run status
     remove  <name>          — stop + delete package
     logs    <name> [lines]  — show package log tail
     info    <name>          — show manifest
     autorun <name> on|off   — toggle autorun at boot
   ═══════════════════════════════════════════ */
static void cmd_pkg(int argc, char **argv)
{
    if (argc < 2) {
        shell_io_println("  Usage: pkg <install|deploy|run|stop|list|remove|logs|info|autorun>");
        return;
    }

    const char *sub = argv[1];

    /* ── pkg install <manifest-url> (legacy) ── */
    if (strcmp(sub, "install") == 0) {
        if (argc < 3) {
            shell_io_println("  Usage: pkg install <manifest-url>");
            shell_io_println("  Manifest lines:  <relative-path> <https-url>");
            return;
        }
        if (!wifi_driver_is_connected()) { shell_io_println("  WiFi not connected."); return; }
        if (!shell_fs_ok())              { shell_io_println("  Storage not available."); return; }
        shell_io_println("  Downloading manifest and files...");
        esp_err_t err = shell_pkg_install_manifest(argv[2]);
        if (err == ESP_OK) shell_io_println("  [OK] Files installed under /root\r\n");
        else shell_io_printf("  [FAIL] %s\r\n\r\n", esp_err_to_name(err));
        return;
    }

    /* ── pkg deploy <abs-path-to-shpkg> ── */
    if (strcmp(sub, "deploy") == 0) {
        if (argc < 3) { shell_io_println("  Usage: pkg deploy <absolute-path-to-.shpkg>"); return; }
        if (!shell_fs_ok()) { shell_io_println("  Storage not available."); return; }
        char abs[SHELL_FS_PATH_MAX];
        if (shell_fs_resolve(argv[2], abs, sizeof(abs)) != ESP_OK) {
            shell_io_println("  Invalid path.");
            return;
        }
        esp_err_t err = pkg_install(abs);
        if (err != ESP_OK) shell_io_printf("  [FAIL] %s\r\n", esp_err_to_name(err));
        return;
    }

    /* ── pkg run <name> ── */
    if (strcmp(sub, "run") == 0) {
        if (argc < 3) { shell_io_println("  Usage: pkg run <name>"); return; }
        pkg_run(argv[2]);
        return;
    }

    /* ── pkg stop <name> ── */
    if (strcmp(sub, "stop") == 0) {
        if (argc < 3) { shell_io_println("  Usage: pkg stop <name>"); return; }
        pkg_stop(argv[2]);
        return;
    }

    /* ── pkg list ── */
    if (strcmp(sub, "list") == 0) {
        char buf[1024];
        pkg_list(buf, sizeof(buf));
        shell_io_println("");
        shell_io_println("  " THEME_BORDER "┌─────────────────────────────────────────────────┐" TERM_RESET);
        shell_io_println("  " THEME_BORDER "│" TERM_RESET "  " THEME_TITLE "Installed packages" TERM_RESET "                          " THEME_BORDER "│" TERM_RESET);
        shell_io_println("  " THEME_BORDER "└─────────────────────────────────────────────────┘" TERM_RESET);
        shell_io_println("");
        shell_io_printf("  " THEME_KEY "%-20s  %-14s  %s" TERM_RESET "\r\n", "Name", "Version", "Status");
        shell_print_separator();
        shell_io_print(buf);
        shell_io_println("");
        return;
    }

    /* ── pkg remove <name> ── */
    if (strcmp(sub, "remove") == 0) {
        if (argc < 3) { shell_io_println("  Usage: pkg remove <name>"); return; }
        pkg_remove(argv[2]);
        return;
    }

    /* ── pkg logs <name> [lines] ── */
    if (strcmp(sub, "logs") == 0) {
        if (argc < 3) { shell_io_println("  Usage: pkg logs <name> [lines]"); return; }
        int lines = (argc >= 4) ? atoi(argv[3]) : 40;
        if (lines <= 0 || lines > 200) lines = 40;
        pkg_logs(argv[2], lines);
        return;
    }

    /* ── pkg info <name> ── */
    if (strcmp(sub, "info") == 0) {
        if (argc < 3) { shell_io_println("  Usage: pkg info <name>"); return; }
        pkg_info(argv[2]);
        return;
    }

    /* ── pkg autorun <name> on|off ── */
    if (strcmp(sub, "autorun") == 0) {
        if (argc < 4) { shell_io_println("  Usage: pkg autorun <name> <on|off>"); return; }
        bool enable = (strcmp(argv[3], "on") == 0);
        pkg_autorun(argv[2], enable);
        return;
    }

    shell_io_printf("  Unknown pkg subcommand: %s\r\n", sub);
    shell_io_println("  pkg <install|deploy|run|stop|list|remove|logs|info|autorun>");
}

/* ═══════════════════════════════════════════
   COMMAND: ping
   ═══════════════════════════════════════════ */
static void cmd_ping(int argc, char **argv)
{
    shell_io_println("");
    if (!wifi_driver_is_connected()) {
        shell_io_println("  Not connected. Use: wifi connect <ssid> <pass>");
    } else {
        char ip[WIFI_IP_MAX_LEN];
        wifi_driver_get_ip(ip);
        shell_print_table_row("Device IP", ip);
        shell_print_table_row("Status",    "Network reachable");
    }
    shell_io_println("");
}

/* ═══════════════════════════════════════════
   COMMAND: netsh — TCP shell starts with WiFi (no "start")
   ═══════════════════════════════════════════ */
static void cmd_netsh(int argc, char **argv)
{
    if (shell_io_is_remote()) {
        shell_io_println("  netsh: use UART console for stop/status.");
        return;
    }
    if (argc < 2) {
        shell_io_println("  Usage: netsh <stop|status>");
        shell_io_println("  TCP shell starts automatically when WiFi connects.");
        return;
    }
    if (strcmp(argv[1], "stop") == 0) {
        netsh_server_stop();
        shell_io_println("  [OK] Stopped.\r\n");
    } else if (strcmp(argv[1], "status") == 0) {
        if (netsh_server_is_running()) {
            shell_io_printf("  netsh: listening on :%u\r\n\r\n", (unsigned)NETSH_DEFAULT_PORT);
        } else {
            shell_io_println("  netsh: stopped (WiFi disconnected or not started)\r\n");
        }
    } else {
        shell_io_println("  Usage: netsh <stop|status>");
        shell_io_println("  (TCP listens on :2323 whenever WiFi is up — no start command.)");
    }
}

/* ═══════════════════════════════════════════
   REGISTER ALL COMMANDS
   ═══════════════════════════════════════════ */
void commands_register_all(void)
{
    shell_register_cmd((shell_cmd_t){"help",    "List all commands",          "help",                         cmd_help});
    shell_register_cmd((shell_cmd_t){"ls",      "List directory",             "ls [path]",                    cmd_ls});
    shell_register_cmd((shell_cmd_t){"cat",     "Print file",                 "cat <file>",                   cmd_cat});
    shell_register_cmd((shell_cmd_t){"write",   "Write text to file",         "write <file> <text...>",       cmd_write});
    shell_register_cmd((shell_cmd_t){"rm",      "Remove file or empty dir",   "rm <path>",                    cmd_rm});
    shell_register_cmd((shell_cmd_t){"cd",      "Change directory",           "cd <dir>",                     cmd_cd});
    shell_register_cmd((shell_cmd_t){"pwd",     "Print working directory",    "pwd",                          cmd_pwd});
    shell_register_cmd((shell_cmd_t){"mkdir",   "Create directory",           "mkdir [-p] <dir>",             cmd_mkdir});
    shell_register_cmd((shell_cmd_t){"mv",      "Rename / move",              "mv <src> <dst>",               cmd_mv});
    shell_register_cmd((shell_cmd_t){"cp",      "Copy file",                  "cp <src> <dst>",               cmd_cp});
    shell_register_cmd((shell_cmd_t){"touch",   "Create empty file",          "touch <file>",                 cmd_touch});
    shell_register_cmd((shell_cmd_t){"df",      "Disk usage (LittleFS)",      "df",                           cmd_df});
    shell_register_cmd((shell_cmd_t){"sysinfo", "Full system information",    "sysinfo",                      cmd_sysinfo});
    shell_register_cmd((shell_cmd_t){"mem",     "Memory usage",               "mem",                          cmd_mem});
    shell_register_cmd((shell_cmd_t){"uptime",  "Time since boot",            "uptime",                       cmd_uptime});
    shell_register_cmd((shell_cmd_t){"version", "OS version",                 "version",                      cmd_version});
    shell_register_cmd((shell_cmd_t){"clear",   "Clear terminal",             "clear",                        cmd_clear});
    shell_register_cmd((shell_cmd_t){"reset",   "Reboot board",               "reset",                        cmd_reset});
    shell_register_cmd((shell_cmd_t){"echo",    "Echo text",                  "echo <text>",                  cmd_echo});
    shell_register_cmd((shell_cmd_t){"run",     "Run script file",            "run <path>",                   cmd_run});
    shell_register_cmd((shell_cmd_t){"log",     "Append to logs/shell.log",   "log <text...>",                cmd_log});
    shell_register_cmd((shell_cmd_t){"tasks",   "Running task count",         "tasks",                        cmd_tasks});
    shell_register_cmd((shell_cmd_t){"led",     "Control flash LED",          "led <on|off|blink [n]>",       cmd_led});
    shell_register_cmd((shell_cmd_t){"gpio",    "Read/write GPIO",            "gpio <pin> <high|low|read>",   cmd_gpio});
    shell_register_cmd((shell_cmd_t){"wifi",    "WiFi commands",              "wifi <scan|connect|disconnect|status>", cmd_wifi});
    shell_register_cmd((shell_cmd_t){"netsh",   "TCP shell stop/status",      "netsh <stop|status>",          cmd_netsh});
    shell_register_cmd((shell_cmd_t){"fetch",   "HTTP(S) download to file",   "fetch <url> <path>",           cmd_fetch});
    shell_register_cmd((shell_cmd_t){"wget",    "Same as fetch",              "wget <url> <path>",            cmd_fetch});
    shell_register_cmd((shell_cmd_t){"pkg",     "Package manager (run/stop/list/logs…)", "pkg <install|deploy|run|stop|list|remove|logs|info|autorun>", cmd_pkg});
    shell_register_cmd((shell_cmd_t){"ping",    "Network connectivity check", "ping",                         cmd_ping});
    shell_register_cmd((shell_cmd_t){"cam",     "Camera commands",            "cam <init|capture|info|stream>", cmd_cam});
}
