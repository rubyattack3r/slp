/*
 * cna_validator.c - CI/CD Validator for Aggressor Script Bundles
 *
 * Uses libaggressor to register all native functions, then overrides
 * specific ones with validation-aware implementations (access() checks,
 * resource verification, etc.).
 */

#include "slp_common.h"
#include "slp_core.h"
#include "slp_vm.h"
#include "slp_compiler.h"
#include "slp_disasm.h"
#include "slp_parser.h"
#include "aggressor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#ifdef _WIN32
#include <io.h>
#define access _access
#define F_OK 0
#else
#include <unistd.h>
#endif

/* -----------------------------------------------------------------------
 * Validator State & Structs
 * ----------------------------------------------------------------------- */

typedef enum {
    FORMAT_TEXT,
    FORMAT_JSON,
    FORMAT_JUNIT
} OutputFormat;

typedef struct {
    char type[64];
    char path[256];
    char alias[128];
    char message[512];
} ValidationError;

#define MAX_VALIDATION_ERRORS 256

/* Global registry for aliases */
typedef struct {
    char name[128];
    SlpObjClosure *closure;
} AliasRecord;

#define MAX_ALIASES 512

typedef struct {
    bool validation_failed;
    OutputFormat format;
    ValidationError errors[MAX_VALIDATION_ERRORS];
    int error_count;
    char current_alias[128];

    /* Open file handle tracking */
    #define MAX_OPEN_FILES 32
    FILE *open_files[MAX_OPEN_FILES];
} ValidatorState;

static void add_validation_error(ValidatorState *state, const char *type, const char *path, const char *msg) {
    state->validation_failed = true;
    if (state->error_count < MAX_VALIDATION_ERRORS) {
        ValidationError *err = &state->errors[state->error_count++];
        strncpy(err->type, type, sizeof(err->type) - 1);
        err->type[sizeof(err->type) - 1] = '\0';
        strncpy(err->path, path ? path : "", sizeof(err->path) - 1);
        err->path[sizeof(err->path) - 1] = '\0';
        strncpy(err->alias, state->current_alias, sizeof(err->alias) - 1);
        err->alias[sizeof(err->alias) - 1] = '\0';
        strncpy(err->message, msg, sizeof(err->message) - 1);
        err->message[sizeof(err->message) - 1] = '\0';
    }
}

/* -----------------------------------------------------------------------
 * VM Setup
 * ----------------------------------------------------------------------- */

static void *repl_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}

static SlpAllocator allocator = {repl_alloc, NULL};

static void repl_error(void *ud, int line, const char *msg) {
    ValidatorState *state = (ValidatorState *)ud;
    if (state && state->format != FORMAT_TEXT) {
        char err_msg[512];
        if (line > 0) {
            snprintf(err_msg, sizeof(err_msg), "Line %d: %s", line, msg);
        } else {
            snprintf(err_msg, sizeof(err_msg), "%s", msg);
        }
        add_validation_error(state, "runtime_error", NULL, err_msg);
        return;
    }
    if (line > 0)
        fprintf(stderr, "Error line %d: %s\n", line, msg);
    else
        fprintf(stderr, "Error: %s\n", msg);
}

static void repl_write(void *ud, const char *msg) {
    ValidatorState *state = (ValidatorState *)ud;
    if (state && state->format != FORMAT_TEXT) {
        return;
    }
    printf("%s", msg);
}

static ValidatorState *global_validator_state = NULL;

