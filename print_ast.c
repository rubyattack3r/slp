#include "sleepy_common.h"
#include "sleepy_core.h"
#include "sleepy_vm.h"
#include "sleepy_compiler.h"
#include "sleepy_disasm.h"
#include "sleepy_parser.h"
#include <stdio.h>
#include <stdlib.h>
static void *alloc(void *ptr, size_t size, void *ud) {
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static SleepyAllocator allocator = {alloc, NULL};

int main() {
    char *src = "sub make_counter { return sub { $1 = $1 + 1; return $1; }; }";
    SleepyVM *vm = sleepy_vm_new(&allocator);
    SleepyParser parser;
    sleepy_parser_init(&parser, src, &allocator);
    SleepyASTNode *ast = sleepy_parser_parse(&parser);
    SleepyObjFunction *fn = sleepy_compile(vm, ast, &allocator);
    
    // We want to see the inner chunks. Let's dig into the constants of fn.
    SleepyObjFunction *sub_fn = SLEEPY_AS_FUNCTION(fn->chunk->constants[0]);
    printf("make_counter chunk:\n");
    sleepy_disasm_chunk(sub_fn->chunk, "make_counter", stdout);
    
    SleepyObjFunction *inner_fn = SLEEPY_AS_FUNCTION(sub_fn->chunk->constants[0]);
    printf("\ninner sub chunk:\n");
    sleepy_disasm_chunk(inner_fn->chunk, "inner sub", stdout);

    return 0;
}
