#ifndef SLP_VALUE_H
#define SLP_VALUE_H

#include "slp_common.h"
#include "slp_core.h"
#include <stdint.h>
#include <stdio.h>

#ifdef SLP_NAN_TAGGING

#define SLP_SIGN_BIT    ((uint64_t)1 << 63)
#define SLP_QNAN        ((uint64_t)0x7ffc000000000000)
#define SLP_TAG_NULL    1
#define SLP_TAG_FALSE   2
#define SLP_TAG_TRUE    3
#define SLP_TAG_NAN     4

#define SLP_IS_NUM(v)   (((v) & SLP_QNAN) != SLP_QNAN)
#define SLP_IS_OBJ(v)   (((v) & (SLP_QNAN | SLP_SIGN_BIT)) == (SLP_QNAN | SLP_SIGN_BIT))
#define SLP_IS_BOOL(v)  (((v) | 3) == (SLP_QNAN | 3))
#define SLP_IS_NULL(v)  ((v) == SLP_NULL_VAL)
#define SLP_IS_TRUE(v)  ((v) == SLP_TRUE_VAL)
#define SLP_IS_FALSE(v) ((v) == SLP_FALSE_VAL)

#define SLP_AS_NUM(v)   slp_value_to_double(v)
#define SLP_AS_OBJ(v)   ((SlpObj*)(uintptr_t)((v) & ~(SLP_QNAN | SLP_SIGN_BIT)))
#define SLP_AS_BOOL(v)  ((v) == SLP_TRUE_VAL)

#define SLP_NUM_VAL(n)  slp_double_to_value(n)
#define SLP_OBJ_VAL(o)  (SLP_QNAN | SLP_SIGN_BIT | (uint64_t)(uintptr_t)(o))
#define SLP_BOOL_VAL(b) ((b) ? SLP_TRUE_VAL : SLP_FALSE_VAL)

#define SLP_NULL_VAL    (SLP_QNAN | SLP_TAG_NULL)
#define SLP_TRUE_VAL    (SLP_QNAN | SLP_TAG_TRUE)
#define SLP_FALSE_VAL   (SLP_QNAN | SLP_TAG_FALSE)

typedef uint64_t SlpValue;

static inline double slp_value_to_double(SlpValue v) {
    double d;
    slp_utils_memcpy(&d, &v, sizeof(double));
    return d;
}

static inline SlpValue slp_double_to_value(double d) {
    SlpValue v;
    slp_utils_memcpy(&v, &d, sizeof(SlpValue));
    return v;
}

#else

typedef enum {
    SLP_VAL_NULL,
    SLP_VAL_BOOL,
    SLP_VAL_NUM,
    SLP_VAL_OBJ
} SlpValueType;

typedef struct {
    SlpValueType type;
    union {
        bool boolean;
        double num;
        struct SlpObj *obj;
    } as;
} SlpValue;

#define SLP_IS_NUM(v)   ((v).type == SLP_VAL_NUM)
#define SLP_IS_OBJ(v)   ((v).type == SLP_VAL_OBJ)
#define SLP_IS_BOOL(v)  ((v).type == SLP_VAL_BOOL)
#define SLP_IS_NULL(v)  ((v).type == SLP_VAL_NULL)
#define SLP_IS_TRUE(v)  ((v).type == SLP_VAL_BOOL && (v).as.boolean)
#define SLP_IS_FALSE(v) ((v).type == SLP_VAL_BOOL && !(v).as.boolean)

#define SLP_AS_NUM(v)   ((v).as.num)
#define SLP_AS_OBJ(v)   ((v).as.obj)
#define SLP_AS_BOOL(v)  ((v).as.boolean)

#define SLP_NUM_VAL(n)   ((SlpValue){SLP_VAL_NUM, {.num = (n)}})
#define SLP_OBJ_VAL(o)   ((SlpValue){SLP_VAL_OBJ, {.obj = (struct SlpObj*)(o)}})
#define SLP_BOOL_VAL(b)  ((SlpValue){SLP_VAL_BOOL, {.boolean = (b)}})
#define SLP_NULL_VAL     ((SlpValue){SLP_VAL_NULL, {.num = 0}})
#define SLP_TRUE_VAL     SLP_BOOL_VAL(true)
#define SLP_FALSE_VAL    SLP_BOOL_VAL(false)

#endif

