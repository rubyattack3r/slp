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
void err(void *ud, int line, const char *msg) { printf("ERR line %d: %s\n", line, msg); }
int main() {
    char *src = "sub my_func { return 42; }";
    SleepyVM *vm = sleepy_vm_new(&allocator);
    sleepy_vm_set_error_fn(vm, err, NULL);
    sleepy_vm_interpret(vm, src);
    
    // Now call it from C
    SleepyValue func = sleepy_obj_hash_get(vm->globals, SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "my_func", 7)));
    sleepy_vm_push(vm, func);
    sleepy_vm_call(vm, 0, 0);
    SleepyValue res = sleepy_vm_pop(vm);
    printf("Result: %g\n", SLEEPY_AS_NUM(res));
    return 0;
}
