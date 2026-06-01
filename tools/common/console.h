#ifndef SLP_CONSOLE_H
#define SLP_CONSOLE_H

#include "slp_vm.h"
#include <stdbool.h>

typedef struct SlpConsole SlpConsole;

/* Extensibility Callbacks */
typedef bool (*SlpConsoleCommandHandler)(SlpConsole *console, const char *command, const char *args);
typedef const char *(*SlpConsolePromptHandler)(SlpConsole *console);
typedef void (*SlpConsoleCompletionHandler)(SlpConsole *console, const char *buf, int pos, void *lc);
typedef char *(*SlpConsoleHintsHandler)(SlpConsole *console, const char *buf);

struct SlpConsole {
    SlpVM *vm;
    bool interact_mode;
    char *buffer;
    size_t buf_len;

    /* Custom Hooks */
    SlpConsoleCommandHandler command_handler;       /* Processes custom commands; returns true if handled */
    SlpConsolePromptHandler prompt_handler;         /* Dynamic prompt generator */
    SlpConsoleCompletionHandler completion_handler; /* Custom tab autocomplete logic */
    SlpConsoleHintsHandler hints_handler;           /* Custom signature helper hints */

    void *userdata;                                 /* Embedder state (e.g. ReplState in aggressor) */
};

/* Construction / Destruction */
SlpConsole *slp_console_new(SlpVM *vm);
void slp_console_free(SlpConsole *console);

/* Default Console Actions (Shared by repl engines) */
SlpValue slp_console_eval(SlpConsole *console, const char *source, bool *success);
void slp_console_load(SlpConsole *console, const char *filename);
void slp_console_tree(SlpConsole *console, const char *name);
void slp_console_env(SlpConsole *console, const char *type, const char *filter);
void slp_console_print_value(SlpValue value);

/* Main REPL Runner */
void slp_console_run(SlpConsole *console, const char *history_file);

/* Lexical Balance Checker */
int console_check_balance(const char *str, int *braces, int *parens, int *brackets);

#endif /* SLP_CONSOLE_H */