typedef enum {
    SLP_OBJ_STRING,
    SLP_OBJ_CLASS,
    SLP_OBJ_JAVA_OBJECT,
    SLP_OBJ_LONG,
    SLP_OBJ_DOUBLE,
    SLP_OBJ_ARRAY,
    SLP_OBJ_HASH,
    SLP_OBJ_FUNCTION,
    SLP_OBJ_CLOSURE,
    SLP_OBJ_UPVALUE,
    SLP_OBJ_CONTINUATION,
    SLP_OBJ_NATIVE,
    SLP_OBJ_BRIDGE,
    SLP_OBJ_IO_HANDLE,
    SLP_OBJ_KEY_VALUE,
    SLP_OBJ_TAINTED,
    SLP_OBJ_SCALAR_CELL
} SlpObjType;

typedef struct SlpObj {
    SlpObjType type;
    bool is_marked;
    struct SlpObj *next;
} SlpObj;

typedef struct {
    SlpObj obj;
    uint32_t length;
    uint32_t hash;
    char chars[];
} SlpObjString;

typedef struct SlpObjArray SlpObjArray;
typedef struct SlpObjHash SlpObjHash;

/*
 * Sleep class literals are Java Class objects stored in ObjectValue scalars.
 * Keep them distinct from strings even in the portable runtime: typeOf(),
 * isa, identity, and their textual "class ..." representation all depend on
 * that distinction.
 */
typedef struct {
    SlpObj obj;
    SlpObjString *name;
    bool is_interface;
    SlpObjArray *interfaces;
    SlpObjHash *fields;
} SlpObjClass;

typedef enum {
    SLP_JAVA_GENERIC,
    SLP_JAVA_LIST,
    SLP_JAVA_MAP,
    SLP_JAVA_ITERATOR,
    SLP_JAVA_PROXY,
    SLP_JAVA_ERROR,
    SLP_JAVA_PROFILE_STATISTIC,
    SLP_JAVA_SEMAPHORE
} SlpJavaObjectKind;

typedef struct {
    SlpObj obj;
    SlpObjClass *class_object;
    SlpJavaObjectKind kind;
    SlpObjArray *list;
    SlpObjHash *map;
    SlpObjHash *fields;
    /*
     * Portable payload for Java wrappers and arrays. Java objects remain
     * distinct object scalars; scalar() explicitly unwraps this value.
     */
    SlpValue value;
} SlpObjJavaObject;

typedef struct {
    SlpObj obj;
    int64_t value;
} SlpObjLong;

typedef struct {
    SlpObj obj;
    double value;
} SlpObjDouble;

typedef struct SlpChunk SlpChunk;

typedef struct {
    SlpObj obj;
    int arity;
    int upvalue_count;
    SlpChunk *chunk;
    SlpObjString *name;
    SlpObjString *source_name;
    int line_start;
    int line_end;
} SlpObjFunction;

typedef struct SlpObjUpvalue SlpObjUpvalue;
typedef struct SlpCallFrame SlpCallFrame;
typedef struct SlpTryHandler SlpTryHandler;

typedef struct {
    SlpObjString *text;
    SlpObjString *pattern;
    int next_offset;
    uint64_t sequence;
} SlpRegexState;

typedef struct {
    SlpObj obj;
    SlpObjFunction *function;
    SlpObjUpvalue **upvalues;

    /* Sleep closures own a persistent "this" scope. Inline closures borrow
       the calling frame's local and closure scopes instead. */
    SlpObjHash *scope;
    bool is_inline;
    SlpObjString *call_name;
    uint64_t identity;

    /* hasmatch is a stateful global-style regex operation. Sleep scopes its
       bounded matcher cache to the currently executing closure. */
    SlpRegexState *regex_states;
    int regex_state_count;
    int regex_state_capacity;
    uint64_t next_regex_sequence;
    SlpObjArray *last_regex_matches;
    
    // Coroutine state for yield/resume
    SlpValue *coroutine_stack;
    int coroutine_stack_count;
    SlpCallFrame *coroutine_frames;
    int coroutine_frame_count;
    bool coroutine_needs_result;
    SlpTryHandler *coroutine_try_handlers;
    int coroutine_try_handler_count;
} SlpObjClosure;

struct SlpObjUpvalue {
    SlpObj obj;
    SlpValue *location;
    SlpValue closed;
    SlpObjUpvalue *next;
};

struct SlpObjArray {
    SlpObj obj;
    SlpValue *elements;
    int count;
    int capacity;
    uint64_t mutation_version;
    /* sublist() is a mutable view. The view keeps a local window for ordinary
       reads and mirrors mutations into its source array. */
    SlpObjArray *view_source;
    int view_offset;
    uint64_t view_source_version;
    bool view_invalid;
    bool read_only;
};

