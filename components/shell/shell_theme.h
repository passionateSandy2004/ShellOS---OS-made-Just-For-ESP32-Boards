#ifndef SHELL_THEME_H
#define SHELL_THEME_H

/*
 * ANSI theme for ShellOS — assumes a modern terminal (VS Code, PuTTY with ANSI,
 * Windows Terminal, nc/PuTTY Raw on :2323). Plain serial monitors may show escape codes.
 */

#define TERM_CLS_FULL   "\033[2J\033[3J\033[H" /* clear screen + scrollback where supported */
#define TERM_RESET      "\033[0m"
#define TERM_BOLD       "\033[1m"
#define TERM_DIM        "\033[2m"
#define TERM_ITALIC     "\033[3m"

/* Standard bright foregrounds */
#define TERM_FG_BLACK   "\033[30m"
#define TERM_FG_RED     "\033[91m"
#define TERM_FG_GREEN   "\033[92m"
#define TERM_FG_YELLOW  "\033[93m"
#define TERM_FG_BLUE    "\033[94m"
#define TERM_FG_MAGENTA "\033[95m"
#define TERM_FG_CYAN    "\033[96m"
#define TERM_FG_WHITE   "\033[97m"
#define TERM_FG_MUTED   "\033[90m"

/* 256-color accents (neon / modern) */
#define TERM_FG_NEON_CYAN   "\033[38;5;51m"
#define TERM_FG_NEON_GREEN  "\033[38;5;118m"
#define TERM_FG_NEON_GOLD   "\033[38;5;220m"
#define TERM_FG_NEON_ORANGE "\033[38;5;208m"
#define TERM_FG_NEON_PINK   "\033[38;5;213m"
#define TERM_FG_NEON_BLUE   "\033[38;5;39m"
#define TERM_FG_NEON_PURPLE "\033[38;5;141m"

/* Prompt: brand + path + cursor */
#define THEME_PROMPT_OS     TERM_BOLD TERM_FG_NEON_CYAN
#define THEME_PROMPT_PATH   TERM_BOLD TERM_FG_NEON_GOLD
#define THEME_PROMPT_ARROW  TERM_FG_NEON_PINK

/* Tables & panels */
#define THEME_KEY       TERM_FG_CYAN
#define THEME_VAL       TERM_FG_WHITE
#define THEME_VAL_OK    TERM_FG_GREEN
#define THEME_VAL_WARN  TERM_FG_YELLOW
#define THEME_VAL_BAD   TERM_FG_RED
#define THEME_GLYPH     TERM_FG_MUTED
#define THEME_BORDER    TERM_FG_NEON_BLUE
#define THEME_TITLE       TERM_BOLD TERM_FG_MAGENTA
#define THEME_SUBTITLE    TERM_DIM TERM_FG_WHITE

/* Status tags */
#define THEME_TAG_OK    TERM_FG_GREEN TERM_BOLD "[OK]" TERM_RESET
#define THEME_TAG_WARN  TERM_FG_YELLOW TERM_BOLD "[!!]" TERM_RESET
#define THEME_TAG_FAIL  TERM_FG_RED TERM_BOLD "[FAIL]" TERM_RESET

/* Banner ASCII art — per-line hue (6 lines) */
#define BANNER_L1 TERM_FG_NEON_CYAN
#define BANNER_L2 TERM_FG_NEON_BLUE
#define BANNER_L3 TERM_FG_NEON_PURPLE
#define BANNER_L4 TERM_FG_NEON_PINK
#define BANNER_L5 TERM_FG_NEON_ORANGE
#define BANNER_L6 TERM_FG_NEON_GOLD

#endif /* SHELL_THEME_H */
