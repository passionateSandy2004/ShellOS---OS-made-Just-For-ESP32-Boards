#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

/* ─────────────────────────────────────────
   Shell Config
   ───────────────────────────────────────── */
#define SHELL_MAX_CMD_LEN    512
#define SHELL_MAX_ARGS       24
#define SHELL_MAX_COMMANDS   48
/* Static label; actual prompt includes cwd when LittleFS is mounted (see shell_run) */
#define SHELL_PROMPT         "ShellOS> "

/* ─────────────────────────────────────────
   Command Structure
   Each command has a name, description,
   and a function pointer to its handler
   ───────────────────────────────────────── */
typedef struct {
    const char *name;
    const char *description;
    const char *usage;
    void (*handler)(int argc, char **argv);
} shell_cmd_t;

/* ─────────────────────────────────────────
   API
   ───────────────────────────────────────── */
void shell_init(void);
void shell_run(void);                              /* blocking loop */
void shell_register_cmd(shell_cmd_t cmd);
void shell_execute(char *line);                    /* parse and run (mutex) */
void shell_dispatch_line(char *line);              /* same as execute, no mutex — for scripts only */
void shell_run_script(const char *user_path);      /* run script file line-by-line */
void shell_autorun_from_config(void);             /* if config/autorun.cfg exists, run script path */
void shell_print_prompt_now(void);                 /* prompt on current shell I/O (UART or TCP) */

/* Utilities available to command handlers */
void shell_print_table_row(const char *key, const char *value);
void shell_print_separator(void);

#endif // SHELL_H