typedef struct {
    SlpValue key;
    SlpValue value;
    uint64_t sequence;
} SlpHashEntry;

struct SlpObjHash {
    SlpObj obj;
    SlpHashEntry *entries;
    int capacity;
    int count;
    uint64_t next_sequence;
    /* 0: Java-compatible hash order, 1: insertion order, 2: access order. */
    uint8_t order_mode;
    SlpObjClosure *miss_policy;
    SlpObjClosure *removal_policy;
};

typedef struct {
    SlpObj obj;
    SlpCallFrame *frames;
    int frame_count;
    SlpValue *stack;
    int stack_count;
    uint8_t *saved_ip;
    SlpObjClosure *coroutine_owner;
    SlpTryHandler *try_handlers;
    int try_handler_count;
    int resume_frame_index;
    /*
     * Calling a continuation temporarily transfers control back into its
     * captured owner.  Preserve the invocation trace here so it can be
     * emitted when that owner returns to the suspended caller.
     */
    SlpObjString *return_trace_call;
    SlpObjString *return_trace_source;
    int return_trace_line;
    bool return_trace_enabled;
} SlpObjContinuation;

typedef struct SlpVM SlpVM;
typedef SlpValue (*SlpNativeFn)(SlpVM *vm, SlpValue *args, int argc);

typedef struct {
    SlpObj obj;
    SlpNativeFn fn;
    SlpObjString *name;
    /*
     * Most native functions receive ordinary scalar values. A small number
     * of Sleep bridge functions, such as checkError($destination), need the
     * caller's scalar cell so they can update an output parameter.
     */
    bool preserve_references;
} SlpObjNative;

typedef struct {
    SlpObj obj;
    SlpObjString *keyword;
    SlpObjString *name;
    SlpObjClosure *closure;
} SlpObjBridge;

typedef struct {
    SlpObj obj;
    FILE *file;
    int socket_fd;
    int pid;
    bool is_socket;
    bool is_pipeline;
    bool is_eof;
    bool is_memory;
    bool is_console;
    bool memory_readable;
    bool memory_text_buffered;
    /* 0 = UTF-8, 1 = ISO-8859-1, 2 = US-ASCII. */
    uint8_t text_encoding;
    SlpValue token;
    bool token_ready;
    long memory_mark;
    SlpValue *object_values;
    int object_count;
    int object_capacity;
    int object_read_index;
} SlpObjIOHandle;

typedef struct {
    SlpObj obj;
    SlpValue key;
    SlpValue value;
} SlpObjKeyValue;

/*
 * Taint belongs to a Sleep scalar, not to the interned string or immediate
 * number it happens to contain. A transparent wrapper prevents equal-valued
 * scalars from accidentally sharing taint state.
 */
typedef struct {
    SlpObj obj;
    SlpValue value;
} SlpObjTainted;

typedef struct {
    SlpObj obj;
    SlpValue value;
    /* Non-NULL when watch('$name') should report subsequent assignments. */
    SlpObjString *watch_name;
} SlpObjScalarCell;

#define SLP_OBJ_TYPE(v) (SLP_AS_OBJ(v)->type)

#define SLP_AS_STRING(v)    ((SlpObjString*)SLP_AS_OBJ(v))
#define SLP_AS_CLASS(v)     ((SlpObjClass*)SLP_AS_OBJ(v))
#define SLP_AS_JAVA_OBJECT(v) ((SlpObjJavaObject*)SLP_AS_OBJ(v))
#define SLP_AS_LONG(v)      ((SlpObjLong*)SLP_AS_OBJ(v))
#define SLP_AS_DOUBLE(v)    ((SlpObjDouble*)SLP_AS_OBJ(v))
#define SLP_AS_ARRAY(v)     ((SlpObjArray*)SLP_AS_OBJ(v))
#define SLP_AS_HASH(v)      ((SlpObjHash*)SLP_AS_OBJ(v))
#define SLP_AS_FUNCTION(v)  ((SlpObjFunction*)SLP_AS_OBJ(v))
#define SLP_AS_CLOSURE(v)   ((SlpObjClosure*)SLP_AS_OBJ(v))
#define SLP_AS_UPVALUE(v)   ((SlpObjUpvalue*)SLP_AS_OBJ(v))
#define SLP_AS_NATIVE(v)    ((SlpObjNative*)SLP_AS_OBJ(v))
#define SLP_AS_BRIDGE(v)    ((SlpObjBridge*)SLP_AS_OBJ(v))
#define SLP_AS_IO_HANDLE(v) ((SlpObjIOHandle*)SLP_AS_OBJ(v))
#define SLP_AS_CONTINUATION(v) ((SlpObjContinuation*)SLP_AS_OBJ(v))
#define SLP_AS_KEY_VALUE(v) ((SlpObjKeyValue*)SLP_AS_OBJ(v))
#define SLP_AS_TAINTED(v)    ((SlpObjTainted*)SLP_AS_OBJ(v))
#define SLP_AS_SCALAR_CELL(v) ((SlpObjScalarCell*)SLP_AS_OBJ(v))

