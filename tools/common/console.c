#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "console.h"
#include "slp_common.h"
#include "slp_core.h"
#include "slp_vm.h"
#include "slp_compiler.h"
#include "slp_disasm.h"
#include "slp_parser.h"
#include "slp_ast.h"
#include "bestline.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static SlpConsole *g_active_console = NULL;

int console_check_balance(const char *str, int *braces, int *parens, int *brackets) {
    int in_string = 0;
    int in_comment = 0;
    for (int i = 0; str[i]; i++) {
        if (in_comment) {
            if (str[i] == '\n') in_comment = 0;
            continue;
        }
        if (in_string) {
            if (str[i] == '\\' && str[i+1] != '\0') i++; // skip escaped
            else if (str[i] == '"') in_string = 0;
            continue;
        }
        if (str[i] == '"') {
            in_string = 1;
            continue;
        }
        if (str[i] == '/' && str[i+1] == '/') {
            in_comment = 1;
            i++;
            continue;
        }
        if (str[i] == '{') (*braces)++;
        else if (str[i] == '}') (*braces)--;
        else if (str[i] == '(') (*parens)++;
        else if (str[i] == ')') (*parens)--;
        else if (str[i] == '[') (*brackets)++;
        else if (str[i] == ']') (*brackets)--;
    }
    return in_string;
}

SlpConsole *slp_console_new(SlpVM *vm) {
    SlpConsole *console = malloc(sizeof(SlpConsole));
    if (console) {
        console->vm = vm;
        console->interact_mode = false;
        console->buffer = NULL;
        console->buf_len = 0;
        console->command_handler = NULL;
        console->prompt_handler = NULL;
        console->completion_handler = NULL;
        console->hints_handler = NULL;
        console->userdata = NULL;
    }
    return console;
}

void slp_console_free(SlpConsole *console) {
    if (console) {
        if (console->buffer) free(console->buffer);
        free(console);
    }
}

SlpValue slp_console_eval(SlpConsole *console, const char *source, bool *success) {
    *success = false;
    SlpValue result = SLP_NULL_VAL;
    SlpVM *vm = console->vm;

    SlpParser parser;
    slp_parser_init(&parser, source, vm->allocator);
    SlpASTNode *ast = slp_parser_parse(&parser);

    if (parser.had_error || !ast) {
        if (vm->error_fn) {
            vm->error_fn(vm->error_userdata, parser.error_line,
                         parser.error_message ? parser.error_message : "Parse error");
        }
        if (ast) slp_parser_free_node(ast, vm->allocator);
        return result;
    }

    SlpObjFunction *fn = slp_compile_ex(vm, ast, true, vm->allocator);
    slp_parser_free_node(ast, vm->allocator);

    if (!fn) return result;

    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->try_handler_count = 0;

    bool had_sub = false;
    SlpBridgeType *bt = vm->bridge_types;
    while (bt) {
        if (strcmp(bt->keyword, "sub") == 0 || strcmp(bt->keyword, "alias") == 0) {
            had_sub = true;
            break;
        }
        bt = bt->next;
    }
    if (!had_sub) {
        slp_vm_register_bridge_type(vm, "sub", NULL, NULL);
    }

    SlpResult r = slp_vm_run(vm, fn);
    if (r == SLP_OK) {
        result = slp_vm_pop(vm);
        *success = true;
    }
    return result;
}

void slp_console_load(SlpConsole *console, const char *filename) {
    char *source = utils_read_file(filename, NULL);
    if (!source) {
        printf("Could not load script %s: File not found or unreadable\n", filename);
        return;
    }
    bool success;
    slp_console_eval(console, source, &success);
    free(source);
}

void slp_console_tree(SlpConsole *console, const char *name) {
    if (!name) {
        printf("Please specify a closure or function name (e.g., &my_sub).\n");
        return;
    }
    SlpVM *vm = console->vm;
    SlpValue key = SLP_OBJ_VAL(slp_vm_copy_cstr(vm, name));
    SlpValue val = slp_obj_hash_get(vm->globals, key);
    if (SLP_IS_NULL(val)) {
        val = slp_obj_hash_get(vm->natives, key);
    }

    if (!SLP_IS_NULL(val) && SLP_IS_OBJ(val)) {
        if (SLP_OBJ_TYPE(val) == SLP_OBJ_CLOSURE) {
            SlpObjClosure *cl = SLP_AS_CLOSURE(val);
            if (cl->function) {
                printf("Disassembly for closure %s:\n", name);
                slp_disasm_chunk(cl->function->chunk, name, stdout);
                return;
            }
        } else if (SLP_OBJ_TYPE(val) == SLP_OBJ_FUNCTION) {
            SlpObjFunction *fn = SLP_AS_FUNCTION(val);
            printf("Disassembly for function %s:\n", name);
            slp_disasm_chunk(fn->chunk, name, stdout);
            return;
        }
    }
    printf("Could not find function/closure '%s' to disassemble.\n", name);
}