static SlpValue val_println(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (global_validator_state && global_validator_state->format != FORMAT_TEXT) {
        return SLP_NULL_VAL;
    }
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

static void print_escaped_json(const char *str) {
    if (!str) return;
    while (*str) {
        if (*str == '\\') {
            printf("\\\\");
        } else if (*str == '"') {
            printf("\\\"");
        } else if (*str == '\n') {
            printf("\\n");
        } else if (*str == '\r') {
            printf("\\r");
        } else if (*str == '\t') {
            printf("\\t");
        } else {
            putchar(*str);
        }
        str++;
    }
}

static void print_json_report(ValidatorState *state, int aliases_registered) {
    printf("{\n");
    printf("  \"status\": \"%s\",\n", state->validation_failed ? "FAILED" : "SUCCESS");
    printf("  \"aliases_registered\": %d,\n", aliases_registered);
    printf("  \"aliases_tested\": %d,\n", aliases_registered);
    printf("  \"errors\": [\n");
    for (int i = 0; i < state->error_count; i++) {
        ValidationError *err = &state->errors[i];
        printf("    {\n");
        printf("      \"type\": \""); print_escaped_json(err->type); printf("\",\n");
        printf("      \"path\": \""); print_escaped_json(err->path); printf("\",\n");
        printf("      \"alias\": \""); print_escaped_json(err->alias); printf("\",\n");
        printf("      \"message\": \""); print_escaped_json(err->message); printf("\"\n");
        printf("    }%s\n", (i == state->error_count - 1) ? "" : ",");
    }
    printf("  ]\n");
    printf("}\n");
}

static void print_escaped_xml(const char *str) {
    if (!str) return;
    while (*str) {
        if (*str == '&') {
            printf("&amp;");
        } else if (*str == '<') {
            printf("&lt;");
        } else if (*str == '>') {
            printf("&gt;");
        } else if (*str == '"') {
            printf("&quot;");
        } else if (*str == '\'') {
            printf("&apos;");
        } else {
            putchar(*str);
        }
        str++;
    }
}

static void print_junit_report(ValidatorState *state, int aliases_registered, int alias_count, AliasRecord *alias_registry) {
    int total_failures = state->error_count;

    printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    printf("<testsuites>\n");
    printf("  <testsuite name=\"cna_validator\" tests=\"%d\" failures=\"%d\" errors=\"0\">\n",
           aliases_registered + (state->error_count > 0 && alias_count == 0 ? 1 : 0), total_failures);

    for (int i = 0; i < state->error_count; i++) {
        if (strcmp(state->errors[i].alias, "") == 0) {
            printf("    <testcase name=\"global_load\" classname=\"cna_validator\">\n");
            printf("      <failure message=\"");
            print_escaped_xml(state->errors[i].message);
            printf("\" type=\"");
            print_escaped_xml(state->errors[i].type);
            printf("\">\n");
            print_escaped_xml(state->errors[i].message);
            printf("\n      </failure>\n");
            printf("    </testcase>\n");
        }
    }

    for (int i = 0; i < alias_count; i++) {
        const char *alias_name = alias_registry[i].name;
        int alias_error_count = 0;
        for (int j = 0; j < state->error_count; j++) {
            if (strcmp(state->errors[j].alias, alias_name) == 0) {
                alias_error_count++;
            }
        }

        if (alias_error_count > 0) {
            printf("    <testcase name=\"");
            print_escaped_xml(alias_name);
            printf("\" classname=\"cna_validator.alias\">\n");
            for (int j = 0; j < state->error_count; j++) {
                if (strcmp(state->errors[j].alias, alias_name) == 0) {
                    printf("      <failure message=\"");
                    print_escaped_xml(state->errors[j].message);
                    printf("\" type=\"");
                    print_escaped_xml(state->errors[j].type);
                    printf("\">\n");
                    print_escaped_xml(state->errors[j].message);
                    printf("\n      </failure>\n");
                }
            }
            printf("    </testcase>\n");
        } else {
            printf("    <testcase name=\"");
            print_escaped_xml(alias_name);
            printf("\" classname=\"cna_validator.alias\"/>\n");
        }
    }

    printf("  </testsuite>\n");
    printf("</testsuites>\n");
}

static AliasRecord alias_registry[MAX_ALIASES];
static int alias_count = 0;

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

    /* Store globally as __alias_NAME for REPL/dry-run invocation */
    char global_name[256];
    snprintf(global_name, sizeof(global_name), "__alias_%s", name);
    SlpObjString *gname = slp_vm_copy_string(vm, global_name, strlen(global_name));
    slp_obj_hash_set(vm->allocator, vm->globals, SLP_OBJ_VAL(gname), SLP_OBJ_VAL(closure));
}

/* -----------------------------------------------------------------------
 * Validator-Specific Overrides
 *
 * These are AggressorNativeFn (with user_data), not SlpNativeFn.
 * ----------------------------------------------------------------------- */

static SlpValue val_barch(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    (void)args; (void)argc; (void)ud;
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, "x64", 3));
}

static SlpValue val_script_resource(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    ValidatorState *state = (ValidatorState *)ud;
    if (argc < 1 || !(SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING))
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    if (strchr(str->chars, '$') == NULL) {
        if (access(str->chars, F_OK) != 0) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Resource not found: %s", str->chars);
            add_validation_error(state, "resource_missing", str->chars, msg);
            if (state->format == FORMAT_TEXT) {
                fprintf(stderr, "\x1b[31m[!] Validation Error: Resource not found: %s\x1b[0m\n", str->chars);
            }
        }
    }
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, str->chars, str->length));
}

