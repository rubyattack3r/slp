#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "sleepy_common.h"
#include "sleepy_core.h"
#include "sleepy_vm.h"
#include "sleepy_compiler.h"
#include "sleepy_disasm.h"
#include "sleepy_parser.h"
#include "sleepy_ast.h"

static void *std_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}

static SleepyAllocator allocator = {std_alloc, NULL};

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not open input file '%s'\n", path);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = (char *)malloc(size + 1);
    if (!buffer) {
        fclose(file);
        fprintf(stderr, "Error: Memory allocation failed for file buffer\n");
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, size, file);
    buffer[read_bytes] = '\0';
    fclose(file);
    return buffer;
}

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <input_file>\n", prog);
}

static void disassemble_function(SleepyObjFunction *fn, FILE *out) {
    const char *name = fn->name ? fn->name->chars : "<script>";
    sleepy_disasm_chunk(fn->chunk, name, out);

    // Recursively check constant pool for other functions/closures defined in the scope
    for (int i = 0; i < fn->chunk->constant_count; i++) {
        SleepyValue val = fn->chunk->constants[i];
        if (SLEEPY_IS_OBJ(val) && SLEEPY_OBJ_TYPE(val) == SLEEPY_OBJ_FUNCTION) {
            fprintf(out, "\n");
            disassemble_function((SleepyObjFunction*)SLEEPY_AS_OBJ(val), out);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argc > 0 ? argv[0] : "sleepyd");
        return argc < 2 ? 1 : 0;
    }

    const char *input_path = argv[1];
    char *source = read_file(input_path);
    if (!source) {
        return 1;
    }

    SleepyParser parser;
    sleepy_parser_init(&parser, source, &allocator);
    SleepyASTNode *ast = sleepy_parser_parse(&parser);
    if (!ast || parser.had_error) {
        fprintf(stderr, "Error parsing file '%s':\n", input_path);
        if (parser.error_message) {
            fprintf(stderr, "  Line %d: %s\n", parser.error_line, parser.error_message);
        }
        free(source);
        return 1;
    }

    SleepyVM *vm = sleepy_vm_new(&allocator);
    if (!vm) {
        fprintf(stderr, "Error: Could not create Sleepy VM\n");
        sleepy_parser_free_node(ast, &allocator);
        free(source);
        return 1;
    }

    SleepyObjFunction *fn = sleepy_compile(vm, ast, &allocator);
    if (!fn) {
        fprintf(stderr, "Error: Compilation failed for '%s'\n", input_path);
        sleepy_vm_free(vm);
        sleepy_parser_free_node(ast, &allocator);
        free(source);
        return 1;
    }

    disassemble_function(fn, stdout);

    sleepy_parser_free_node(ast, &allocator);
    sleepy_vm_free(vm);
    free(source);
    return 0;
}
