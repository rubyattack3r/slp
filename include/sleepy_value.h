#ifndef SLEEPY_VALUE_H
#define SLEEPY_VALUE_H

#include "sleepy_common.h"
#include "sleepy_core.h"
#include <stdint.h>

#ifdef SLEEPY_NAN_TAGGING

#define SLEEPY_SIGN_BIT    ((uint64_t)1 << 63)
#define SLEEPY_QNAN        ((uint64_t)0x7ffc000000000000)
#define SLEEPY_TAG_NULL    1
#define SLEEPY_TAG_FALSE   2
#define SLEEPY_TAG_TRUE    3
#define SLEEPY_TAG_NAN     4

#define SLEEPY_IS_NUM(v)   (((v) & SLEEPY_QNAN) != SLEEPY_QNAN)
#define SLEEPY_IS_OBJ(v)   (((v) & (SLEEPY_QNAN | SLEEPY_SIGN_BIT)) == (SLEEPY_QNAN | SLEEPY_SIGN_BIT))
#define SLEEPY_IS_BOOL(v)  (((v) | 3) == (SLEEPY_QNAN | 3))
#define SLEEPY_IS_NULL(v)  ((v) == SLEEPY_NULL_VAL)
#define SLEEPY_IS_TRUE(v)  ((v) == SLEEPY_TRUE_VAL)
#define SLEEPY_IS_FALSE(v) ((v) == SLEEPY_FALSE_VAL)

#define SLEEPY_AS_NUM(v)   sleepy_value_to_double(v)
#define SLEEPY_AS_OBJ(v)   ((SleepyObj*)(uintptr_t)((v) & ~(SLEEPY_QNAN | SLEEPY_SIGN_BIT)))
#define SLEEPY_AS_BOOL(v)  ((v) == SLEEPY_TRUE_VAL)

#define SLEEPY_NUM_VAL(n)  sleepy_double_to_value(n)
#define SLEEPY_OBJ_VAL(o)  (SLEEPY_QNAN | SLEEPY_SIGN_BIT | (uint64_t)(uintptr_t)(o))
#define SLEEPY_BOOL_VAL(b) ((b) ? SLEEPY_TRUE_VAL : SLEEPY_FALSE_VAL)

#define SLEEPY_NULL_VAL    (SLEEPY_QNAN | SLEEPY_TAG_NULL)
#define SLEEPY_TRUE_VAL    (SLEEPY_QNAN | SLEEPY_TAG_TRUE)
#define SLEEPY_FALSE_VAL   (SLEEPY_QNAN | SLEEPY_TAG_FALSE)

typedef uint64_t SleepyValue;

static inline double sleepy_value_to_double(SleepyValue v) {
    double d;
    sleepy_utils_memcpy(&d, &v, sizeof(double));
    return d;
}

static inline SleepyValue sleepy_double_to_value(double d) {
    SleepyValue v;
    sleepy_utils_memcpy(&v, &d, sizeof(SleepyValue));
    return v;
}

#else

typedef enum {
    SLEEPY_VAL_NULL,
    SLEEPY_VAL_BOOL,
    SLEEPY_VAL_NUM,
    SLEEPY_VAL_OBJ
} SleepyValueType;

typedef struct {
    SleepyValueType type;
    union {
        bool boolean;
        double num;
        struct SleepyObj *obj;
    } as;
} SleepyValue;

#define SLEEPY_IS_NUM(v)   ((v).type == SLEEPY_VAL_NUM)
#define SLEEPY_IS_OBJ(v)   ((v).type == SLEEPY_VAL_OBJ)
#define SLEEPY_IS_BOOL(v)  ((v).type == SLEEPY_VAL_BOOL)
#define SLEEPY_IS_NULL(v)  ((v).type == SLEEPY_VAL_NULL)
#define SLEEPY_IS_TRUE(v)  ((v).type == SLEEPY_VAL_BOOL && (v).as.boolean)
#define SLEEPY_IS_FALSE(v) ((v).type == SLEEPY_VAL_BOOL && !(v).as.boolean)

#define SLEEPY_AS_NUM(v)   ((v).as.num)
#define SLEEPY_AS_OBJ(v)   ((v).as.obj)
#define SLEEPY_AS_BOOL(v)  ((v).as.boolean)

#define SLEEPY_NUM_VAL(n)   ((SleepyValue){SLEEPY_VAL_NUM, {.num = (n)}})
#define SLEEPY_OBJ_VAL(o)   ((SleepyValue){SLEEPY_VAL_OBJ, {.obj = (struct SleepyObj*)(o)}})
#define SLEEPY_BOOL_VAL(b)  ((SleepyValue){SLEEPY_VAL_BOOL, {.boolean = (b)}})
#define SLEEPY_NULL_VAL     ((SleepyValue){SLEEPY_VAL_NULL, {.num = 0}})
#define SLEEPY_TRUE_VAL     SLEEPY_BOOL_VAL(true)
#define SLEEPY_FALSE_VAL    SLEEPY_BOOL_VAL(false)

#endif

typedef enum {
    SLEEPY_OBJ_STRING,
    SLEEPY_OBJ_LONG,
    SLEEPY_OBJ_ARRAY,
    SLEEPY_OBJ_HASH,
    SLEEPY_OBJ_FUNCTION,
    SLEEPY_OBJ_CLOSURE,
    SLEEPY_OBJ_UPVALUE,
    SLEEPY_OBJ_CONTINUATION,
    SLEEPY_OBJ_NATIVE,
    SLEEPY_OBJ_BRIDGE
} SleepyObjType;

