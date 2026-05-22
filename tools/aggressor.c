/*
 * aggressor.c - Standalone Interactive REPL for Aggressor Script Bundles
 *
 * Loads a .cna bundle or script, registers all mocked Aggressor functions,
 * and starts an interactive shell to execute registered aliases.
 */

#include "sleepy_common.h"
#include "sleepy_core.h"
#include "sleepy_vm.h"
#include "sleepy_compiler.h"
#include "sleepy_disasm.h"
#include "sleepy_parser.h"
#include "aggressor.h"
#include "bestline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

/* -----------------------------------------------------------------------
 * REPL State & Structs
 * ----------------------------------------------------------------------- */

typedef struct {
    char name[128];
    SleepyObjClosure *closure;
} AliasRecord;

#define MAX_ALIASES 512
#define MAX_OPEN_FILES 32

typedef struct {
    char current_alias[128];
    FILE *open_files[MAX_OPEN_FILES];
} REPLState;

static AliasRecord alias_registry[MAX_ALIASES];
static int alias_count = 0;
static REPLState global_repl_state;

/* -----------------------------------------------------------------------
 * VM Allocator & Output Helpers
 * ----------------------------------------------------------------------- */

static void *repl_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}

static SleepyAllocator allocator = {repl_alloc, NULL};

static void repl_error(void *ud, int line, const char *msg) {
    (void)ud;
    if (line > 0)
        fprintf(stderr, "Error line %d: %s\n", line, msg);
    else
        fprintf(stderr, "Error: %s\n", msg);
}

static void repl_write(void *ud, const char *msg) {
    (void)ud;
    printf("%s", msg);
}

static SleepyValue val_println(SleepyVM *vm, SleepyValue *args, int argc) {
    (void)vm;
    if (argc > 0) {
        if (SLEEPY_IS_NUM(args[0]))
            printf("%g\n", SLEEPY_AS_NUM(args[0]));
        else if (SLEEPY_IS_BOOL(args[0]))
            printf("%s\n", SLEEPY_AS_BOOL(args[0]) ? "true" : "false");
        else if (SLEEPY_IS_NULL(args[0]))
            printf("$null\n");
        else if (SLEEPY_IS_OBJ(args[0])) {
            SleepyObj *obj = SLEEPY_AS_OBJ(args[0]);
            if (obj->type == SLEEPY_OBJ_STRING)
                printf("%s\n", ((SleepyObjString*)obj)->chars);
            else
                printf("[object]\n");
        }
    } else {
        printf("\n");
    }
    return SLEEPY_NULL_VAL;
}

/* -----------------------------------------------------------------------
 * Bridge Handler: alias
 * ----------------------------------------------------------------------- */

static void alias_handler(SleepyVM *vm, const char *keyword, const char *name,
                           const char *str, SleepyObjClosure *closure,
                           void *ud) {
    (void)keyword; (void)str; (void)ud;
    if (!name) return;

    if (alias_count < MAX_ALIASES) {
        strncpy(alias_registry[alias_count].name, name,
                sizeof(alias_registry[alias_count].name) - 1);
        alias_registry[alias_count].name[sizeof(alias_registry[alias_count].name) - 1] = '\0';
        alias_registry[alias_count].closure = closure;
        alias_count++;
    }

    /* Store globally as __alias_NAME for interactive execution */
    char global_name[256];
    snprintf(global_name, sizeof(global_name), "__alias_%s", name);
    SleepyObjString *gname = sleepy_vm_copy_string(vm, global_name, strlen(global_name));
    sleepy_obj_hash_set(vm->allocator, vm->globals, SLEEPY_OBJ_VAL(gname), SLEEPY_OBJ_VAL(closure));
}

/* -----------------------------------------------------------------------
 * Mocks & Overrides
 * ----------------------------------------------------------------------- */

static SleepyValue val_barch(SleepyVM *vm, SleepyValue *args, int argc, void *ud) {
    (void)args; (void)argc; (void)ud;
    return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "x64", 3));
}