static SlpValue val_bof_pack(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    (void)args; (void)argc; (void)ud;
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, "packed_bof_args", 15));
}

static SlpValue val_btask(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    ValidatorState *state = (ValidatorState *)ud;
    (void)vm;
    if (state->format == FORMAT_TEXT && argc >= 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING)
        printf("[Task] %s\n", SLP_AS_STRING(args[1])->chars);
    return SLP_NULL_VAL;
}

static SlpValue val_blog(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    ValidatorState *state = (ValidatorState *)ud;
    (void)vm;
    if (state->format == FORMAT_TEXT && argc >= 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING)
        printf("[Log] %s\n", SLP_AS_STRING(args[1])->chars);
    return SLP_NULL_VAL;
}

static SlpValue val_berror(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    ValidatorState *state = (ValidatorState *)ud;
    (void)vm;
    if (state->format == FORMAT_TEXT && argc >= 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING)
        printf("\x1b[31m[Error]\x1b[0m %s\n", SLP_AS_STRING(args[1])->chars);
    return SLP_NULL_VAL;
}

static SlpValue val_beacon_command_register(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    ValidatorState *state = (ValidatorState *)ud;
    (void)vm;
    if (state->format == FORMAT_TEXT && argc >= 1 && SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING)
        printf("[+] Registered command: %s\n", SLP_AS_STRING(args[0])->chars);
    return SLP_NULL_VAL;
}

static SlpValue val_beacon_inline_execute(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    ValidatorState *state = (ValidatorState *)ud;
    (void)vm;
    if (state->format == FORMAT_TEXT) {
        printf("[+] \x1b[36mbeacon_inline_execute\x1b[0m triggered!\n");
        if (argc >= 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING)
            printf("    -> BOF data length: %d bytes\n", SLP_AS_STRING(args[1])->length);
        if (argc >= 3 && SLP_IS_OBJ(args[2]) && SLP_OBJ_TYPE(args[2]) == SLP_OBJ_STRING)
            printf("    -> Entrypoint: %s\n", SLP_AS_STRING(args[2])->chars);
        if (argc >= 4 && SLP_IS_OBJ(args[3]) && SLP_OBJ_TYPE(args[3]) == SLP_OBJ_STRING)
            printf("    -> Args length: %d bytes\n", SLP_AS_STRING(args[3])->length);
    }
    return SLP_NULL_VAL;
}

static SlpValue val_beacon_command_detail(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    (void)args; (void)argc; (void)ud;
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, "Mock details", 12));
}

static SlpValue val_openf(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    ValidatorState *state = (ValidatorState *)ud;
    (void)vm;
    if (argc < 1 || !(SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING))
        return SLP_NUM_VAL(-1);
    const char *path = SLP_AS_STRING(args[0])->chars;

    if (strchr(path, '$') != NULL) {
        return SLP_NUM_VAL(-1);
    }

    if (access(path, F_OK) != 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "openf failed, file not found: %s", path);
        add_validation_error(state, "file_missing", path, msg);
        if (state->format == FORMAT_TEXT) {
            fprintf(stderr, "\x1b[31m[!] Validation Error: openf failed, file not found: %s\x1b[0m\n", path);
        }
        return SLP_NUM_VAL(-1);
    }

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!state->open_files[i]) {
            FILE *f = fopen(path, "rb");
            if (f) {
                state->open_files[i] = f;
                return SLP_NUM_VAL(i);
            }
            break;
        }
    }
    return SLP_NUM_VAL(-1);
}

static SlpValue val_readb(SlpVM *vm, SlpValue *args, int argc, void *ud) {
    ValidatorState *state = (ValidatorState *)ud;
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
    ValidatorState *state = (ValidatorState *)ud;
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NULL_VAL;
    int handle = (int)SLP_AS_NUM(args[0]);
    if (handle >= 0 && handle < MAX_OPEN_FILES && state->open_files[handle]) {
        fclose(state->open_files[handle]);
        state->open_files[handle] = NULL;
    }
    return SLP_NULL_VAL;
}

/* -----------------------------------------------------------------------
 * Fallback: catch-all for any unregistered function
 * ----------------------------------------------------------------------- */

static SlpValue validator_fallback(SlpVM *vm, const char *func_name,
                                       SlpValue *args, int argc,
                                       void *user_data) {
    (void)vm; (void)args; (void)argc; (void)user_data;
    /* Silent stub — return NULL for any unknown function */
    return SLP_NULL_VAL;
}