typedef struct SleepyObj {
    SleepyObjType type;
    bool is_marked;
    struct SleepyObj *next;
} SleepyObj;

typedef struct {
    SleepyObj obj;
    uint32_t length;
    uint32_t hash;
    char chars[];
} SleepyObjString;

typedef struct {
    SleepyObj obj;
    int64_t value;
} SleepyObjLong;

typedef struct SleepyChunk SleepyChunk;

typedef struct {
    SleepyObj obj;
    int arity;
    int upvalue_count;
    SleepyChunk *chunk;
    SleepyObjString *name;
} SleepyObjFunction;

typedef struct SleepyObjUpvalue SleepyObjUpvalue;

typedef struct {
    SleepyObj obj;
    SleepyObjFunction *function;
    SleepyObjUpvalue **upvalues;
} SleepyObjClosure;

struct SleepyObjUpvalue {
    SleepyObj obj;
    SleepyValue *location;
    SleepyValue closed;
    SleepyObjUpvalue *next;
};

typedef struct {
    SleepyObj obj;
    SleepyValue *elements;
    int count;
    int capacity;
} SleepyObjArray;

typedef struct {
    SleepyValue key;
    SleepyValue value;
} SleepyHashEntry;

typedef struct {
    SleepyObj obj;
    SleepyHashEntry *entries;
    int capacity;
    int count;
} SleepyObjHash;

typedef struct SleepyCallFrame SleepyCallFrame;

typedef struct {
    SleepyObj obj;
    SleepyCallFrame *frames;
    int frame_count;
    SleepyValue *stack;
    int stack_count;
    uint8_t *saved_ip;
} SleepyObjContinuation;

typedef struct SleepyVM SleepyVM;
typedef SleepyValue (*SleepyNativeFn)(SleepyVM *vm, SleepyValue *args, int argc);

typedef struct {
    SleepyObj obj;
    SleepyNativeFn fn;
    SleepyObjString *name;
} SleepyObjNative;

typedef struct {
    SleepyObj obj;
    SleepyObjString *keyword;
    SleepyObjString *name;
    SleepyObjClosure *closure;
} SleepyObjBridge;

#define SLEEPY_OBJ_TYPE(v) (SLEEPY_AS_OBJ(v)->type)

#define SLEEPY_AS_STRING(v)    ((SleepyObjString*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_LONG(v)      ((SleepyObjLong*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_ARRAY(v)     ((SleepyObjArray*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_HASH(v)      ((SleepyObjHash*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_FUNCTION(v)  ((SleepyObjFunction*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_CLOSURE(v)   ((SleepyObjClosure*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_UPVALUE(v)   ((SleepyObjUpvalue*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_NATIVE(v)    ((SleepyObjNative*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_BRIDGE(v)    ((SleepyObjBridge*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_CONTINUATION(v) ((SleepyObjContinuation*)SLEEPY_AS_OBJ(v))

bool sleepy_value_is_falsy(SleepyValue v);
bool sleepy_value_equals(SleepyValue a, SleepyValue b);
SleepyObjString *sleepy_obj_string_new(SleepyAllocator *alloc, const char *chars, uint32_t length);
SleepyObjString *sleepy_obj_string_copy(SleepyAllocator *alloc, const char *chars, uint32_t length);
SleepyObjLong *sleepy_obj_long_new(SleepyAllocator *alloc, int64_t value);
SleepyObjArray *sleepy_obj_array_new(SleepyAllocator *alloc);
SleepyObjHash *sleepy_obj_hash_new(SleepyAllocator *alloc);
SleepyObjFunction *sleepy_obj_function_new(SleepyAllocator *alloc);
SleepyObjClosure *sleepy_obj_closure_new(SleepyAllocator *alloc, SleepyObjFunction *fn);
SleepyObjUpvalue *sleepy_obj_upvalue_new(SleepyAllocator *alloc, SleepyValue *slot);
SleepyObjNative *sleepy_obj_native_new(SleepyAllocator *alloc, SleepyNativeFn fn, SleepyObjString *name);
SleepyObjBridge *sleepy_obj_bridge_new(SleepyAllocator *alloc, SleepyObjString *keyword, SleepyObjString *name, SleepyObjClosure *closure);
SleepyObjContinuation *sleepy_obj_continuation_new(SleepyAllocator *alloc);

void sleepy_obj_array_push(SleepyAllocator *alloc, SleepyObjArray *arr, SleepyValue val);
SleepyValue sleepy_obj_array_pop(SleepyObjArray *arr);
SleepyValue sleepy_obj_array_get(SleepyObjArray *arr, int index);
void sleepy_obj_array_set(SleepyAllocator *alloc, SleepyObjArray *arr, int index, SleepyValue val);

bool sleepy_obj_hash_set(SleepyAllocator *alloc, SleepyObjHash *hash, SleepyValue key, SleepyValue value);
SleepyValue sleepy_obj_hash_get(SleepyObjHash *hash, SleepyValue key);
bool sleepy_obj_hash_delete(SleepyAllocator *alloc, SleepyObjHash *hash, SleepyValue key);

uint32_t sleepy_hash_string(const char *key, uint32_t length);
uint32_t sleepy_hash_value(SleepyValue v);
SleepyObjString *sleepy_find_interned_string(SleepyObj *head, const char *chars, uint32_t length, uint32_t hash);

#endif // SLEEPY_VALUE_H