void slp_console_env(SlpConsole *console, const char *type, const char *filter) {
    SlpVM *vm = console->vm;

    // 1. Bridges
    if (!type || strcmp(type, "other") == 0) {
        SlpBridgeType *bt = vm->bridge_types;
        while (bt) {
            if (!filter || strstr(bt->keyword, filter)) {
                printf("%-20s => [bridge type]\n", bt->keyword);
            }
            bt = bt->next;
        }
    }

    // 2. Native Functions
    if (!type || strcmp(type, "functions") == 0) {
        SlpObjHash *natives = vm->natives;
        if (natives) {
            for (int i = 0; i < natives->capacity; i++) {
                SlpValue k = natives->entries[i].key;
                if (!SLP_IS_NULL(k) && SLP_IS_OBJ(k) && SLP_OBJ_TYPE(k) == SLP_OBJ_STRING) {
                    const char *name = SLP_AS_STRING(k)->chars;
                    if (!filter || strstr(name, filter)) {
                        printf("%-20s => [native function]\n", name);
                    }
                }
            }
        }
    }

    // 3. Globals
    if (!type || strcmp(type, "other") == 0) {
        SlpObjHash *globals = vm->globals;
        if (globals) {
            for (int i = 0; i < globals->capacity; i++) {
                SlpValue k = globals->entries[i].key;
                SlpValue v = globals->entries[i].value;
                if (!SLP_IS_NULL(k) && SLP_IS_OBJ(k) && SLP_OBJ_TYPE(k) == SLP_OBJ_STRING) {
                    const char *name = SLP_AS_STRING(k)->chars;
                    if (!filter || strstr(name, filter)) {
                        printf("%-20s => ", name);
                        slp_console_print_value(v);
                        printf("\n");
                    }
                }
            }
        }
    }
}

void slp_console_print_value(SlpValue value) {
    if (SLP_IS_NULL(value)) {
        printf("\x1b[36m$null\x1b[0m");
    } else if (SLP_IS_BOOL(value)) {
        printf("\x1b[36m%s\x1b[0m", SLP_AS_BOOL(value) ? "true" : "false");
    } else if (SLP_IS_NUM(value)) {
        printf("\x1b[33m%g\x1b[0m", SLP_AS_NUM(value));
    } else if (SLP_IS_OBJ(value)) {
        switch (SLP_OBJ_TYPE(value)) {
            case SLP_OBJ_STRING:
                printf("\x1b[32m\"%s\"\x1b[0m", SLP_AS_STRING(value)->chars);
                break;
            case SLP_OBJ_LONG:
                printf("\x1b[33m%" PRId64 "L\x1b[0m", SLP_AS_LONG(value)->value);
                break;
            case SLP_OBJ_FUNCTION:
                printf("\x1b[35m[function]\x1b[0m");
                break;
            case SLP_OBJ_CLOSURE:
                printf("\x1b[35m[closure]\x1b[0m");
                break;
            case SLP_OBJ_NATIVE:
                printf("\x1b[35m[native]\x1b[0m");
                break;
            case SLP_OBJ_ARRAY: {
                SlpObjArray *arr = SLP_AS_ARRAY(value);
                printf("\x1b[1m@(\x1b[0m");
                for (int i = 0; i < arr->count; i++) {
                    slp_console_print_value(arr->elements[i]);
                    if (i < arr->count - 1) printf("\x1b[1m, \x1b[0m");
                }
                printf("\x1b[1m)\x1b[0m");
                break;
            }
            case SLP_OBJ_HASH: {
                SlpObjHash *hash = SLP_AS_HASH(value);
                printf("\x1b[1m%%(\x1b[0m");
                bool first = true;
                for (int i = 0; i < hash->capacity; i++) {
                    if (!SLP_IS_NULL(hash->entries[i].key) && !SLP_IS_TRUE(hash->entries[i].value)) {
                        if (!first) printf("\x1b[1m, \x1b[0m");
                        slp_console_print_value(hash->entries[i].key);
                        printf(" \x1b[1m=>\x1b[0m ");
                        slp_console_print_value(hash->entries[i].value);
                        first = false;
                    }
                }
                printf("\x1b[1m)\x1b[0m");
                break;
            }
            default:
                printf("\x1b[35m[object]\x1b[0m");
                break;
        }
    }
}

