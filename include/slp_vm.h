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
    SlpObjArray *local_scopes;
    SlpObjHash *closure_scope;
    bool foreach_pending;
    SlpObjArray *foreach_array;
    SlpObjHash *foreach_hash;
    SlpValue foreach_key;
    int foreach_index;
    int foreach_iterator_offset;
    bool foreach_active;
    SlpObjArray *foreach_iteration_array;
    uint64_t foreach_iteration_version;
    SlpValue foreach_last_key;
    SlpValue foreach_last_value;
    bool foreach_invalidated;
    SlpObjContinuation *continuation_return;
    SlpObjString *trace_call;
    SlpObjString *trace_source;
    int trace_line;
    bool trace_enabled;
    bool tainted_arguments;
};

struct SlpTryHandler {
    uint8_t *catch_ip;
    int frame_count;
    int stack_count;
};

typedef void (*SlpVMErrorFn)(void *ud, int line, const char *msg);
typedef void (*SlpVMWriteFn)(void *ud, const char *text);

typedef struct SlpBridgeType SlpBridgeType;
typedef struct SlpCallable SlpCallable;

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
    char compile_error_message[256];
    int compile_error_line;
    char *source_name;
    char *source_path;
    int debug_flags;

    SlpValue ffi_slots[256];
    SlpCallable *callables;

    bool halted;
    bool abort_requested;
    bool flow_exit_requested;
    SlpValue thrown_exception;
    /*
     * Sleep's ScriptEnvironment.flagError/checkError pair is a single,
     * destructive-read error slot. Keep it as a GC root because bridge
     * failures commonly store portable Java exception objects here.
     */
    SlpValue last_error;

    SlpTryHandler try_handlers[SLP_MAX_HANDLERS];
    int try_handler_count;

    SlpObjArray *last_regex_matches;
    SlpObjArray *last_stack_trace;
    SlpObjArray *profile_statistics;
    SlpObjArray *packed_objects;
    SlpObjIOHandle *console_handle;
    uint64_t next_closure_identity;
    uint64_t next_proxy_identity;
    void *user_data;
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
SlpResult slp_vm_eval_inline(
    SlpVM *vm, const char *source,
    const char *source_name,
    SlpValue *result_value);
SlpObjClosure *slp_vm_compile_closure_source(
    SlpVM *vm, const char *source,
    const char *source_name);
SlpResult slp_vm_run(SlpVM *vm, SlpObjFunction *fn);
SlpResult slp_vm_call(SlpVM *vm, int arg_count, bool has_message);
SlpResult slp_vm_call_value(
    SlpVM *vm, SlpValue callable,
    const SlpValue *arguments, int argument_count,
    SlpValue *out_result);

void slp_vm_push(SlpVM *vm, SlpValue value);
SlpValue slp_vm_pop(SlpVM *vm);
SlpValue slp_vm_peek(SlpVM *vm, int distance);

SlpObjString *slp_vm_intern_string(SlpVM *vm, const char *chars, uint32_t length);
SlpObjString *slp_vm_copy_string(SlpVM *vm, const char *chars, uint32_t length);
SlpObjString *slp_vm_copy_cstr(SlpVM *vm, const char *cstr);
SlpObjClass *slp_vm_new_class(SlpVM *vm, const char *name);
SlpObjArray *slp_vm_new_array(SlpVM *vm);
SlpObjHash *slp_vm_new_hash(SlpVM *vm);
SlpObjJavaObject *slp_vm_new_java_object(
    SlpVM *vm, const char *class_name,
    SlpJavaObjectKind kind);
SlpObjIOHandle *slp_vm_get_console_handle(SlpVM *vm);
SlpValue slp_vm_new_error(
    SlpVM *vm, const char *class_name,
    const char *message, const char *formatted,
    const char *display);
SlpObjLong *slp_vm_new_long(SlpVM *vm, int64_t value);
SlpObjDouble *slp_vm_new_double(SlpVM *vm, double value);
SlpValue slp_vm_taint_value(SlpVM *vm, SlpValue value);
SlpObjClosure *slp_vm_clone_closure(SlpVM *vm, SlpObjClosure *source);
SlpObjClosure *slp_vm_new_closure(
    SlpVM *vm, SlpObjFunction *function);
SlpObjString *slp_vm_stringify(SlpVM *vm, SlpValue value);
SlpValue slp_vm_hash_get(SlpVM *vm, SlpObjHash *hash, SlpValue key);
bool slp_vm_hash_contains(SlpVM *vm, SlpObjHash *hash, SlpValue key);
bool slp_vm_hash_set(
    SlpVM *vm, SlpObjHash *hash, SlpValue key, SlpValue value);
bool slp_vm_hash_delete(SlpVM *vm, SlpObjHash *hash, SlpValue key);
int slp_vm_regex_find(SlpVM *vm, const char *text, const char *pattern,
                      int start, bool full_match, int *match_end);
SlpObjArray *slp_vm_regex_matches(SlpVM *vm);
SlpObjArray *slp_vm_profile_statistics(
    SlpVM *vm);

void slp_vm_register_bridge_type(SlpVM *vm, const char *keyword,
    void (*handler)(SlpVM*, const char*, const char*, const char*,
                    SlpObjClosure*, void*),
    void *userdata);
void slp_vm_register_native(SlpVM *vm, const char *name, SlpNativeFn fn);
void slp_vm_register_native_raw(
    SlpVM *vm, const char *name, SlpNativeFn fn);

void slp_vm_flag_error(SlpVM *vm, SlpValue error);
SlpValue slp_vm_check_error(SlpVM *vm);

/* Sleep variable scopes. These are used by the standard-library declaration
   functions while the VM owns the lookup and lifetime rules. */
bool slp_vm_declare_local(SlpVM *vm, const char *declarations);
bool slp_vm_declare_closure(SlpVM *vm, const char *declarations);
bool slp_vm_declare_global(SlpVM *vm, const char *declarations);
bool slp_vm_push_local_scope(SlpVM *vm, SlpValue *initializers, int count);
bool slp_vm_pop_local_scope(SlpVM *vm, SlpValue *initializers, int count);
bool slp_vm_watch_variables(SlpVM *vm, const char *declarations);
bool slp_vm_assign_reference(
    SlpVM *vm, SlpValue reference, SlpValue value);

void slp_vm_set_error_fn(SlpVM *vm, SlpVMErrorFn fn, void *ud);
void slp_vm_set_write_fn(SlpVM *vm, SlpVMWriteFn fn, void *ud);
void slp_vm_set_source_name(SlpVM *vm, const char *source_name);
bool slp_vm_set_scoped_value(
    SlpVM *vm, const char *name, SlpValue value);
void slp_vm_write(SlpVM *vm, const char *text);
void slp_vm_warning(SlpVM *vm, const char *message);
void slp_vm_abort_warning(SlpVM *vm, const char *message);

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

double slp_value_as_number(SlpValue v);

#endif // SLP_VM_H