static SleepyValue val_script_resource(SleepyVM *vm, SleepyValue *args, int argc, void *ud) {
    (void)ud;
    if (argc < 1 || !(SLEEPY_IS_OBJ(args[0]) && SLEEPY_OBJ_TYPE(args[0]) == SLEEPY_OBJ_STRING))
        return SLEEPY_NULL_VAL;
    SleepyObjString *str = SLEEPY_AS_STRING(args[0]);
    return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, str->chars, str->length));
}

static SleepyValue val_bof_pack(SleepyVM *vm, SleepyValue *args, int argc, void *ud) {
    (void)args; (void)argc; (void)ud;
    return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "packed_bof_args", 15));
}

static SleepyValue val_btask(SleepyVM *vm, SleepyValue *args, int argc, void *ud) {
    (void)vm; (void)ud;
    if (argc >= 2 && SLEEPY_IS_OBJ(args[1]) && SLEEPY_OBJ_TYPE(args[1]) == SLEEPY_OBJ_STRING)
        printf("[Task] %s\n", SLEEPY_AS_STRING(args[1])->chars);
    return SLEEPY_NULL_VAL;
}

static SleepyValue val_blog(SleepyVM *vm, SleepyValue *args, int argc, void *ud) {
    (void)vm; (void)ud;
    if (argc >= 2 && SLEEPY_IS_OBJ(args[1]) && SLEEPY_OBJ_TYPE(args[1]) == SLEEPY_OBJ_STRING)
        printf("[Log] %s\n", SLEEPY_AS_STRING(args[1])->chars);
    return SLEEPY_NULL_VAL;
}

static SleepyValue val_berror(SleepyVM *vm, SleepyValue *args, int argc, void *ud) {
    (void)vm; (void)ud;
    if (argc >= 2 && SLEEPY_IS_OBJ(args[1]) && SLEEPY_OBJ_TYPE(args[1]) == SLEEPY_OBJ_STRING)
        printf("\x1b[31m[Error]\x1b[0m %s\n", SLEEPY_AS_STRING(args[1])->chars);
    return SLEEPY_NULL_VAL;
}

static SleepyValue val_beacon_command_register(SleepyVM *vm, SleepyValue *args, int argc, void *ud) {
    (void)vm; (void)ud;
    if (argc >= 1 && SLEEPY_IS_OBJ(args[0]) && SLEEPY_OBJ_TYPE(args[0]) == SLEEPY_OBJ_STRING)
        printf("[+] Registered command: %s\n", SLEEPY_AS_STRING(args[0])->chars);
    return SLEEPY_NULL_VAL;
}

static SleepyValue val_beacon_inline_execute(SleepyVM *vm, SleepyValue *args, int argc, void *ud) {
    (void)vm; (void)ud;
    printf("[+] \x1b[36mbeacon_inline_execute\x1b[0m triggered!\n");
    if (argc >= 2 && SLEEPY_IS_OBJ(args[1]) && SLEEPY_OBJ_TYPE(args[1]) == SLEEPY_OBJ_STRING)
        printf("    -> BOF data length: %d bytes\n", SLEEPY_AS_STRING(args[1])->length);
    if (argc >= 3 && SLEEPY_IS_OBJ(args[2]) && SLEEPY_OBJ_TYPE(args[2]) == SLEEPY_OBJ_STRING)
        printf("    -> Entrypoint: %s\n", SLEEPY_AS_STRING(args[2])->chars);
    if (argc >= 4 && SLEEPY_IS_OBJ(args[3]) && SLEEPY_OBJ_TYPE(args[3]) == SLEEPY_OBJ_STRING)
        printf("    -> Args length: %d bytes\n", SLEEPY_AS_STRING(args[3])->length);
    return SLEEPY_NULL_VAL;
}