/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(int argc, char **argv) {
    const char *script_file = NULL;
    OutputFormat fmt = FORMAT_TEXT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "json") == 0) {
                fmt = FORMAT_JSON;
            } else if (strcmp(argv[i], "junit") == 0) {
                fmt = FORMAT_JUNIT;
            } else {
                fmt = FORMAT_TEXT;
            }
        } else if (strncmp(argv[i], "--format=", 9) == 0) {
            const char *val = argv[i] + 9;
            if (strcmp(val, "json") == 0) {
                fmt = FORMAT_JSON;
            } else if (strcmp(val, "junit") == 0) {
                fmt = FORMAT_JUNIT;
            } else {
                fmt = FORMAT_TEXT;
            }
        } else if (script_file == NULL) {
            script_file = argv[i];
        }
    }

    if (!script_file) {
        printf("Usage: %s <bundle.cna> [--format <text|json|junit>]\n", argv[0]);
        return 0;
    }

    /* Initialize validator state */
    ValidatorState state;
    memset(&state, 0, sizeof(state));
    state.format = fmt;

    global_validator_state = &state;

    /* Create VM */
    SlpVM *vm = slp_vm_new(&allocator);
    slp_vm_set_error_fn(vm, repl_error, &state);
    slp_vm_set_write_fn(vm, repl_write, &state);

    /* Initialize libaggressor with our fallback and state */
    AggressorConfig cfg = {
        .fallback  = validator_fallback,
        .user_data = &state,
    };
    AggressorState *ag_state = aggressor_init(vm, &cfg);

    /* Override println so it isn't reset by aggressor_init */
    slp_vm_register_native(vm, "println", val_println);

    /* Override functions with validation-aware implementations */
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

    /* Load and validate the script */
    if (state.format == FORMAT_TEXT) {
        printf("[*] Loading and validating %s...\n", script_file);
    }
    FILE *f = fopen(script_file, "rb");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Could not open file: %s", script_file);
        add_validation_error(&state, "file_open_failed", script_file, msg);
        if (state.format == FORMAT_TEXT) {
            fprintf(stderr, "Could not open file: %s\n", script_file);
        }
        if (state.format == FORMAT_JSON) {
            print_json_report(&state, 0);
        } else if (state.format == FORMAT_JUNIT) {
            print_junit_report(&state, 0, 0, NULL);
        }
        free(ag_state);
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
        add_validation_error(&state, "global_load_failed", NULL, "Failed evaluating the script during initial load.");
        if (state.format == FORMAT_TEXT) {
            fprintf(stderr, "[!] Validation failed: error evaluating the script.\n");
        }
        if (state.format == FORMAT_JSON) {
            print_json_report(&state, 0);
        } else if (state.format == FORMAT_JUNIT) {
            print_junit_report(&state, 0, 0, NULL);
        }
        free(ag_state);
        slp_vm_free(vm);
        return 1;
    }

    if (state.format == FORMAT_TEXT) {
        printf("[+] Global load successful.\n");
        printf("[+] Executing dry-runs on %d aliases...\n", alias_count);
    }

    for (int i = 0; i < alias_count; i++) {
        strncpy(state.current_alias, alias_registry[i].name, sizeof(state.current_alias) - 1);
        state.current_alias[sizeof(state.current_alias) - 1] = '\0';
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "__alias_%s('1', 'dummy')", alias_registry[i].name);
        SlpResult ar = slp_vm_interpret(vm, cmd);
        if (ar != SLP_OK) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Execution crashed in alias '%s'", alias_registry[i].name);
            add_validation_error(&state, "execution_crash", NULL, msg);
            if (state.format == FORMAT_TEXT) {
                fprintf(stderr, "\x1b[31m[!] Validation Error: Execution crashed in alias '%s'\x1b[0m\n",
                        alias_registry[i].name);
            }
        }
    }
    /* Clear current alias context */
    strcpy(state.current_alias, "");

    /* Report final status */
    if (state.format == FORMAT_TEXT) {
        if (state.validation_failed) {
            fprintf(stderr, "\x1b[31m[!] Validation FAILED.\x1b[0m\n");
        } else {
            printf("[+] Validation SUCCESSFUL.\n");
        }
    } else if (state.format == FORMAT_JSON) {
        print_json_report(&state, alias_count);
    } else if (state.format == FORMAT_JUNIT) {
        print_junit_report(&state, alias_count, alias_count, alias_registry);
    }

    free(ag_state);
    slp_vm_free(vm);
    return state.validation_failed ? 1 : 0;
}
