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
    SLP_OBJ_LONG,
    SLP_OBJ_ARRAY,
    SLP_OBJ_HASH,
    SLP_OBJ_FUNCTION,
    SLP_OBJ_CLOSURE,
    SLP_OBJ_UPVALUE,
    SLP_OBJ_CONTINUATION,
    SLP_OBJ_NATIVE,
    SLP_OBJ_BRIDGE,
    SLP_OBJ_IO_HANDLE
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

typedef struct {
    SlpObj obj;
    int64_t value;
} SlpObjLong;

typedef struct SlpChunk SlpChunk;

typedef struct {
    SlpObj obj;
    int arity;
    int upvalue_count;
    SlpChunk *chunk;
    SlpObjString *name;
} SlpObjFunction;

typedef struct SlpObjUpvalue SlpObjUpvalue;

typedef struct {
    SlpObj obj;
    SlpObjFunction *function;
    SlpObjUpvalue **upvalues;
    
    // Coroutine state for yield/resume
    SlpValue *coroutine_stack;
    int coroutine_stack_count;
    uint8_t *coroutine_ip;
} SlpObjClosure;

struct SlpObjUpvalue {
    SlpObj obj;
    SlpValue *location;
    SlpValue closed;
    SlpObjUpvalue *next;
};

typedef struct {
    SlpObj obj;
    SlpValue *elements;
    int count;
    int capacity;
} SlpObjArray;

typedef struct {
    SlpValue key;
    SlpValue value;
} SlpHashEntry;

typedef struct {
    SlpObj obj;
    SlpHashEntry *entries;
    int capacity;
    int count;
} SlpObjHash;

typedef struct SlpCallFrame SlpCallFrame;

typedef struct {
    SlpObj obj;
    SlpCallFrame *frames;
    int frame_count;
    SlpValue *stack;
    int stack_count;
    uint8_t *saved_ip;
} SlpObjContinuation;

typedef struct SlpVM SlpVM;
typedef SlpValue (*SlpNativeFn)(SlpVM *vm, SlpValue *args, int argc);

typedef struct {
    SlpObj obj;
    SlpNativeFn fn;
    SlpObjString *name;
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
} SlpObjIOHandle;

#define SLP_OBJ_TYPE(v) (SLP_AS_OBJ(v)->type)

#define SLP_AS_STRING(v)    ((SlpObjString*)SLP_AS_OBJ(v))
#define SLP_AS_LONG(v)      ((SlpObjLong*)SLP_AS_OBJ(v))
#define SLP_AS_ARRAY(v)     ((SlpObjArray*)SLP_AS_OBJ(v))
#define SLP_AS_HASH(v)      ((SlpObjHash*)SLP_AS_OBJ(v))
#define SLP_AS_FUNCTION(v)  ((SlpObjFunction*)SLP_AS_OBJ(v))
#define SLP_AS_CLOSURE(v)   ((SlpObjClosure*)SLP_AS_OBJ(v))
#define SLP_AS_UPVALUE(v)   ((SlpObjUpvalue*)SLP_AS_OBJ(v))
#define SLP_AS_NATIVE(v)    ((SlpObjNative*)SLP_AS_OBJ(v))
#define SLP_AS_BRIDGE(v)    ((SlpObjBridge*)SLP_AS_OBJ(v))
#define SLP_AS_IO_HANDLE(v) ((SlpObjIOHandle*)SLP_AS_OBJ(v))
#define SLP_AS_CONTINUATION(v) ((SlpObjContinuation*)SLP_AS_OBJ(v))

bool slp_value_is_falsy(SlpValue v);
bool slp_value_equals(SlpValue a, SlpValue b);
SlpObjString *slp_obj_string_new(SlpAllocator *alloc, const char *chars, uint32_t length);
SlpObjString *slp_obj_string_copy(SlpAllocator *alloc, const char *chars, uint32_t length);
SlpObjLong *slp_obj_long_new(SlpAllocator *alloc, int64_t value);
SlpObjArray *slp_obj_array_new(SlpAllocator *alloc);
SlpObjHash *slp_obj_hash_new(SlpAllocator *alloc);
SlpObjFunction *slp_obj_function_new(SlpAllocator *alloc);
SlpObjClosure *slp_obj_closure_new(SlpAllocator *alloc, SlpObjFunction *fn);
SlpObjUpvalue *slp_obj_upvalue_new(SlpAllocator *alloc, SlpValue *slot);
SlpObjNative *slp_obj_native_new(SlpAllocator *alloc, SlpNativeFn fn, SlpObjString *name);
SlpObjBridge *slp_obj_bridge_new(SlpAllocator *alloc, SlpObjString *keyword, SlpObjString *name, SlpObjClosure *closure);
SlpObjIOHandle *slp_obj_io_handle_new(SlpAllocator *alloc);
SlpObjContinuation *slp_obj_continuation_new(SlpAllocator *alloc);

void slp_obj_array_push(SlpAllocator *alloc, SlpObjArray *arr, SlpValue val);
SlpValue slp_obj_array_pop(SlpObjArray *arr);
SlpValue slp_obj_array_get(SlpObjArray *arr, int index);
void slp_obj_array_set(SlpAllocator *alloc, SlpObjArray *arr, int index, SlpValue val);

bool slp_obj_hash_set(SlpAllocator *alloc, SlpObjHash *hash, SlpValue key, SlpValue value);
SlpValue slp_obj_hash_get(SlpObjHash *hash, SlpValue key);
bool slp_obj_hash_delete(SlpAllocator *alloc, SlpObjHash *hash, SlpValue key);

uint32_t slp_hash_string(const char *key, uint32_t length);
uint32_t slp_hash_value(SlpValue v);
SlpObjString *slp_find_interned_string(SlpObj *head, const char *chars, uint32_t length, uint32_t hash);

#endif // SLP_VALUE_H
