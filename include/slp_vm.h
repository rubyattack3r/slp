#ifndef SLP_VM_H
#define SLP_VM_H

#include "slp_value.h"
#include "slp_chunk.h"
#include "slp_gc.h"
#include "slp_opcodes.h"

#define SLP_STACK_MAX   4096
#define SLP_MAX_FRAMES  512
#define SLP_MAX_HANDLERS 256
#define SLP_INTERN_INIT_CAP 256

struct SlpCallFrame {
    uint8_t *ip;
    SlpObjClosure *closure;
    SlpValue *slots;
};

struct SlpTryHandler {
    uint8_t *catch_ip;
    int frame_count;
};

typedef struct SlpTryHandler SlpTryHandler;

typedef void (*SlpVMErrorFn)(void *ud, int line, const char *msg);
typedef void (*SlpVMWriteFn)(void *ud, const char *text);

typedef struct SlpBridgeType SlpBridgeType;

struct SlpBridgeType {
    char *keyword;
    void (*handler)(SlpVM *vm, const char *keyword, const char *identifier,
                    const char *string_arg, SlpObjClosure *closure,
                    void *userdata);
    void *userdata;
    SlpBridgeType *next;
};

struct SlpVM {
    SlpAllocator *allocator;

    SlpValue stack[SLP_STACK_MAX];
    SlpValue *stack_top;

    SlpCallFrame frames[SLP_MAX_FRAMES];
    int frame_count;

    SlpObjHash *globals;

    SlpObj *objects;

    SlpObjString **interned;
    int interned_count;
    int interned_capacity;

    SlpObjUpvalue *open_upvalues;

    SlpBridgeType *bridge_types;
    SlpObjHash *natives;

    size_t bytes_allocated;
    size_t next_gc_threshold;

    SlpObj **gc_gray_stack;
    int gc_gray_count;
    int gc_gray_capacity;

    SlpVMErrorFn error_fn;
    void *error_userdata;
    SlpVMWriteFn write_fn;
    void *write_userdata;

    SlpValue ffi_slots[256];

    bool halted;
    SlpValue thrown_exception;

    SlpTryHandler try_handlers[SLP_MAX_HANDLERS];
    int try_handler_count;

    bool repl_mode;
};

typedef enum {
    SLP_OK,
    SLP_COMPILE_ERROR,
    SLP_RUNTIME_ERROR,
    SLP_HALT
} SlpResult;

SlpVM *slp_vm_new(SlpAllocator *allocator);
void slp_vm_free(SlpVM *vm);

SlpResult slp_vm_interpret(SlpVM *vm, const char *source);
SlpResult slp_vm_run(SlpVM *vm, SlpObjFunction *fn);

void slp_vm_push(SlpVM *vm, SlpValue value);
SlpValue slp_vm_pop(SlpVM *vm);
SlpValue slp_vm_peek(SlpVM *vm, int distance);

SlpObjString *slp_vm_intern_string(SlpVM *vm, const char *chars, uint32_t length);
SlpObjString *slp_vm_copy_string(SlpVM *vm, const char *chars, uint32_t length);

void slp_vm_register_bridge_type(SlpVM *vm, const char *keyword,
    void (*handler)(SlpVM*, const char*, const char*, const char*,
                    SlpObjClosure*, void*),
    void *userdata);
void slp_vm_register_native(SlpVM *vm, const char *name, SlpNativeFn fn);

void slp_vm_set_error_fn(SlpVM *vm, SlpVMErrorFn fn, void *ud);
void slp_vm_set_write_fn(SlpVM *vm, SlpVMWriteFn fn, void *ud);

void slp_vm_ffi_set_null(SlpVM *vm, int slot);
void slp_vm_ffi_set_bool(SlpVM *vm, int slot, bool val);
void slp_vm_ffi_set_number(SlpVM *vm, int slot, double val);
void slp_vm_ffi_set_long(SlpVM *vm, int slot, int64_t val);
void slp_vm_ffi_set_string(SlpVM *vm, int slot, const char *val);

bool slp_vm_ffi_is_null(SlpVM *vm, int slot);
bool slp_vm_ffi_is_bool(SlpVM *vm, int slot);
bool slp_vm_ffi_is_number(SlpVM *vm, int slot);
bool slp_vm_ffi_is_string(SlpVM *vm, int slot);
bool slp_vm_ffi_get_bool(SlpVM *vm, int slot);
double slp_vm_ffi_get_number(SlpVM *vm, int slot);
int64_t slp_vm_ffi_get_long(SlpVM *vm, int slot);
const char *slp_vm_ffi_get_string(SlpVM *vm, int slot);

void slp_vm_runtime_error(SlpVM *vm, const char *msg);

#endif // SLP_H