static SleepyValue val_beacon_command_detail(SleepyVM *vm, SleepyValue *args, int argc, void *ud) {
    (void)args; (void)argc; (void)ud;
    return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "Mock details", 12));
}

static SleepyValue val_openf(SleepyVM *vm, SleepyValue *args, int argc, void *ud) {
    REPLState *state = (REPLState *)ud;
    (void)vm;
    if (argc < 1 || !(SLEEPY_IS_OBJ(args[0]) && SLEEPY_OBJ_TYPE(args[0]) == SLEEPY_OBJ_STRING))
        return SLEEPY_NUM_VAL(-1);
    const char *path = SLEEPY_AS_STRING(args[0])->chars;

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!state->open_files[i]) {
            FILE *f = fopen(path, "rb");
            if (f) {
                state->open_files[i] = f;
                return SLEEPY_NUM_VAL(i);
            }
            break;
        }
    }
    return SLEEPY_NUM_VAL(-1);
}

static SleepyValue val_readb(SleepyVM *vm, SleepyValue *args, int argc, void *ud) {
    REPLState *state = (REPLState *)ud;
    if (argc < 2 || !SLEEPY_IS_NUM(args[0]) || !SLEEPY_IS_NUM(args[1]))
        return SLEEPY_NULL_VAL;
    int handle = (int)SLEEPY_AS_NUM(args[0]);
    int size = (int)SLEEPY_AS_NUM(args[1]);

    if (handle < 0 || handle >= MAX_OPEN_FILES || !state->open_files[handle])
        return SLEEPY_NULL_VAL;

    FILE *f = state->open_files[handle];
    if (size == -1) {
        long current = ftell(f);
        fseek(f, 0, SEEK_END);
        long end = ftell(f);
        fseek(f, current, SEEK_SET);
        size = (int)(end - current);
    }

    if (size <= 0) return SLEEPY_NULL_VAL;

    char *buf = malloc(size);
    size_t read_bytes = fread(buf, 1, size, f);
    SleepyObjString *str = sleepy_vm_copy_string(vm, buf, (uint32_t)read_bytes);
    free(buf);
    return SLEEPY_OBJ_VAL(str);
}

