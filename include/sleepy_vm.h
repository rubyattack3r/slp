#ifndef SLEEPY_VM_H
#define SLEEPY_VM_H

#include "sleepy_value.h"
#include "sleepy_chunk.h"
#include "sleepy_gc.h"
#include "sleepy_opcodes.h"

#define SLEEPY_STACK_MAX   4096
#define SLEEPY_MAX_FRAMES  512
#define SLEEPY_MAX_HANDLERS 256
#define SLEEPY_INTERN_INIT_CAP 256

struct SleepyCallFrame {
    uint8_t *ip;
    SleepyObjClosure *closure;
    SleepyValue *slots;
};

struct SleepyTryHandler {
    uint8_t *catch_ip;
    int frame_count;
};

typedef struct SleepyTryHandler SleepyTryHandler;

typedef void (*SleepyVMErrorFn)(void *ud, int line, const char *msg);
typedef void (*SleepyVMWriteFn)(void *ud, const char *text);

typedef struct SleepyBridgeType SleepyBridgeType;

struct SleepyBridgeType {
    char *keyword;
    void (*handler)(SleepyVM *vm, const char *keyword, const char *identifier,
                    const char *string_arg, SleepyObjClosure *closure,
                    void *userdata);
    void *userdata;
    SleepyBridgeType *next;
};

struct SleepyVM {
    SleepyAllocator *allocator;

    SleepyValue stack[SLEEPY_STACK_MAX];
    SleepyValue *stack_top;

    SleepyCallFrame frames[SLEEPY_MAX_FRAMES];
    int frame_count;

    SleepyObjHash *globals;

    SleepyObj *objects;

    SleepyObjString **interned;
    int interned_count;
    int interned_capacity;

    SleepyObjUpvalue *open_upvalues;

    SleepyBridgeType *bridge_types;
    SleepyObjHash *natives;

    size_t bytes_allocated;
    size_t next_gc_threshold;

    SleepyObj **gc_gray_stack;
    int gc_gray_count;
    int gc_gray_capacity;

    SleepyVMErrorFn error_fn;
    void *error_userdata;
    SleepyVMWriteFn write_fn;
    void *write_userdata;

    SleepyValue ffi_slots[256];

    bool halted;
    SleepyValue thrown_exception;

    SleepyTryHandler try_handlers[SLEEPY_MAX_HANDLERS];
    int try_handler_count;

    bool repl_mode;
};

typedef enum {
    SLEEPY_OK,
    SLEEPY_COMPILE_ERROR,
    SLEEPY_RUNTIME_ERROR,
    SLEEPY_HALT
} SleepyResult;

SleepyVM *sleepy_vm_new(SleepyAllocator *allocator);
void sleepy_vm_free(SleepyVM *vm);

SleepyResult sleepy_vm_interpret(SleepyVM *vm, const char *source);
SleepyResult sleepy_vm_run(SleepyVM *vm, SleepyObjFunction *fn);

void sleepy_vm_push(SleepyVM *vm, SleepyValue value);
SleepyValue sleepy_vm_pop(SleepyVM *vm);
SleepyValue sleepy_vm_peek(SleepyVM *vm, int distance);

SleepyObjString *sleepy_vm_intern_string(SleepyVM *vm, const char *chars, uint32_t length);
SleepyObjString *sleepy_vm_copy_string(SleepyVM *vm, const char *chars, uint32_t length);

void sleepy_vm_register_bridge_type(SleepyVM *vm, const char *keyword,
    void (*handler)(SleepyVM*, const char*, const char*, const char*,
                    SleepyObjClosure*, void*),
    void *userdata);
void sleepy_vm_register_native(SleepyVM *vm, const char *name, SleepyNativeFn fn);

void sleepy_vm_set_error_fn(SleepyVM *vm, SleepyVMErrorFn fn, void *ud);
void sleepy_vm_set_write_fn(SleepyVM *vm, SleepyVMWriteFn fn, void *ud);

void sleepy_vm_ffi_set_null(SleepyVM *vm, int slot);
void sleepy_vm_ffi_set_bool(SleepyVM *vm, int slot, bool val);
void sleepy_vm_ffi_set_number(SleepyVM *vm, int slot, double val);
void sleepy_vm_ffi_set_long(SleepyVM *vm, int slot, int64_t val);
void sleepy_vm_ffi_set_string(SleepyVM *vm, int slot, const char *val);

bool sleepy_vm_ffi_is_null(SleepyVM *vm, int slot);
bool sleepy_vm_ffi_is_bool(SleepyVM *vm, int slot);
bool sleepy_vm_ffi_is_number(SleepyVM *vm, int slot);
bool sleepy_vm_ffi_is_string(SleepyVM *vm, int slot);
bool sleepy_vm_ffi_get_bool(SleepyVM *vm, int slot);
double sleepy_vm_ffi_get_number(SleepyVM *vm, int slot);
int64_t sleepy_vm_ffi_get_long(SleepyVM *vm, int slot);
const char *sleepy_vm_ffi_get_string(SleepyVM *vm, int slot);

void sleepy_vm_runtime_error(SleepyVM *vm, const char *msg);

#endif // SLEEPY_H
