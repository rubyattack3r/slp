/*
 * aggressor.c - Standalone Interactive REPL for Aggressor Script Bundles
 *
 * Loads a .cna bundle or script, registers all mocked Aggressor functions,
 * and starts an interactive shell to execute registered aliases.
 */

#include "slp_common.h"
#include "slp_core.h"
#include "slp_vm.h"
#include "slp_compiler.h"
#include "slp_disasm.h"
#include "slp_parser.h"
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
    SlpObjClosure *closure;
} AliasRecord;

#define MAX_ALIASES 512
#define MAX_OPEN_FILES 32

typedef struct {
    char current_alias[128];
    FILE *open_files[MAX_OPEN_FILES];
    char script_dir[512];
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

static SlpAllocator allocator = {repl_alloc, NULL};

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

static SlpValue val_println(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc > 0) {
        if (SLP_IS_NUM(args[0]))
            printf("%g\n", SLP_AS_NUM(args[0]));
        else if (SLP_IS_BOOL(args[0]))
            printf("%s\n", SLP_AS_BOOL(args[0]) ? "true" : "false");
        else if (SLP_IS_NULL(args[0]))
            printf("$null\n");
        else if (SLP_IS_OBJ(args[0])) {
            SlpObj *obj = SLP_AS_OBJ(args[0]);
            if (obj->type == SLP_OBJ_STRING)
                printf("%s\n", ((SlpObjString*)obj)->chars);
            else
                printf("[object]\n");
        }
    } else {
        printf("\n");
    }
    return SLP_NULL_VAL;
}

/* -----------------------------------------------------------------------
 * Bridge Handler: alias
 * ----------------------------------------------------------------------- */

static void alias_handler(SlpVM *vm, const char *keyword, const char *name,
                           const char *str, SlpObjClosure *closure,
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
    SlpObjString *gname = slp_vm_copy_string(vm, global_name, strlen(global_name));
    slp_obj_hash_set(vm->allocator, vm->globals, SLP_OBJ_VAL(gname), SLP_OBJ_VAL(closure));
}

/* -----------------------------------------------------------------------
 * Mocks & Overrides
 * ----------------------------------------------------------------------- */

static SlpValue val_beacon_info(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    (void)args; (void)ud;
    if (argc >= 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING) {
        const char *key = SLP_AS_STRING(args[1])->chars;
        if (strcmp(key, "barch") == 0) {
            return SLP_OBJ_VAL(slp_vm_copy_string(vm, "x64", 3));
        } else if (strcmp(key, "isadmin") == 0) {
            return SLP_NUM_VAL(1);
        }
    }
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, "mock_beacon_info_val", 20));
}

static SlpValue val_deleteFile(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    (void)vm; (void)args; (void)argc; (void)ud;
    return SLP_BOOL_VAL(true);
}

static SlpValue val_readAll(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    (void)args; (void)argc; (void)ud;
    SlpObjArray *arr = slp_obj_array_new(vm->allocator);
    slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(slp_vm_copy_string(vm, "line1", 5)));
    slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(slp_vm_copy_string(vm, "line2", 5)));
    return SLP_OBJ_VAL(arr);
}

static SlpValue val_barch(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    (void)args; (void)argc; (void)ud;
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, "x64", 3));
}

static SlpValue val_script_resource(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    REPLState *state = (REPLState *)ud;
    if (argc < 1 || !(SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING))
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    char resolved[1024];
    snprintf(resolved, sizeof(resolved), "%s%s", state->script_dir, str->chars);
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, resolved, strlen(resolved)));
}

static SlpValue val_bof_pack(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    (void)args; (void)argc; (void)ud;
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, "packed_bof_args", 15));
}

static SlpValue val_btask(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    (void)vm; (void)ud;
    if (argc >= 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING)
        printf("[Task] %s\n", SLP_AS_STRING(args[1])->chars);
    return SLP_NULL_VAL;
}

