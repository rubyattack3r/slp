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
    FILE *f = fopen("tests/fixtures_vm/stdlib.sl", "r");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    fread(buf, 1, size, f);
    buf[size] = 0;
    
    SleepyVM *vm = sleepy_vm_new(&allocator);
    sleepy_vm_set_error_fn(vm, err, NULL);
    SleepyResult r = sleepy_vm_interpret(vm, buf);
    printf("Result: %d\n", r);
    return 0;
}