bool slp_value_is_falsy(SlpValue v);
bool slp_value_equals(SlpValue a, SlpValue b);
bool slp_value_identity_equals(SlpValue a, SlpValue b);
bool slp_value_is_tainted(SlpValue value);
SlpValue slp_value_unwrap_taint(SlpValue value);
SlpObjString *slp_obj_string_new(SlpAllocator *alloc, const char *chars, uint32_t length);
SlpObjString *slp_obj_string_copy(SlpAllocator *alloc, const char *chars, uint32_t length);
SlpObjClass *slp_obj_class_new(SlpAllocator *alloc, SlpObjString *name,
                               bool is_interface);
SlpObjJavaObject *slp_obj_java_object_new(
    SlpAllocator *alloc, SlpObjClass *class_object,
    SlpJavaObjectKind kind);
SlpObjLong *slp_obj_long_new(SlpAllocator *alloc, int64_t value);
SlpObjDouble *slp_obj_double_new(SlpAllocator *alloc, double value);
SlpObjArray *slp_obj_array_new(SlpAllocator *alloc);
SlpObjHash *slp_obj_hash_new(SlpAllocator *alloc);
SlpObjFunction *slp_obj_function_new(SlpAllocator *alloc);
SlpObjClosure *slp_obj_closure_new(SlpAllocator *alloc, SlpObjFunction *fn);
SlpObjUpvalue *slp_obj_upvalue_new(SlpAllocator *alloc, SlpValue *slot);
SlpObjNative *slp_obj_native_new(SlpAllocator *alloc, SlpNativeFn fn, SlpObjString *name);
SlpObjBridge *slp_obj_bridge_new(SlpAllocator *alloc, SlpObjString *keyword, SlpObjString *name, SlpObjClosure *closure);
SlpObjIOHandle *slp_obj_io_handle_new(SlpAllocator *alloc);
SlpObjContinuation *slp_obj_continuation_new(SlpAllocator *alloc);
SlpObjKeyValue *slp_obj_key_value_new(SlpAllocator *alloc, SlpValue key, SlpValue value);
SlpObjTainted *slp_obj_tainted_new(
    SlpAllocator *alloc, SlpValue value);
SlpObjScalarCell *slp_obj_scalar_cell_new(SlpAllocator *alloc, SlpValue value);

void slp_obj_array_push(SlpAllocator *alloc, SlpObjArray *arr, SlpValue val);
SlpValue slp_obj_array_pop(SlpObjArray *arr);
SlpValue slp_obj_array_get(SlpObjArray *arr, int index);
void slp_obj_array_set(SlpAllocator *alloc, SlpObjArray *arr, int index, SlpValue val);
void slp_obj_array_insert(SlpAllocator *alloc, SlpObjArray *arr, int index,
                          SlpValue val);
SlpValue slp_obj_array_remove_at(SlpObjArray *arr, int index);
void slp_obj_array_sync_view(SlpAllocator *alloc, SlpObjArray *arr);
bool slp_obj_array_view_is_valid(SlpObjArray *arr);

bool slp_obj_hash_set(SlpAllocator *alloc, SlpObjHash *hash, SlpValue key, SlpValue value);
/* Normal lookups expose a scalar cell's value, as Sleep variables do.
   VM binding/reference machinery uses the raw variant to preserve identity. */
SlpValue slp_obj_hash_get(SlpObjHash *hash, SlpValue key);
SlpValue slp_obj_hash_get_raw(SlpObjHash *hash, SlpValue key);
bool slp_obj_hash_contains(SlpObjHash *hash, SlpValue key);
bool slp_obj_hash_delete(SlpAllocator *alloc, SlpObjHash *hash, SlpValue key);
int slp_obj_hash_ordered_index(SlpObjHash *hash, int ordinal);
int slp_obj_hash_visible_count(SlpObjHash *hash);

uint32_t slp_hash_string(const char *key, uint32_t length);
uint32_t slp_hash_value(SlpValue v);
SlpObjString *slp_find_interned_string(SlpObj *head, const char *chars, uint32_t length, uint32_t hash);

#endif // SLP_VALUE_H