static SlpValue val_blog(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    (void)vm; (void)ud;
    if (argc >= 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING)
        printf("[Log] %s\n", SLP_AS_STRING(args[1])->chars);
    return SLP_NULL_VAL;
}

static SlpValue val_berror(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    (void)vm; (void)ud;
    if (argc >= 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING)
        printf("\x1b[31m[Error]\x1b[0m %s\n", SLP_AS_STRING(args[1])->chars);
    return SLP_NULL_VAL;
}

static SlpValue val_beacon_command_register(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    (void)ud;
    if (argc >= 1 && SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
        const char *name = SLP_AS_STRING(args[0])->chars;
        printf("[+] Registered command: %s\n", name);

        int64_t addr = slp_vm_ffi_get_long(vm, 255);
        AggressorState *state = (AggressorState *)(uintptr_t)addr;
        if (state) {
            const char *description = (argc >= 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING) ? SLP_AS_STRING(args[1])->chars : "";
            const char *help = (argc >= 3 && SLP_IS_OBJ(args[2]) && SLP_OBJ_TYPE(args[2]) == SLP_OBJ_STRING) ? SLP_AS_STRING(args[2])->chars : "";
            aggressor_register_command(state, name, description, help);
        }
    }
    return SLP_NULL_VAL;
}

static SlpValue val_beacon_inline_execute(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    (void)vm; (void)ud;
    printf("[+] \x1b[36mbeacon_inline_execute\x1b[0m triggered!\n");
    if (argc >= 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING)
        printf("    -> BOF data length: %d bytes\n", SLP_AS_STRING(args[1])->length);
    if (argc >= 3 && SLP_IS_OBJ(args[2]) && SLP_OBJ_TYPE(args[2]) == SLP_OBJ_STRING)
        printf("    -> Entrypoint: %s\n", SLP_AS_STRING(args[2])->chars);
    if (argc >= 4 && SLP_IS_OBJ(args[3]) && SLP_OBJ_TYPE(args[3]) == SLP_OBJ_STRING)
        printf("    -> Args length: %d bytes\n", SLP_AS_STRING(args[3])->length);
    return SLP_NULL_VAL;
}

static SlpValue val_beacon_command_detail(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    (void)ud;
    if (argc >= 1 && SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
        const char *name = SLP_AS_STRING(args[0])->chars;
        int64_t addr = slp_vm_ffi_get_long(vm, 255);
        AggressorState *state = (AggressorState *)(uintptr_t)addr;
        if (state) {
            const char *help = aggressor_get_command_help(state, name);
            if (help) {
                return SLP_OBJ_VAL(slp_vm_copy_string(vm, help, strlen(help)));
            }
        }
    }
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, "Mock details", 12));
}

static SlpValue val_openf(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    REPLState *state = (REPLState *)ud;
    (void)vm;
    if (argc < 1 || !(SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING)) {
        printf("[Debug] val_openf: Invalid arguments\n");
        return SLP_NUM_VAL(-1);
    }
    const char *path = SLP_AS_STRING(args[0])->chars;
    printf("[Debug] val_openf: Attempting to open path: '%s'\n", path);

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!state->open_files[i]) {
            FILE *f = fopen(path, "rb");
            if (f) {
                printf("[Debug] val_openf: Successfully opened '%s' as handle %d\n", path, i);
                state->open_files[i] = f;
                return SLP_NUM_VAL(i);
            } else {
                perror("[Debug] val_openf fopen failed");
            }
            break;
        }
    }
    return SLP_NUM_VAL(-1);
}

static SlpValue val_readb(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    REPLState *state = (REPLState *)ud;
    if (argc < 2 || !SLP_IS_NUM(args[0]) || !SLP_IS_NUM(args[1]))
        return SLP_NULL_VAL;
    int handle = (int)SLP_AS_NUM(args[0]);
    int size = (int)SLP_AS_NUM(args[1]);

    if (handle < 0 || handle >= MAX_OPEN_FILES || !state->open_files[handle])
        return SLP_NULL_VAL;

    FILE *f = state->open_files[handle];
    if (size == -1) {
        long current = ftell(f);
        fseek(f, 0, SEEK_END);
        long end = ftell(f);
        fseek(f, current, SEEK_SET);
        size = (int)(end - current);
    }

    if (size <= 0) return SLP_NULL_VAL;

    char *buf = malloc(size);
    size_t read_bytes = fread(buf, 1, size, f);
    SlpObjString *str = slp_vm_copy_string(vm, buf, (uint32_t)read_bytes);
    free(buf);
    return SLP_OBJ_VAL(str);
}

