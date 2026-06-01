#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "slp_common.h"
#include "slp_core.h"
#include "slp_vm.h"
#include "slp_compiler.h"
#include "slp_disasm.h"
#include "slp_parser.h"
#include "slp_ast.h"
#include "utils.h"

static void *std_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}

static SlpAllocator allocator = {std_alloc, NULL};

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <input_file>\n", prog);
}

static void disassemble_function(SlpObjFunction *fn, FILE *out) {
    const char *name = fn->name ? fn->name->chars : "<script>";
    slp_disasm_chunk(fn->chunk, name, out);

    // Recursively check constant pool for other functions/closures defined in the scope
    for (int i = 0; i < fn->chunk->constant_count; i++) {
        SlpValue val = fn->chunk->constants[i];
        if (SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_FUNCTION) {
            fprintf(out, "\n");
            disassemble_function((SlpObjFunction*)SLP_AS_OBJ(val), out);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argc > 0 ? argv[0] : "slpd");
        return argc < 2 ? 1 : 0;
    }

    const char *input_path = argv[1];
    char *source = utils_read_file(input_path, NULL);
    if (!source) {
        fprintf(stderr, "Error: Could not read input file '%s'\n", input_path);
        return 1;
    }

    SlpParser parser;
    slp_parser_init(&parser, source, &allocator);
    SlpASTNode *ast = slp_parser_parse(&parser);
    if (!ast || parser.had_error) {
        fprintf(stderr, "Error parsing file '%s':\n", input_path);
        if (parser.error_message) {
            fprintf(stderr, "  Line %d: %s\n", parser.error_line, parser.error_message);
        }
        free(source);
        return 1;
    }

    SlpVM *vm = slp_vm_new(&allocator);
    if (!vm) {
        fprintf(stderr, "Error: Could not create Slp VM\n");
        slp_parser_free_node(ast, &allocator);
        free(source);
        return 1;
    }

    SlpObjFunction *fn = slp_compile(vm, ast, &allocator);
    if (!fn) {
        fprintf(stderr, "Error: Compilation failed for '%s'\n", input_path);
        slp_vm_free(vm);
        slp_parser_free_node(ast, &allocator);
        free(source);
        return 1;
    }

    disassemble_function(fn, stdout);

    slp_parser_free_node(ast, &allocator);
    slp_vm_free(vm);
    free(source);
    return 0;
}
