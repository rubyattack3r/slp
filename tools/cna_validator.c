#include "sleepy_common.h"
#include "sleepy_core.h"
#include "sleepy_vm.h"
#include "sleepy_compiler.h"
#include "sleepy_disasm.h"
#include "sleepy_parser.h"
#include "bestline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// Global state for open files
#define MAX_OPEN_FILES 32
static FILE *open_files[MAX_OPEN_FILES];

// Global registry for aliases
typedef struct {
    char name[128];
    SleepyObjClosure *closure;
} AliasRecord;

#define MAX_ALIASES 512
static AliasRecord alias_registry[MAX_ALIASES];
static int alias_count = 0;

// Bridge handler for 'alias'
static void alias_handler(SleepyVM *vm, const char *keyword, const char *name, const char *str, SleepyObjClosure *closure, void *ud) {
    if (!name) return;
    
    // Store in our registry
    if (alias_count < MAX_ALIASES) {
        strncpy(alias_registry[alias_count].name, name, sizeof(alias_registry[alias_count].name) - 1);
        alias_registry[alias_count].name[sizeof(alias_registry[alias_count].name) - 1] = '\0';
        alias_registry[alias_count].closure = closure;
        alias_count++;
    }
    
    // Also store globally in VM as __alias_NAME so we can easily interpret it
    char global_name[256];
    snprintf(global_name, sizeof(global_name), "__alias_%s", name);
    SleepyObjString *gname = sleepy_vm_copy_string(vm, global_name, strlen(global_name));
    sleepy_obj_hash_set(vm->allocator, vm->globals, SLEEPY_OBJ_VAL(gname), SLEEPY_OBJ_VAL(closure));
}

// Dummy native functions
static SleepyValue native_barch(SleepyVM *vm, SleepyValue *args, int argc) {
    return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "x64", 3));
}

static SleepyValue native_script_resource(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc < 1 || !(SLEEPY_IS_OBJ(args[0]) && SLEEPY_OBJ_TYPE(args[0]) == SLEEPY_OBJ_STRING)) return SLEEPY_NULL_VAL;
    SleepyObjString *str = SLEEPY_AS_STRING(args[0]);
    // Just return the same path for diagnostics
    return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, str->chars, str->length));
}

static SleepyValue native_bof_pack(SleepyVM *vm, SleepyValue *args, int argc) {
    return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "packed_bof_args", 15));
}

