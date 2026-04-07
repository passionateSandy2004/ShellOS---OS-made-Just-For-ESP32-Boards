#include "shell.h"
#include "shell_io.h"
#include "shell_theme.h"
#include "uart_driver.h"
#include "shell_fs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static SemaphoreHandle_t s_shell_exe_mutex;

void shell_init(void)
{
    shell_io_init();
}

/* ─────────────────────────────────────────
   Prompt: ShellOS [cwd]>  (cwd from LittleFS)
   ───────────────────────────────────────── */
void shell_print_prompt_now(void)
{
    const char *cwd = shell_fs_getcwd();
    if (shell_fs_mounted() && cwd && cwd[0] != '\0') {
        shell_io_printf(THEME_PROMPT_OS "ShellOS" TERM_RESET " " THEME_PROMPT_PATH "%s" TERM_RESET " "
                        THEME_PROMPT_ARROW ">" TERM_RESET " ",
                        cwd);
    } else {
        shell_io_printf(THEME_PROMPT_OS "ShellOS" TERM_RESET " " THEME_PROMPT_ARROW ">" TERM_RESET " ");
    }
}

/* ─────────────────────────────────────────
   Internal command registry
   ───────────────────────────────────────── */
shell_cmd_t cmd_table[SHELL_MAX_COMMANDS];
int         cmd_count = 0;

/* ─────────────────────────────────────────
   Register a command into the table
   ───────────────────────────────────────── */
void shell_register_cmd(shell_cmd_t cmd)
{
    if (cmd_count < SHELL_MAX_COMMANDS) {
        cmd_table[cmd_count++] = cmd;
    }
}

/* ─────────────────────────────────────────
   Tokenise with double-quote support (POSIX shell–like)
   ───────────────────────────────────────── */
static int shell_tokenize(char *line, char **argv, int max_argv)
{
    int   argc = 0;
    char *p     = line;

    while (*p && argc < max_argv) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') {
                p++;
            }
            if (*p == '"') {
                *p++ = '\0';
            }
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') {
                p++;
            }
            if (*p) {
                *p++ = '\0';
            }
        }
    }
    return argc;
}

/* ─────────────────────────────────────────
   Dispatch one line (caller must NOT hold s_shell_exe_mutex)
   Used by shell_execute and shell_run_script to avoid deadlock on nested run
   ───────────────────────────────────────── */
void shell_dispatch_line(char *line)
{
    if (!line || line[0] == '\0') {
        return;
    }

    char *argv[SHELL_MAX_ARGS];
    int   argc = shell_tokenize(line, argv, SHELL_MAX_ARGS);
    if (argc == 0) {
        return;
    }

    for (int i = 0; i < cmd_count; i++) {
        if (strcmp(argv[0], cmd_table[i].name) == 0) {
            cmd_table[i].handler(argc, argv);
            return;
        }
    }

    shell_io_printf("  " TERM_FG_RED "!" TERM_RESET " " TERM_BOLD "Unknown:" TERM_RESET " " TERM_FG_YELLOW "%s" TERM_RESET " " TERM_FG_MUTED "(type 'help')" TERM_RESET "\r\n", argv[0]);
}

/* ─────────────────────────────────────────
   Parse line into argc/argv then dispatch (mutex for interactive / TCP)
   ───────────────────────────────────────── */
void shell_execute(char *line)
{
    if (!line || line[0] == '\0') {
        return;
    }

    if (!s_shell_exe_mutex) {
        s_shell_exe_mutex = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_shell_exe_mutex, portMAX_DELAY);
    shell_dispatch_line(line);
    xSemaphoreGive(s_shell_exe_mutex);
}

/* ─────────────────────────────────────────
   Run a script file: one command per line, # comments, skip blanks
   ───────────────────────────────────────── */
void shell_run_script(const char *user_path)
{
    if (!user_path || !shell_fs_ok()) {
        return;
    }

    char abs[SHELL_FS_PATH_MAX];
    if (shell_fs_resolve(user_path, abs, sizeof(abs)) != ESP_OK) {
        shell_io_println("  run: invalid path.");
        return;
    }

    shell_fs_lock();
    FILE *f = fopen(abs, "r");
    shell_fs_unlock();
    if (!f) {
        shell_io_printf("  run: cannot open '%s'\r\n", user_path);
        return;
    }

    char raw[SHELL_MAX_CMD_LEN];
    while (fgets(raw, sizeof(raw), f)) {
        char *s = raw;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        size_t n = strlen(s);
        while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
            s[--n] = '\0';
        }
        if (*s == '\0' || *s == '#') {
            continue;
        }

        char linebuf[SHELL_MAX_CMD_LEN];
        strncpy(linebuf, s, sizeof(linebuf) - 1);
        linebuf[sizeof(linebuf) - 1] = '\0';

        shell_dispatch_line(linebuf);
    }

    fclose(f);
}

/* ─────────────────────────────────────────
   Autorun: first non-empty, non-# line in config/autorun.cfg = script path
   ───────────────────────────────────────── */
void shell_autorun_from_config(void)
{
    if (!shell_fs_ok()) {
        return;
    }

    char cfg_abs[SHELL_FS_PATH_MAX];
    if (shell_fs_resolve("config/autorun.cfg", cfg_abs, sizeof(cfg_abs)) != ESP_OK) {
        return;
    }

    shell_fs_lock();
    FILE *f = fopen(cfg_abs, "r");
    shell_fs_unlock();
    if (!f) {
        return;
    }

    char line[SHELL_MAX_CMD_LEN];
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        size_t n = strlen(s);
        while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
            s[--n] = '\0';
        }
        if (*s == '\0' || *s == '#') {
            continue;
        }
        fclose(f);
        shell_io_printf("  [autorun] %s\r\n", s);
        shell_run_script(s);
        return;
    }

    fclose(f);
}

/* ─────────────────────────────────────────
   Shell loop — never returns
   Prints prompt, reads line, executes it
   ───────────────────────────────────────── */
void shell_run(void)
{
    char line_buf[SHELL_MAX_CMD_LEN];

    while (1) {
        /* Own UART for this session: netsh may have left I/O bound to TCP while we blocked in readline */
        shell_io_bind_uart();
        shell_print_prompt_now();
        uart_readline(line_buf, sizeof(line_buf));
        shell_io_bind_uart();
        shell_execute(line_buf);
    }
}

/* ─────────────────────────────────────────
   Utility: pretty table row
   e.g.  "  CPU Cores     : 2"
   ───────────────────────────────────────── */
void shell_print_table_row(const char *key, const char *value)
{
    shell_io_printf("  " THEME_KEY "%-16s" TERM_RESET " " THEME_GLYPH ":" TERM_RESET " " THEME_VAL "%s" TERM_RESET "\r\n",
                    key, value);
}

void shell_print_separator(void)
{
    shell_io_println("  " TERM_FG_MUTED "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" TERM_RESET);
}
