#ifndef SLEEPY_COMPILER_H
#define SLEEPY_COMPILER_H

#include "sleepy_vm.h"
#include "sleepy_parser.h"

typedef struct {
    SleepyObjString *name;
    int depth;
    bool is_captured;
} SleepyLocal;

typedef struct {
    uint8_t index;
    bool is_local;
} SleepyCompilerUpvalue;

typedef struct SleepyCompiler {
    struct SleepyCompiler *enclosing;

    SleepyObjFunction *function;
    SleepyVM *vm;
    SleepyAllocator *allocator;

    SleepyLocal locals[256];
    int local_count;
    int scope_depth;

    SleepyCompilerUpvalue upvalues[256];
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
} SleepyCompiler;

SleepyObjFunction *sleepy_compile(SleepyVM *vm, SleepyASTNode *ast, SleepyAllocator *allocator);

#endif // SLEEPY_H