static SleepyValue native_btask(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc >= 2 && (SLEEPY_IS_OBJ(args[1]) && SLEEPY_OBJ_TYPE(args[1]) == SLEEPY_OBJ_STRING)) {
        printf("[Task] %s\n", SLEEPY_AS_STRING(args[1])->chars);
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_blog(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc >= 2 && (SLEEPY_IS_OBJ(args[1]) && SLEEPY_OBJ_TYPE(args[1]) == SLEEPY_OBJ_STRING)) {
        printf("[Log] %s\n", SLEEPY_AS_STRING(args[1])->chars);
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_berror(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc >= 2 && (SLEEPY_IS_OBJ(args[1]) && SLEEPY_OBJ_TYPE(args[1]) == SLEEPY_OBJ_STRING)) {
        printf("\x1b[31m[Error]\x1b[0m %s\n", SLEEPY_AS_STRING(args[1])->chars);
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_beacon_command_register(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc >= 1 && (SLEEPY_IS_OBJ(args[0]) && SLEEPY_OBJ_TYPE(args[0]) == SLEEPY_OBJ_STRING)) {
        printf("[+] Registered command: %s\n", SLEEPY_AS_STRING(args[0])->chars);
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_beacon_inline_execute(SleepyVM *vm, SleepyValue *args, int argc) {
    printf("[+] \x1b[36mbeacon_inline_execute\x1b[0m triggered!\n");
    if (argc >= 2 && (SLEEPY_IS_OBJ(args[1]) && SLEEPY_OBJ_TYPE(args[1]) == SLEEPY_OBJ_STRING)) {
        printf("    -> BOF data length: %d bytes\n", SLEEPY_AS_STRING(args[1])->length);
    }
    if (argc >= 3 && (SLEEPY_IS_OBJ(args[2]) && SLEEPY_OBJ_TYPE(args[2]) == SLEEPY_OBJ_STRING)) {
        printf("    -> Entrypoint: %s\n", SLEEPY_AS_STRING(args[2])->chars);
    }
    if (argc >= 4 && (SLEEPY_IS_OBJ(args[3]) && SLEEPY_OBJ_TYPE(args[3]) == SLEEPY_OBJ_STRING)) {
        printf("    -> Args length: %d bytes\n", SLEEPY_AS_STRING(args[3])->length);
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_openf(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc < 1 || !(SLEEPY_IS_OBJ(args[0]) && SLEEPY_OBJ_TYPE(args[0]) == SLEEPY_OBJ_STRING)) return SLEEPY_NUM_VAL(-1);
    const char *path = SLEEPY_AS_STRING(args[0])->chars;
    
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i]) {
            FILE *f = fopen(path, "rb");
            if (f) {
                open_files[i] = f;
                return SLEEPY_NUM_VAL(i);
            }
            break;
        }
    }
    return SLEEPY_NUM_VAL(-1);
}

static SleepyValue native_readb(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc < 2 || !SLEEPY_IS_NUM(args[0]) || !SLEEPY_IS_NUM(args[1])) return SLEEPY_NULL_VAL;
    int handle = (int)SLEEPY_AS_NUM(args[0]);
    int size = (int)SLEEPY_AS_NUM(args[1]);
    
    if (handle < 0 || handle >= MAX_OPEN_FILES || !open_files[handle]) return SLEEPY_NULL_VAL;
    
    FILE *f = open_files[handle];
    if (size == -1) {
        long current = ftell(f);
        fseek(f, 0, SEEK_END);
        long end = ftell(f);
        fseek(f, current, SEEK_SET);
        size = end - current;
    }
    
    if (size <= 0) return SLEEPY_NULL_VAL;
    
    char *buf = malloc(size);
    size_t read_bytes = fread(buf, 1, size, f);
    
    SleepyObjString *str = sleepy_vm_copy_string(vm, buf, read_bytes);
    free(buf);
    return SLEEPY_OBJ_VAL(str);
}

static SleepyValue native_closef(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc < 1 || !SLEEPY_IS_NUM(args[0])) return SLEEPY_NULL_VAL;
    int handle = (int)SLEEPY_AS_NUM(args[0]);
    if (handle >= 0 && handle < MAX_OPEN_FILES && open_files[handle]) {
        fclose(open_files[handle]);
        open_files[handle] = NULL;
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_local(SleepyVM *vm, SleepyValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
    return SLEEPY_NULL_VAL; // Stub for local variable declaration
}

static SleepyValue native_strlen(SleepyVM *vm, SleepyValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLEEPY_NUM_VAL(0);
    if (!SLEEPY_IS_OBJ(args[0]) || SLEEPY_AS_OBJ(args[0])->type != SLEEPY_OBJ_STRING) return SLEEPY_NUM_VAL(0);
    return SLEEPY_NUM_VAL((double)((SleepyObjString*)SLEEPY_AS_OBJ(args[0]))->length);
}

static SleepyValue native_int(SleepyVM *vm, SleepyValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLEEPY_NUM_VAL(0);
    if (!SLEEPY_IS_NUM(args[0])) return SLEEPY_NUM_VAL(0);
    return SLEEPY_NUM_VAL((double)(int)SLEEPY_AS_NUM(args[0]));
}

static SleepyValue native_substr(SleepyVM *vm, SleepyValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_print(SleepyVM *vm, SleepyValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_ticks(SleepyVM *vm, SleepyValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
    return SLEEPY_NUM_VAL(0);
}

static SleepyValue native_iff(SleepyVM *vm, SleepyValue *args, int argc) {
    (void)vm;
    if (argc < 3) return SLEEPY_NULL_VAL;
    if (SLEEPY_IS_TRUE(args[0])) return args[1];
    return args[2];
}

// ... completion logic
static void completion(const char *buf, int pos, bestlineCompletions *lc) {
    int start = pos;
    while (start > 0 && buf[start - 1] != ' ') {
        start--;
    }
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

int main(int argc, char **argv) {
    bool use_repl = false;
    const char *script_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--repl") == 0) {
            use_repl = true;
        } else if (script_file == NULL) {
            script_file = argv[i];
        }
    }

    if (!script_file) {
        printf("Usage: %s <bundle.cna> [--repl]\n", argv[0]);
        return 0;
    }

    for (int i = 0; i < MAX_OPEN_FILES; i++) open_files[i] = NULL;

    SleepyVM *vm = sleepy_vm_new(&allocator);
    sleepy_vm_set_error_fn(vm, repl_error, NULL);
    sleepy_vm_set_write_fn(vm, repl_write, NULL);

    sleepy_vm_register_bridge_type(vm, "alias", alias_handler, NULL);
    sleepy_vm_register_native(vm, "barch", native_barch);
    sleepy_vm_register_native(vm, "script_resource", native_script_resource);
    sleepy_vm_register_native(vm, "bof_pack", native_bof_pack);
    sleepy_vm_register_native(vm, "btask", native_btask);
    sleepy_vm_register_native(vm, "blog", native_blog);
    sleepy_vm_register_native(vm, "berror", native_berror);
    sleepy_vm_register_native(vm, "beacon_command_register", native_beacon_command_register);
    sleepy_vm_register_native(vm, "beacon_inline_execute", native_beacon_inline_execute);
    sleepy_vm_register_native(vm, "openf", native_openf);
    sleepy_vm_register_native(vm, "readb", native_readb);
    sleepy_vm_register_native(vm, "closef", native_closef);
    sleepy_vm_register_native(vm, "local", native_local);
    sleepy_vm_register_native(vm, "strlen", native_strlen);
    sleepy_vm_register_native(vm, "int", native_int);
    sleepy_vm_register_native(vm, "substr", native_substr);
    sleepy_vm_register_native(vm, "print", native_print);
    sleepy_vm_register_native(vm, "ticks", native_ticks);
    sleepy_vm_register_native(vm, "iff", native_iff);

    printf("[*] Loading and validating %s...\n", script_file);
    FILE *f = fopen(script_file, "rb");
    if (!f) {
        fprintf(stderr, "Could not open file: %s\n", script_file);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
        char *source = (char *)malloc(fsize + 1);
        fread(source, 1, fsize, f);
        source[fsize] = '\0';
        fclose(f);

    SleepyResult r = sleepy_vm_interpret(vm, source);
    free(source);
    if (r != SLEEPY_OK) {
        fprintf(stderr, "[!] Validation failed: error evaluating the script.\n");
        return 1;
    } else {
        printf("[+] Validation successful.\n");
        printf("[+] Aliases successfully registered: %d\n", alias_count);
    }

    if (!use_repl) {
        sleepy_vm_free(vm);
        return 0;
    }

    printf("\n\x1b[32m=== Aggressor REPL ===\x1b[0m\n");
    printf("Type an alias to execute (e.g. 'sc_config arg1 arg2'). Type 'exit' to quit.\n");
    printf("Registered aliases: %d\n\n", alias_count);
    
    bestlineSetCompletionCallback(completion);
    bestlineHistoryLoad(".aggressor_history");

    while (1) {
        char *line = bestline("aggressor> ");
        if (!line) break;

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            free(line);
            break;
        }

        if (line[0] != '\0') {
            bestlineHistoryAdd(line);
            bestlineHistorySave(".aggressor_history");

            // Extremely basic space tokenization to pass args to the alias
            char *cmd = strdup(line);
            char *args_str = "";
            char *space = strchr(cmd, ' ');
            if (space) {
                *space = '\0';
                args_str = space + 1;
            }

            // Look up alias
            int found = 0;
            for (int i = 0; i < alias_count; i++) {
                if (strcmp(alias_registry[i].name, cmd) == 0) {
                    found = 1;
                    // Format sleep interpret string: __alias_NAME("arg1 arg2");
                    // In true sleep, arguments are passed individually.
                    // For our REPL, we'll pass the rest of the line as a single string argument,
                    // or maybe we should pass '1' (dummy bid) as $1, and the args as $2?
                    // Usually an alias has signature: alias foo { $bid = $1; $args = $2; ... }
                    char eval_buf[4096];
                    snprintf(eval_buf, sizeof(eval_buf), "__alias_%s('1', '%s');", cmd, args_str);
                    
                    vm->repl_mode = true;
                    sleepy_vm_interpret(vm, eval_buf);
                    vm->repl_mode = false;
                    break;
                }
            }
            if (!found) {
                printf("[-] Alias '%s' not found.\n", cmd);
            }
            free(cmd);
        }
        free(line);
    }

    sleepy_vm_free(vm);
    return 0;
}
