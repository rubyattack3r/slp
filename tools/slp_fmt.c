#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "slp_common.h"
#include "slp_core.h"
#include "slp_parser.h"
#include "slp_ast.h"

static void *std_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}

static SlpAllocator allocator = {std_alloc, NULL};

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] <input_file>\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -w          Write formatted code back to the input file in-place.\n");
    fprintf(stderr, "  -o <file>   Write formatted code to the specified output file.\n");
    fprintf(stderr, "  -h, --help  Show this help message.\n");
}

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

int main(int argc, char **argv) {
    bool in_place = false;
    const char *output_path = NULL;
    const char *input_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-w") == 0) {
            in_place = true;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Option '-o' requires an argument.\n");
                print_usage(argv[0]);
                return 1;
            }
            output_path = argv[++i];
        } else {
            if (input_path) {
                fprintf(stderr, "Error: Multiple input files specified.\n");
                print_usage(argv[0]);
                return 1;
            }
            input_path = argv[i];
        }
    }

    if (!input_path) {
        fprintf(stderr, "Error: No input file specified.\n");
        print_usage(argv[0]);
        return 1;
    }

    if (in_place && output_path) {
        fprintf(stderr, "Error: Cannot specify both '-w' and '-o'.\n");
        print_usage(argv[0]);
        return 1;
    }

    char *source = read_file(input_path);
    if (!source) {
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

    char *formatted = slp_ast_format(ast, &allocator);
    if (!formatted) {
        fprintf(stderr, "Error formatting AST for '%s'\n", input_path);
        slp_parser_free_node(ast, &allocator);
        free(source);
        return 1;
    }

    const char *target = input_path;
    if (output_path) {
        target = output_path;
    }

    if (in_place || output_path) {
        FILE *out = fopen(target, "wb");
        if (!out) {
            fprintf(stderr, "Error: Could not open target file '%s' for writing.\n", target);
            allocator.reallocate(formatted, 0, allocator.user_data);
            slp_parser_free_node(ast, &allocator);
            free(source);
            return 1;
        }
        fputs(formatted, out);
        fclose(out);
    } else {
        printf("%s", formatted);
    }

    allocator.reallocate(formatted, 0, allocator.user_data);
    slp_parser_free_node(ast, &allocator);
    free(source);
    return 0;
}
