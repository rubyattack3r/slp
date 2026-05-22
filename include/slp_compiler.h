#ifndef SLP_COMPILER_H
#define SLP_COMPILER_H

#include "slp_vm.h"
#include "slp_parser.h"

typedef struct {
    SlpObjString *name;
    int depth;
    bool is_captured;
} SlpLocal;

typedef struct {
    uint8_t index;
    bool is_local;
} SlpCompilerUpvalue;

typedef struct SlpCompiler {
    struct SlpCompiler *enclosing;

    SlpObjFunction *function;
    SlpVM *vm;
    SlpAllocator *allocator;

    SlpLocal locals[256];
    int local_count;
    int scope_depth;

    SlpCompilerUpvalue upvalues[256];
    int upvalue_count;

    int loop_start;
    int loop_continue_target;
    int loop_exit_jump;
    int loop_scope_depth;
    int break_jumps[256];
    int break_jump_count;
    int continue_jumps[256];
    int continue_jump_count;

    bool had_error;
    int error_line;
    const char *error_message;
} SlpCompiler;

SlpObjFunction *slp_compile(SlpVM *vm, SlpASTNode *ast, SlpAllocator *allocator);

#endif // SLP_H
