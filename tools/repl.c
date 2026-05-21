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

int main(int argc, char **argv) {
    SleepyVM *vm = sleepy_vm_new(&allocator);
    sleepy_vm_set_error_fn(vm, repl_error, NULL);
    sleepy_vm_set_write_fn(vm, repl_write, NULL);

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        if (!f) {
            fprintf(stderr, "Could not open file: %s\n", argv[1]);
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
        sleepy_vm_free(vm);
        return r == SLEEPY_OK ? 0 : 1;
    }

    printf("SleepyVM REPL v0.1\n");
    printf("Type 'exit' to quit.\n");

    while (1) {
        char *line = bestline("> ");
        if (!line) break;

        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            free(line);
            break;
        }

        if (line[0] != '\0') {
            bestlineHistoryAdd(line);
            size_t len = strlen(line);
            char *source = (char *)malloc(len + 2);
            strcpy(source, line);
            if (source[len - 1] != ';' && source[len - 1] != '}') {
                source[len] = ';';
                source[len + 1] = '\0';
            }
            
            SleepyResult r = sleepy_vm_interpret(vm, source);
            // Print top of stack if ok, but interpret pops the whole stack.
            // But wait, interpret runs a full script, the result is lost. That's fine.
            (void)r;
            free(source);
        }
        free(line);
    }

    sleepy_vm_free(vm);
    return 0;
}