static SlpValue val_closef(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    REPLState *state = (REPLState *)ud;
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NULL_VAL;
    int handle = (int)SLP_AS_NUM(args[0]);
    if (handle >= 0 && handle < MAX_OPEN_FILES && state->open_files[handle]) {
        fclose(state->open_files[handle]);
        state->open_files[handle] = NULL;
    }
    return SLP_NULL_VAL;
}

static SlpValue repl_fallback(SlpVM *vm, const char *func_name,
                                 SlpValue *args, int argc,
                                 void *user_data) {
    (void)vm; (void)args; (void)argc; (void)user_data;
    (void)func_name;
    return SLP_NULL_VAL;
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

    memset(&global_repl_state, 0, sizeof(global_repl_state));

    const char *script_file = argv[1];
    const char *last_slash = strrchr(script_file, '/');
    if (last_slash) {
        size_t dir_len = last_slash - script_file + 1;
        if (dir_len < sizeof(global_repl_state.script_dir)) {
            memcpy(global_repl_state.script_dir, script_file, dir_len);
            global_repl_state.script_dir[dir_len] = '\0';
        }
    } else {
        global_repl_state.script_dir[0] = '\0';
    }

    /* Create VM */
    SlpVM *vm = slp_vm_new(&allocator);
    slp_vm_set_error_fn(vm, repl_error, NULL);
    slp_vm_set_write_fn(vm, repl_write, NULL);

    /* Initialize libaggressor with our fallback and state */
    AggressorConfig cfg = {
        .fallback  = repl_fallback,
        .user_data = &global_repl_state,
    };
    AggressorState *ag_state = aggressor_init(vm, &cfg);

    /* Register/Override standard functions */
    slp_vm_register_native(vm, "println", val_println);

    aggressor_set(ag_state, "barch",                    val_barch);
    aggressor_set(ag_state, "beacon_info",              val_beacon_info);
    aggressor_set(ag_state, "deleteFile",               val_deleteFile);
    aggressor_set(ag_state, "readAll",                  val_readAll);
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
        aggressor_deinit(ag_state);
        slp_vm_free(vm);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *source = (char *)malloc(fsize + 1);
    size_t read_cnt = fread(source, 1, fsize, f);
    source[read_cnt] = '\0';
    fclose(f);

    SlpResult r = slp_vm_interpret(vm, source);
    free(source);
    if (r != SLP_OK) {
        fprintf(stderr, "[!] Failed evaluating script: %s\n", script_file);
        aggressor_deinit(ag_state);
        slp_vm_free(vm);
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
                    int len = snprintf(eval_buf, sizeof(eval_buf), "__alias_%s('1'", cmd);
                    char *args_copy = strdup(args_str);
                    char *token = strtok(args_copy, " \t\r\n");
                    while (token) {
                        int written = snprintf(eval_buf + len, sizeof(eval_buf) - len, ", '%s'", token);
                        if (written > 0) len += written;
                        token = strtok(NULL, " \t\r\n");
                    }
                    free(args_copy);
                    snprintf(eval_buf + len, sizeof(eval_buf) - len, ");");
                    
                    vm->repl_mode = true;
                    slp_vm_interpret(vm, eval_buf);
                    vm->repl_mode = false;
                    break;
                }
            }
            if (!found) printf("[-] Alias '%s' not found.\n", cmd);
            free(cmd);
        }
        free(line);
    }

    aggressor_deinit(ag_state);
    slp_vm_free(vm);
    return 0;
}
