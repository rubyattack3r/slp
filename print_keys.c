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
    char *src = "%h = %(1 => \"a\", 2 => \"b\"); @k = keys(%h); println(@k[0]); println(@k[1]);";
    SleepyVM *vm = sleepy_vm_new(&allocator);
    sleepy_vm_interpret(vm, src);
    return 0;
}
