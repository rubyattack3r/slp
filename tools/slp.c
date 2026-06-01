#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "slp_common.h"
#include "slp_core.h"
#include "slp_vm.h"
#include "slp_compiler.h"
#include "slp_parser.h"
#include "slp_ast.h"
#include "slp_stdlib.h"
#include "utils.h"
#include "console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

int main(int argc, char **argv) {
    SlpVM *vm = slp_vm_new(&allocator);
    slp_stdlib_init(vm);
    slp_vm_set_error_fn(vm, repl_error, NULL);
    slp_vm_set_write_fn(vm, repl_write, NULL);

    int check = 0;
    int eval = 0;
    int expr = 0;
    int time_script = 0;
    int start = 1;

    while (start < argc && argv[start][0] == '-') {
        if (strcmp(argv[start], "-version") == 0 || strcmp(argv[start], "--version") == 0 || strcmp(argv[start], "-v") == 0) {
            printf("Sleep v2.1\n");
            slp_vm_free(vm);
            return 0;
        }
        else if (strcmp(argv[start], "-help") == 0 || strcmp(argv[start], "--help") == 0 || strcmp(argv[start], "-h") == 0) {
            printf("Sleep v2.1\n");
            printf("Usage: slp [options] [-|file|expression] [args]\n");
            printf("       options:\n");
            printf("         -c --check     check the syntax of the specified file\n");
            printf("         -e --eval      evaluate a script as specified on command line\n");
            printf("         -h --help      display this help message\n");
            printf("         -t --time      display total script runtime\n");
            printf("         -v --version   display version information\n");
            printf("         -x --expr      evaluate an expression as specified on the command line\n");
            printf("       file:\n");
            printf("         specify a '-' to read script from STDIN\n");
            slp_vm_free(vm);
            return 0;
        }
        else if (strcmp(argv[start], "--check") == 0 || strcmp(argv[start], "-c") == 0) {
            check = 1;
        }
        else if (strcmp(argv[start], "--eval") == 0 || strcmp(argv[start], "-e") == 0) {
            eval = 1;
        }
        else if (strcmp(argv[start], "--expr") == 0 || strcmp(argv[start], "-x") == 0) {
            expr = 1;
        }
        else if (strcmp(argv[start], "--time") == 0 || strcmp(argv[start], "-t") == 0) {
            time_script = 1;
        }
        else if (strcmp(argv[start], "-") == 0) {
            break; // Stop parsing options, this is STDIN
        }
        else {
            fprintf(stderr, "Unknown argument: %s\n", argv[start]);
            slp_vm_free(vm);
            return 1;
        }
        start++;
    }

    if (start < argc || eval || expr || (start < argc && strcmp(argv[start], "-") == 0)) {
        char *source = NULL;
        if (eval || expr) {
            if (start >= argc) {
                fprintf(stderr, "Error: no expression or script specified for evaluation\n");
                slp_vm_free(vm);
                return 1;
            }
            if (expr) {
                size_t len = strlen(argv[start]) + 32;
                source = malloc(len);
                snprintf(source, len, "println(%s);", argv[start]);
            } else {
                source = strdup(argv[start]);
            }
            start++;
        } else if (start < argc) {
            if (strcmp(argv[start], "-") == 0) {
                size_t cap = 4096;
                size_t len = 0;
                source = malloc(cap);
                int c;
                while ((c = getchar()) != EOF) {
                    if (len + 1 >= cap) {
                        cap *= 2;
                        source = realloc(source, cap);
                    }
                    source[len++] = (char)c;
                }
                source[len] = '\0';
                start++;
            } else {
                source = utils_read_file(argv[start], NULL);
                if (!source) {
                    fprintf(stderr, "Could not open file: %s\n", argv[start]);
                    slp_vm_free(vm);
                    return 1;
                }
                start++;
            }
        }

        if (source) {
            // Populate @args in globals
            SlpObjArray *args_arr = slp_vm_new_array(vm);
            for (int x = start; x < argc; x++) {
                SlpObjString *arg_str = slp_vm_copy_cstr(vm, argv[x]);
                slp_obj_array_push(vm->allocator, args_arr, SLP_OBJ_VAL(arg_str));
            }
            SlpObjString *args_name = slp_vm_copy_string(vm, "@args", 5);
            slp_obj_hash_set(vm->allocator, vm->globals, SLP_OBJ_VAL(args_name), SLP_OBJ_VAL(args_arr));

            if (check) {
                SlpParser parser;
                slp_parser_init(&parser, source, vm->allocator);
                SlpASTNode *node = slp_parser_parse(&parser);
                if (parser.had_error || !node) {
                    fprintf(stderr, "Syntax check failed: %s at line %d\n",
                            parser.error_message ? parser.error_message : "Unknown error",
                            parser.error_line);
                    if (node) slp_parser_free_node(node, vm->allocator);
                    free(source);
                    slp_vm_free(vm);
                    return 1;
                }
                slp_parser_free_node(node, vm->allocator);
                printf("Syntax OK\n");
                free(source);
                slp_vm_free(vm);
                return 0;
            }

            double start_t = 0;
            if (time_script) {
                start_t = (double)clock() / (double)CLOCKS_PER_SEC;
            }

            SlpResult r = slp_vm_interpret(vm, source);

            if (time_script) {
                double end_t = (double)clock() / (double)CLOCKS_PER_SEC;
                printf("Runtime: %.4fs\n", end_t - start_t);
            }

            free(source);
            slp_vm_free(vm);
            return r == SLP_OK ? 0 : 1;
        }
    }

    SlpConsole *console = slp_console_new(vm);
    slp_console_run(console, ".slp_history");
    slp_console_free(console);

    slp_vm_free(vm);
    return 0;
}
