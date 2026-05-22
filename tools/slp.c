#include "slp_common.h"
#include "slp_core.h"
#include "slp_vm.h"
#include "slp_compiler.h"
#include "slp_disasm.h"
#include "slp_parser.h"
#include "bestline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// Simple balance checker for multi-line REPL
static int check_balance(const char *str, int *braces, int *parens, int *brackets) {
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

static void completion(const char *buf, int pos, bestlineCompletions *lc) {
    const char *keywords[] = {
        "println", "size", "array", "keys", "typeof", "remove", "push", "pop", "assert",
        "if", "else", "while", "for", "foreach", "return", "break", "continue",
        "sub", "alias", "try", "catch", "throw", "true", "false", "null", NULL
    };

    int start = pos;
    while (start > 0 && ((buf[start - 1] >= 'a' && buf[start - 1] <= 'z') || 
                         (buf[start - 1] >= 'A' && buf[start - 1] <= 'Z') || 
                         buf[start - 1] == '_' || 
                         (buf[start - 1] >= '0' && buf[start - 1] <= '9'))) {
        start--;
    }

    int word_len = pos - start;
    if (word_len == 0) return;

    for (int i = 0; keywords[i] != NULL; i++) {
        if (strncmp(buf + start, keywords[i], word_len) == 0) {
            size_t new_len = start + strlen(keywords[i]) + strlen(buf + pos) + 1;
            char *new_buf = malloc(new_len);
            if (new_buf) {
                strncpy(new_buf, buf, start);
                strcpy(new_buf + start, keywords[i]);
                strcpy(new_buf + start + strlen(keywords[i]), buf + pos);
                bestlineAddCompletion(lc, new_buf);
                free(new_buf);
            }
        }
    }
}

static void pry_print_value(SlpValue value) {
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
                printf("\x1b[33m%lldL\x1b[0m", SLP_AS_LONG(value)->value);
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
                    pry_print_value(arr->elements[i]);
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
                        pry_print_value(hash->entries[i].key);
                        printf(" \x1b[1m=>\x1b[0m ");
                        pry_print_value(hash->entries[i].value);
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

int main(int argc, char **argv) {
    SlpVM *vm = slp_vm_new(&allocator);
    slp_vm_set_error_fn(vm, repl_error, NULL);
    slp_vm_set_write_fn(vm, repl_write, NULL);

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

        SlpResult r = slp_vm_interpret(vm, source);
        free(source);
        slp_vm_free(vm);
        return r == SLP_OK ? 0 : 1;
    }

    printf("SlpVM REPL v0.1\n");
    printf("Type 'exit' to quit.\n");
    
    bestlineSetCompletionCallback(completion);
    bestlineHistoryLoad(".slp_history");

    char *buffer = NULL;
    size_t buf_len = 0;

    while (1) {
        int braces = 0, parens = 0, brackets = 0;
        int in_string = 0;
        if (buffer) {
            in_string = check_balance(buffer, &braces, &parens, &brackets);
        }

        const char *prompt = (braces > 0 || parens > 0 || brackets > 0 || in_string) ? "... " : "> ";
        char *line = bestline(prompt);
        if (!line) {
            if (buffer) { free(buffer); buffer = NULL; buf_len = 0; printf("\n"); continue; }
            break; // EOF
        }

        if (!buffer && (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0)) {
            free(line);
            break;
        }

        if (line[0] != '\0') {
            size_t line_len = strlen(line);
            if (!buffer) {
                buffer = malloc(line_len + 2);
                strcpy(buffer, line);
                buf_len = line_len;
            } else {
                buffer = realloc(buffer, buf_len + line_len + 2);
                buffer[buf_len] = '\n';
                strcpy(buffer + buf_len + 1, line);
                buf_len += line_len + 1;
            }
        } else if (buffer) {
            buffer[buf_len] = '\n';
            buffer[buf_len+1] = '\0';
            buf_len++;
        }
        free(line);

        if (buffer) {
            braces = 0; parens = 0; brackets = 0;
            in_string = check_balance(buffer, &braces, &parens, &brackets);
            if (braces <= 0 && parens <= 0 && brackets <= 0 && !in_string) {
                bestlineHistoryAdd(buffer);
                bestlineHistorySave(".slp_history");

                // Quick heuristic: add semicolon if not present for simple lines
                if (buffer[buf_len - 1] != ';' && buffer[buf_len - 1] != '}') {
                    buffer = realloc(buffer, buf_len + 2);
                    buffer[buf_len] = ';';
                    buffer[buf_len + 1] = '\0';
                }
                
                // If it's a simple expression, we could evaluate and print it, 
                // but interpreting directly is safest.
                vm->repl_mode = true;
                SlpResult r = slp_vm_interpret(vm, buffer);
                vm->repl_mode = false;
                
                if (r == SLP_OK) {
                    // Try to grab the return value from the stack (if any)
                    // The compiler pushes the return value of OP_RETURN to the top
                    // Wait, our compiler returns the result of the top-level block.
                    // If it was an expression, slp_vm_interpret will pop it because slp_vm_run pops.
                    // Actually, let's peek the stack before we do anything.
                    // Wait, slp_vm_interpret pops the frame, but the result is on the stack *if* the compiler emitted OP_RETURN.
                    // In our compiler, repl_mode emits OP_RETURN which pushes the result.
                    SlpValue result = slp_vm_pop(vm);
                    
                    // We also don't want to print $null for variable assignments or block statements that return $null.
                    // But in a REPL it's standard to print $null too if it's the result, unless it's a completely empty line.
                    printf("\x1b[90m=>\x1b[0m ");
                    pry_print_value(result);
                    printf("\n");
                }
                
                free(buffer);
                buffer = NULL;
                buf_len = 0;
            }
        }
    }

    if (buffer) free(buffer);
    slp_vm_free(vm);
    return 0;
}