static void console_bestline_completion(const char *buf, int pos, bestlineCompletions *lc) {
    if (!g_active_console) return;

    if (g_active_console->completion_handler) {
        g_active_console->completion_handler(g_active_console, buf, pos, lc);
        if (lc->len > 0) return;
    }

    if (!g_active_console->interact_mode) {
        // Command Mode Autocomplete
        const char *commands[] = {"help", "env", "interact", "version", "load", "tree", "x", "?", "exit", "quit", NULL};
        int start = pos;
        while (start > 0 && buf[start - 1] != ' ') {
            start--;
        }
        int word_len = pos - start;
        if (word_len > 0) {
            for (int i = 0; commands[i] != NULL; i++) {
                if (strncmp(buf + start, commands[i], (size_t)word_len) == 0) {
                    size_t new_len = (size_t)start + strlen(commands[i]) + strlen(buf + pos) + 1;
                    char *new_buf = malloc(new_len);
                    if (new_buf) {
                        strncpy(new_buf, buf, (size_t)start);
                        strcpy(new_buf + start, commands[i]);
                        strcpy(new_buf + start + strlen(commands[i]), buf + pos);
                        bestlineAddCompletion(lc, new_buf);
                        free(new_buf);
                    }
                }
            }
        }
    } else {
        // Interactive Mode Autocomplete
        int start = pos;
        while (start > 0 && buf[start - 1] != ' ' && buf[start - 1] != '(' && buf[start - 1] != ')' &&
               buf[start - 1] != '[' && buf[start - 1] != ']' && buf[start - 1] != ',' &&
               buf[start - 1] != '=' && buf[start - 1] != '+' && buf[start - 1] != '-' &&
               buf[start - 1] != '*' && buf[start - 1] != '/' && buf[start - 1] != '\t' &&
               buf[start - 1] != ';' && buf[start - 1] != '{' && buf[start - 1] != '}') {
            start--;
        }
        int word_len = pos - start;

        // Live Dynamic Variable Auto-completion from VM state!
        if (word_len > 0 && g_active_console->vm && g_active_console->vm->globals &&
            (buf[start] == '$' || buf[start] == '@' || buf[start] == '%')) {
            SlpObjHash *globals = g_active_console->vm->globals;
            for (int i = 0; i < globals->capacity; i++) {
                SlpValue k = globals->entries[i].key;
                if (!SLP_IS_NULL(k) && SLP_IS_OBJ(k) && SLP_OBJ_TYPE(k) == SLP_OBJ_STRING) {
                    const char *var_name = SLP_AS_STRING(k)->chars;
                    if (strncmp(buf + start, var_name, (size_t)word_len) == 0) {
                        size_t new_len = (size_t)start + strlen(var_name) + strlen(buf + pos) + 1;
                        char *new_buf = malloc(new_len);
                        if (new_buf) {
                            strncpy(new_buf, buf, (size_t)start);
                            strcpy(new_buf + start, var_name);
                            strcpy(new_buf + start + strlen(var_name), buf + pos);
                            bestlineAddCompletion(lc, new_buf);
                            free(new_buf);
                        }
                    }
                }
            }
            return;
        }

        const char *keywords[] = {
            "if", "else", "while", "for", "foreach", "return", "break", "continue",
            "sub", "alias", "try", "catch", "throw", "true", "false", "null",
            "print", "println", "openf", "connect", "listen", "exec", "readln",
            "readAll", "readc", "readb", "writeb", "closef", "bread", "bwrite",
            "available", "sizeof", "pack", "unpack",
            "left", "right", "charAt", "byteAt", "uc", "lc", "substr", "indexOf",
            "lindexOf", "strlen", "replaceAt", "asc", "chr", "strrep", "replace",
            "sort", "sorta", "sortn", "sortd", "tr",
            "abs", "acos", "asin", "atan", "atan2", "ceil", "floor", "cos", "sin",
            "sqrt", "tan", "radians", "degrees", "exp", "double", "int", "uint",
            "long", "srand", "rand", "round", "sum", "parseNumber", "formatNumber",
            "array", "hash", "ohash", "ohasha", "keys", "size", "push", "pop", "add",
            "flatten", "clear", "splice", "subarray", "sublist", "copy", "putAll",
            "addAll", "removeAll", "retainAll", "pushl", "popl", "search", "reduce",
            "values", "typeOf", "global", "local", "reverse", "removeAt", "shift",
            "cast", "contains",
            "ls", "mkdir", "deleteFile", "rename", "lof", "lastModified", "cwd",
            "chdir", "getFileName", "getFileParent", "getFileProper",
            "matched", "ismatch", "hasmatch",
            "ticks", "tstamp", "sleep", "formatDate", "parseDate",
            NULL
        };

        int start_kw = pos;
        while (start_kw > 0 && ((buf[start_kw - 1] >= 'a' && buf[start_kw - 1] <= 'z') ||
                             (buf[start_kw - 1] >= 'A' && buf[start_kw - 1] <= 'Z') ||
                             buf[start_kw - 1] == '_' ||
                             (buf[start_kw - 1] >= '0' && buf[start_kw - 1] <= '9'))) {
            start_kw--;
        }

        int word_len_kw = pos - start_kw;
        if (word_len_kw == 0) return;

        for (int i = 0; keywords[i] != NULL; i++) {
            if (strncmp(buf + start_kw, keywords[i], (size_t)word_len_kw) == 0) {
                size_t new_len = (size_t)start_kw + strlen(keywords[i]) + strlen(buf + pos) + 1;
                char *new_buf = malloc(new_len);
                if (new_buf) {
                    strncpy(new_buf, buf, (size_t)start_kw);
                    strcpy(new_buf + start_kw, keywords[i]);
                    strcpy(new_buf + start_kw + strlen(keywords[i]), buf + pos);
                    bestlineAddCompletion(lc, new_buf);
                    free(new_buf);
                }
            }
        }
    }
}