static SleepyValue val_closef(SleepyVM *vm, SleepyValue *args, int argc, void *ud) {
    REPLState *state = (REPLState *)ud;
    (void)vm;
    if (argc < 1 || !SLEEPY_IS_NUM(args[0])) return SLEEPY_NULL_VAL;
    int handle = (int)SLEEPY_AS_NUM(args[0]);
    if (handle >= 0 && handle < MAX_OPEN_FILES && state->open_files[handle]) {
        fclose(state->open_files[handle]);
        state->open_files[handle] = NULL;
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue repl_fallback(SleepyVM *vm, const char *func_name,
                                 SleepyValue *args, int argc,
                                 void *user_data) {
    (void)vm; (void)args; (void)argc; (void)user_data;
    (void)func_name;
    return SLEEPY_NULL_VAL;
}

/* -----------------------------------------------------------------------
 * Autocomplete
 * ----------------------------------------------------------------------- */

static void completion(const char *buf, int pos, bestlineCompletions *lc) {
    int start = pos;
    while (start > 0 && buf[start - 1] != ' ') start--;
    int word_len = pos - start;
    if (word_len == 0) return;

    for (int i = 0; i < alias_count; i++) {
        if (strncmp(buf + start, alias_registry[i].name, word_len) == 0) {
            size_t new_len = start + strlen(alias_registry[i].name) + strlen(buf + pos) + 1;
            char *new_buf = malloc(new_len);
            if (new_buf) {
                strncpy(new_buf, buf, start);
                strcpy(new_buf + start, alias_registry[i].name);
                strcpy(new_buf + start + strlen(alias_registry[i].name), buf + pos);
                bestlineAddCompletion(lc, new_buf);
                free(new_buf);
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * Main Entry Point
 * ----------------------------------------------------------------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <bundle.cna>\n", argv[0]);
        return 0;
    }

    const char *script_file = argv[1];

    memset(&global_repl_state, 0, sizeof(global_repl_state));

    /* Create VM */
    SleepyVM *vm = sleepy_vm_new(&allocator);
    sleepy_vm_set_error_fn(vm, repl_error, NULL);
    sleepy_vm_set_write_fn(vm, repl_write, NULL);

    /* Initialize libaggressor with our fallback and state */
    AggressorConfig cfg = {
        .fallback  = repl_fallback,
        .user_data = &global_repl_state,
    };
    AggressorState *ag_state = aggressor_init(vm, &cfg);

    /* Register/Override standard functions */
    sleepy_vm_register_native(vm, "println", val_println);

    aggressor_set(ag_state, "barch",                    val_barch);
    aggressor_set(ag_state, "script_resource",          val_script_resource);
    aggressor_set(ag_state, "bof_pack",                 val_bof_pack);
    aggressor_set(ag_state, "btask",                    val_btask);
    aggressor_set(ag_state, "blog",                     val_blog);
    aggressor_set(ag_state, "berror",                   val_berror);
    aggressor_set(ag_state, "beacon_command_register",  val_beacon_command_register);
    aggressor_set(ag_state, "beacon_inline_execute",    val_beacon_inline_execute);
    aggressor_set(ag_state, "beacon_command_detail",    val_beacon_command_detail);
    aggressor_set(ag_state, "openf",                    val_openf);
    aggressor_set(ag_state, "readb",                    val_readb);
    aggressor_set(ag_state, "closef",                   val_closef);

    /* Register bridge types */
    AggressorBridgeOps bridge_ops = { .alias_handler = alias_handler };
    aggressor_set_bridge_ops(vm, &bridge_ops);

    /* Load the script */
    printf("[*] Loading script: %s...\n", script_file);
    FILE *f = fopen(script_file, "rb");
    if (!f) {
        fprintf(stderr, "Could not open file: %s\n", script_file);
        free(ag_state);
        sleepy_vm_free(vm);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *source = (char *)malloc(fsize + 1);
    size_t read_cnt = fread(source, 1, fsize, f);
    source[read_cnt] = '\0';
    fclose(f);

    SleepyResult r = sleepy_vm_interpret(vm, source);
    free(source);
    if (r != SLEEPY_OK) {
        fprintf(stderr, "[!] Failed evaluating script: %s\n", script_file);
        free(ag_state);
        sleepy_vm_free(vm);
        return 1;
    }

    printf("[+] Script loaded successfully.\n");
    printf("\n\x1b[32m=== Aggressor REPL ===\x1b[0m\n");
    printf("Type an alias to execute (e.g. 'sc_config arg1 arg2'). Type 'exit' to quit.\n");
    printf("Registered aliases: %d\n\n", alias_count);

    bestlineSetCompletionCallback(completion);

    /* Note: History loading/saving is explicitly disabled */

    while (1) {
        char *line = bestline("aggressor> ");
        if (!line) break;

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            free(line);
            break;
        }

        if (line[0] != '\0') {
            char *cmd = strdup(line);
            char *args_str = "";
            char *space = strchr(cmd, ' ');
            if (space) {
                *space = '\0';
                args_str = space + 1;
            }

            int found = 0;
            for (int i = 0; i < alias_count; i++) {
                if (strcmp(alias_registry[i].name, cmd) == 0) {
                    found = 1;
                    char eval_buf[4096];
                    snprintf(eval_buf, sizeof(eval_buf), "__alias_%s('1', '%s');", cmd, args_str);
                    vm->repl_mode = true;
                    sleepy_vm_interpret(vm, eval_buf);
                    vm->repl_mode = false;
                    break;
                }
            }
            if (!found) printf("[-] Alias '%s' not found.\n", cmd);
            free(cmd);
        }
        free(line);
    }

    free(ag_state);
    sleepy_vm_free(vm);
    return 0;
}