static char *console_bestline_hints(const char *buf, const char **color, const char **bold) {
    (void)color;
    (void)bold;
    if (!g_active_console) return NULL;

    if (g_active_console->hints_handler) {
        char *hint = g_active_console->hints_handler(g_active_console, buf);
        if (hint) return hint;
    }

    int len = (int)strlen(buf);
    if (len == 0) return NULL;

    if (!g_active_console->interact_mode) {
        char command[64] = {0};
        int i = 0;
        while (i < len && buf[i] != ' ' && i < 63) {
            command[i] = buf[i];
            i++;
        }
        command[i] = '\0';

        if (strcmp(command, "help") == 0 && len == 4) return strdup(" [command]");
        if (strcmp(command, "env") == 0 && len == 3) return strdup(" [functions|other] [filter]");
        if (strcmp(command, "load") == 0 && len == 4) return strdup(" <file>");
        if (strcmp(command, "tree") == 0 && len == 4) return strdup(" <closure>");
        if (strcmp(command, "x") == 0 && len == 1) return strdup(" <expression>");
        if (strcmp(command, "?") == 0 && len == 1) return strdup(" <predicate>");
        return NULL;
    }

    int start = len;
    while (start > 0 && ((buf[start - 1] >= 'a' && buf[start - 1] <= 'z') ||
                         (buf[start - 1] >= 'A' && buf[start - 1] <= 'Z') ||
                         buf[start - 1] == '_')) {
        start--;
    }
    int word_len = len - start;
    if (word_len == 0) return NULL;

    const char *last_word = buf + start;

    if (strncmp(last_word, "println", (size_t)word_len) == 0 && word_len == 7) return strdup(" (val)");
    if (strncmp(last_word, "print", (size_t)word_len) == 0 && word_len == 5) return strdup(" (val)");
    if (strncmp(last_word, "getFileProper", (size_t)word_len) == 0 && word_len == 13) return strdup(" (parent, child, ...)");
    if (strncmp(last_word, "getFileParent", (size_t)word_len) == 0 && word_len == 13) return strdup(" (file)");
    if (strncmp(last_word, "getFileName", (size_t)word_len) == 0 && word_len == 11) return strdup(" (file)");
    if (strncmp(last_word, "substr", (size_t)word_len) == 0 && word_len == 6) return strdup(" (str, start, [end])");
    if (strncmp(last_word, "indexOf", (size_t)word_len) == 0 && word_len == 7) return strdup(" (str, sub, [start])");
    if (strncmp(last_word, "replaceAt", (size_t)word_len) == 0 && word_len == 9) return strdup(" (str, rep, start, length)");
    if (strncmp(last_word, "strrep", (size_t)word_len) == 0 && word_len == 6) return strdup(" (str, old, new)");
    if (strncmp(last_word, "digest", (size_t)word_len) == 0 && word_len == 6) return strdup(" (str, type)");
    if (strncmp(last_word, "checksum", (size_t)word_len) == 0 && word_len == 8) return strdup(" (str, type)");
    if (strncmp(last_word, "formatDate", (size_t)word_len) == 0 && word_len == 10) return strdup(" (pattern, ticks)");
    if (strncmp(last_word, "parseDate", (size_t)word_len) == 0 && word_len == 9) return strdup(" (pattern, str)");
    if (strncmp(last_word, "pack", (size_t)word_len) == 0 && word_len == 4) return strdup(" (format, args...)");
    if (strncmp(last_word, "unpack", (size_t)word_len) == 0 && word_len == 6) return strdup(" (format, data)");
    if (strncmp(last_word, "openf", (size_t)word_len) == 0 && word_len == 5) return strdup(" (file_or_mode, [file])");
    if (strncmp(last_word, "readb", (size_t)word_len) == 0 && word_len == 5) return strdup(" (handle, bytes)");
    if (strncmp(last_word, "writeb", (size_t)word_len) == 0 && word_len == 6) return strdup(" (handle, data)");
    if (strncmp(last_word, "closef", (size_t)word_len) == 0 && word_len == 6) return strdup(" (handle)");

    return NULL;
}

static void console_free_hints(void *hint) {
    if (hint) free(hint);
}

void slp_console_run(SlpConsole *console, const char *history_file) {
    g_active_console = console;

    bestlineSetCompletionCallback(console_bestline_completion);
    bestlineSetHintsCallback(console_bestline_hints);
    bestlineSetFreeHintsCallback(console_free_hints);
    bestlineHistoryLoad(history_file);

    while (1) {
        const char *prompt = "> ";
        if (console->prompt_handler) {
            prompt = console->prompt_handler(console);
        } else {
            prompt = console->interact_mode ? ">> " : "> ";
        }

        char *line = bestline(prompt);
        if (!line) {
            if (console->buffer) {
                free(console->buffer);
                console->buffer = NULL;
                console->buf_len = 0;
            }
            printf("\n");
            if (console->interact_mode) {
                console->interact_mode = false;
                printf(">> Left interactive mode.\n");
                continue;
            }
            break; // EOF
        }

        if (!console->interact_mode) {
            if (line[0] == '\0') {
                free(line);
                continue;
            }
            bestlineHistoryAdd(line);
            bestlineHistorySave(history_file);

            char *space = strchr(line, ' ');
            char *command = line;
            char *args = NULL;
            if (space) {
                *space = '\0';
                args = space + 1;
                while (*args == ' ' || *args == '\t') {
                    args++;
                }
                if (*args == '\0') args = NULL;
            }

            // Extensible custom command hook
            if (console->command_handler && console->command_handler(console, command, args)) {
                free(line);
                continue;
            }

            if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0 || strcmp(command, "done") == 0) {
                free(line);
                printf("Good bye!\n");
                break;
            }
            else if (strcmp(command, "version") == 0) {
                printf("Sleep v2.1 (SlpVM v0.1)\n");
            }
            else if (strcmp(command, "help") == 0) {
                if (args) {
                    if (strcmp(args, "debug") == 0) {
                        printf("debug [script] <level>\n");
                        printf("   sets the debug level for the specified script\n");
                        printf("   1 - show critical errors\n");
                        printf("   2 - show warnings\n");
                        printf("   4 - strict mode, complain about non-declared variables\n");
                        printf("   8 - trace all function calls\n");
                    } else if (strcmp(args, "env") == 0) {
                        printf("env [functions/other] [regex filter]\n");
                        printf("   dumps the shared environment, filters output with specified substring\n");
                    } else if (strcmp(args, "interact") == 0) {
                        printf("interact\n");
                        printf("   enters the console into interactive mode.\n");
                    } else if (strcmp(args, "list") == 0) {
                        printf("list\n");
                        printf("   lists loaded scripts (interactive REPL session has 1 active VM environment)\n");
                    } else if (strcmp(args, "load") == 0) {
                        printf("load <file>\n");
                        printf("   loads and executes a Sleep script file into the current VM\n");
                    } else if (strcmp(args, "tree") == 0) {
                        printf("tree <closure>\n");
                        printf("   disassembles the bytecode for the specified closure or function\n");
                    } else if (strcmp(args, "quit") == 0 || strcmp(args, "exit") == 0) {
                        printf("quit / exit\n");
                        printf("   stops the console REPL\n");
                    } else if (strcmp(args, "x") == 0) {
                        printf("x <expression>\n");
                        printf("   evaluates a sleep expression and displays the value\n");
                    } else if (strcmp(args, "?") == 0) {
                        printf("? <predicate expression>\n");
                        printf("   evaluates a sleep predicate expression and displays the truth value\n");
                    } else {
                        printf("help [command]\n");
                    }
                } else {
                    printf("debug [script] <level>\n");
                    printf("env [functions/other] [substring filter]\n");
                    printf("help [command]\n");
                    printf("interact\n");
                    printf("list\n");
                    printf("load <file>\n");
                    printf("quit\n");
                    printf("tree [key]\n");
                    printf("version\n");
                    printf("x <expression>\n");
                    printf("? <predicate expression>\n");
                }
            }
            else if (strcmp(command, "interact") == 0) {
                console->interact_mode = true;
                printf(">> Welcome to interactive mode.\n");
                printf("Type your code and then '.' on a line by itself to execute the code.\n");
                printf("Type Ctrl+D or 'done' on a line by itself to leave interactive mode.\n");
            }
            else if (strcmp(command, "list") == 0) {
                printf("<interactive REPL>\n");
            }
            else if (strcmp(command, "load") == 0) {
                if (!args) {
                    printf("Error: Please specify a file to load.\n");
                } else {
                    slp_console_load(console, args);
                }
            }
            else if (strcmp(command, "tree") == 0) {
                slp_console_tree(console, args);
            }
            else if (strcmp(command, "x") == 0) {
                if (!args) {
                    printf("Error: Please specify an expression to evaluate.\n");
                } else {
                    size_t len = strlen(args) + 32;
                    char *source = malloc(len);
                    snprintf(source, len, "return (%s);", args);
                    bool success;
                    SlpValue res = slp_console_eval(console, source, &success);
                    if (success) {
                        printf("=> ");
                        slp_console_print_value(res);
                        printf("\n");
                    }
                    free(source);
                }
            }
            else if (strcmp(command, "?") == 0) {
                if (!args) {
                    printf("Error: Please specify a predicate to evaluate.\n");
                } else {
                    size_t len = strlen(args) + 64;
                    char *source = malloc(len);
                    snprintf(source, len, "return iff(%s, 'true', 'false');", args);
                    bool success;
                    SlpValue res = slp_console_eval(console, source, &success);
                    if (success) {
                        printf("=> ");
                        slp_console_print_value(res);
                        printf("\n");
                    }
                    free(source);
                }
            }
            else if (strcmp(command, "env") == 0) {
                char *type = NULL;
                char *filter = NULL;
                if (args) {
                    char *space2 = strchr(args, ' ');
                    if (space2) {
                        *space2 = '\0';
                        type = args;
                        filter = space2 + 1;
                    } else {
                        type = args;
                    }
                }
                slp_console_env(console, type, filter);
            }
            else {
                printf("Command '%s' not understood. Type 'help' if you need it\n", command);
            }

            free(line);
        } else {
            // Interactive Mode!
            if (strcmp(line, "done") == 0) {
                free(line);
                if (console->buffer) {
                    free(console->buffer);
                    console->buffer = NULL;
                    console->buf_len = 0;
                }
                console->interact_mode = false;
                printf(">> Left interactive mode.\n");
                continue;
            }

            if (strcmp(line, ".") == 0) {
                free(line);
                if (console->buffer) {
                    bestlineHistoryAdd(console->buffer);
                    bestlineHistorySave(history_file);
                    bool success;
                    slp_console_eval(console, console->buffer, &success);
                    free(console->buffer);
                    console->buffer = NULL;
                    console->buf_len = 0;
                } else {
                    printf("No accumulated code to run.\n");
                }
                continue;
            }

            size_t line_len = strlen(line);
            if (line_len > 0) {
                if (!console->buffer) {
                    console->buffer = malloc(line_len + 2);
                    strcpy(console->buffer, line);
                    console->buf_len = line_len;
                } else {
                    console->buffer = realloc(console->buffer, console->buf_len + line_len + 2);
                    console->buffer[console->buf_len] = '\n';
                    strcpy(console->buffer + console->buf_len + 1, line);
                    console->buf_len += line_len + 1;
                }
            }
            free(line);
        }
    }

    g_active_console = NULL;
}
