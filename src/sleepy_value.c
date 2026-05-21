#include "sleepy_value.h"
#include "sleepy_chunk.h"
#include "sleepy_utils.h"

bool sleepy_value_is_falsy(SleepyValue v) {
    if (SLEEPY_IS_NULL(v)) return true;
    if (SLEEPY_IS_BOOL(v) && !SLEEPY_AS_BOOL(v)) return true;
    if (SLEEPY_IS_NUM(v) && SLEEPY_AS_NUM(v) == 0.0) return true;
    return false;
}

bool sleepy_value_equals(SleepyValue a, SleepyValue b) {
#ifdef SLEEPY_NAN_TAGGING
    if (a == b) return true;
    if (SLEEPY_IS_NUM(a) && SLEEPY_IS_NUM(b))
        return SLEEPY_AS_NUM(a) == SLEEPY_AS_NUM(b);
    return false;
#else
    if (a.type != b.type) return false;
    switch (a.type) {
    case SLEEPY_VAL_NULL: return true;
    case SLEEPY_VAL_BOOL: return a.as.boolean == b.as.boolean;
    case SLEEPY_VAL_NUM: return a.as.num == b.as.num;
    case SLEEPY_VAL_OBJ: return a.as.obj == b.as.obj;
    }
    return false;
#endif
}

uint32_t sleepy_hash_string(const char *key, uint32_t length) {
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

uint32_t sleepy_hash_value(SleepyValue v) {
    if (SLEEPY_IS_NUM(v)) {
        double d = SLEEPY_AS_NUM(v);
        uint32_t h;
        sleepy_utils_memcpy(&h, &d, 4);
        return h;
    }
    if (SLEEPY_IS_OBJ(v)) {
        SleepyObj *obj = SLEEPY_AS_OBJ(v);
        if (obj->type == SLEEPY_OBJ_STRING) {
            return ((SleepyObjString*)obj)->hash;
        }
        uint32_t ptr;
        sleepy_utils_memcpy(&ptr, &obj, 4);
        return ptr;
    }
#ifdef SLEEPY_NAN_TAGGING
    return (uint32_t)(v & 0xFFFFFFFF);
#else
    return (uint32_t)v.type;
#endif
}

SleepyObjString *sleepy_find_interned_string(SleepyObj *head, const char *chars, uint32_t length, uint32_t hash) {
    SleepyObj *obj = head;
    while (obj != NULL) {
        if (obj->type == SLEEPY_OBJ_STRING) {
            SleepyObjString *s = (SleepyObjString*)obj;
            if (s->hash == hash && s->length == length) {
                bool match = true;
                for (uint32_t i = 0; i < length; i++) {
                    if (s->chars[i] != chars[i]) {
                        match = false;
                        break;
                    }
                }
                if (match) return s;
            }
        }
        obj = obj->next;
    }
    return NULL;
}

SleepyObjString *sleepy_obj_string_new(SleepyAllocator *alloc, const char *chars, uint32_t length) {
    size_t size = sizeof(SleepyObjString) + length + 1;
    SleepyObjString *str = (SleepyObjString*)SLEEPY_MALLOC(alloc, size);
    if (!str) return NULL;
    str->obj.type = SLEEPY_OBJ_STRING;
    str->obj.is_marked = false;
    str->obj.next = NULL;
    str->length = length;
    str->hash = sleepy_hash_string(chars, length);
    sleepy_utils_memcpy(str->chars, chars, length);
    str->chars[length] = '\0';
    return str;
}

SleepyObjString *sleepy_obj_string_copy(SleepyAllocator *alloc, const char *chars, uint32_t length) {
    return sleepy_obj_string_new(alloc, chars, length);
}

SleepyObjLong *sleepy_obj_long_new(SleepyAllocator *alloc, int64_t value) {
    SleepyObjLong *obj = SLEEPY_ALLOCATE(alloc, SleepyObjLong);
    if (!obj) return NULL;
    obj->obj.type = SLEEPY_OBJ_LONG;
    obj->obj.is_marked = false;
    obj->obj.next = NULL;
    obj->value = value;
    return obj;
}

SleepyObjArray *sleepy_obj_array_new(SleepyAllocator *alloc) {
    SleepyObjArray *arr = SLEEPY_ALLOCATE(alloc, SleepyObjArray);
    if (!arr) return NULL;
    arr->obj.type = SLEEPY_OBJ_ARRAY;
    arr->obj.is_marked = false;
    arr->obj.next = NULL;
    arr->elements = NULL;
    arr->count = 0;
    arr->capacity = 0;
    return arr;
}

void sleepy_obj_array_push(SleepyAllocator *alloc, SleepyObjArray *arr, SleepyValue val) {
    if (arr->count + 1 > arr->capacity) {
        int new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        arr->elements = (SleepyValue*)SLEEPY_REALLOC(alloc, arr->elements, sizeof(SleepyValue) * new_cap);
        arr->capacity = new_cap;
    }
    arr->elements[arr->count++] = val;
}

SleepyValue sleepy_obj_array_pop(SleepyObjArray *arr) {
    if (arr->count == 0) return SLEEPY_NULL_VAL;
    return arr->elements[--arr->count];
}

SleepyValue sleepy_obj_array_get(SleepyObjArray *arr, int index) {
    if (index < 0 || index >= arr->count) return SLEEPY_NULL_VAL;
    return arr->elements[index];
}

void sleepy_obj_array_set(SleepyAllocator *alloc, SleepyObjArray *arr, int index, SleepyValue val) {
    if (index < 0) return;
    while (index >= arr->capacity) {
        int new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        arr->elements = (SleepyValue*)SLEEPY_REALLOC(alloc, arr->elements, sizeof(SleepyValue) * new_cap);
        for (int i = arr->capacity; i < new_cap; i++)
            arr->elements[i] = SLEEPY_NULL_VAL;
        arr->capacity = new_cap;
    }
    arr->elements[index] = val;
    if (index >= arr->count) arr->count = index + 1;
}

static void hash_grow(SleepyAllocator *alloc, SleepyObjHash *hash) {
    int new_cap = hash->capacity == 0 ? 8 : hash->capacity * 2;
    SleepyHashEntry *new_entries = (SleepyHashEntry*)SLEEPY_MALLOC(alloc, sizeof(SleepyHashEntry) * new_cap);
    for (int i = 0; i < new_cap; i++) {
        new_entries[i].key = SLEEPY_NULL_VAL;
        new_entries[i].value = SLEEPY_NULL_VAL;
    }
    for (int i = 0; i < hash->capacity; i++) {
        if (SLEEPY_IS_NULL(hash->entries[i].key)) continue;
        uint32_t idx = sleepy_hash_value(hash->entries[i].key) & (new_cap - 1);
        while (!SLEEPY_IS_NULL(new_entries[idx].key)) {
            idx = (idx + 1) & (new_cap - 1);
        }
        new_entries[idx] = hash->entries[i];
    }
    if (hash->entries) SLEEPY_FREE(alloc, hash->entries);
    hash->entries = new_entries;
    hash->capacity = new_cap;
}

SleepyObjHash *sleepy_obj_hash_new(SleepyAllocator *alloc) {
    SleepyObjHash *h = SLEEPY_ALLOCATE(alloc, SleepyObjHash);
    if (!h) return NULL;
    h->obj.type = SLEEPY_OBJ_HASH;
    h->obj.is_marked = false;
    h->obj.next = NULL;
    h->entries = NULL;
    h->capacity = 0;
    h->count = 0;
    return h;
}

bool sleepy_obj_hash_set(SleepyAllocator *alloc, SleepyObjHash *hash, SleepyValue key, SleepyValue value) {
    if (hash->count + 1 > hash->capacity * 3 / 4)
        hash_grow(alloc, hash);
    if (hash->capacity == 0)
        hash_grow(alloc, hash);
    uint32_t idx = sleepy_hash_value(key) & (hash->capacity - 1);
    int tombstone = -1;
    for (;;) {
        if (SLEEPY_IS_NULL(hash->entries[idx].key)) {
            if (SLEEPY_IS_TRUE(hash->entries[idx].value)) {
                if (tombstone == -1) tombstone = idx;
            } else {
                if (tombstone != -1) idx = tombstone;
                hash->entries[idx].key = key;
                hash->entries[idx].value = value;
                hash->count++;
                return true;
            }
        } else if (sleepy_value_equals(hash->entries[idx].key, key)) {
            hash->entries[idx].value = value;
            return true;
        }
        idx = (idx + 1) & (hash->capacity - 1);
    }
}

SleepyValue sleepy_obj_hash_get(SleepyObjHash *hash, SleepyValue key) {
    if (hash->capacity == 0) return SLEEPY_NULL_VAL;
    uint32_t idx = sleepy_hash_value(key) & (hash->capacity - 1);
    for (int i = 0; i < hash->capacity; i++) {
        if (SLEEPY_IS_NULL(hash->entries[idx].key)) {
            if (SLEEPY_IS_TRUE(hash->entries[idx].value)) {
                // Tombstone
                idx = (idx + 1) & (hash->capacity - 1);
                continue;
            } else {
                return SLEEPY_NULL_VAL;
            }
        }
        if (sleepy_value_equals(hash->entries[idx].key, key))
            return hash->entries[idx].value;
        idx = (idx + 1) & (hash->capacity - 1);
    }
    return SLEEPY_NULL_VAL;
}

bool sleepy_obj_hash_delete(SleepyAllocator *alloc, SleepyObjHash *hash, SleepyValue key) {
    if (hash->capacity == 0) return false;
    uint32_t idx = sleepy_hash_value(key) & (hash->capacity - 1);
    for (int i = 0; i < hash->capacity; i++) {
        if (SLEEPY_IS_NULL(hash->entries[idx].key))
            return false;
        if (sleepy_value_equals(hash->entries[idx].key, key)) {
            hash->entries[idx].key = SLEEPY_NULL_VAL;
            hash->entries[idx].value = SLEEPY_TRUE_VAL;
            hash->count--;
            return true;
        }
        idx = (idx + 1) & (hash->capacity - 1);
    }
    return false;
    (void)alloc;
}

SleepyObjFunction *sleepy_obj_function_new(SleepyAllocator *alloc) {
    SleepyObjFunction *fn = SLEEPY_ALLOCATE(alloc, SleepyObjFunction);
    if (!fn) return NULL;
    fn->obj.type = SLEEPY_OBJ_FUNCTION;
    fn->obj.is_marked = false;
    fn->obj.next = NULL;
    fn->arity = 0;
    fn->upvalue_count = 0;
    fn->chunk = SLEEPY_ALLOCATE(alloc, SleepyChunk);
    if (fn->chunk) sleepy_chunk_init(fn->chunk, alloc);
    fn->name = NULL;
    return fn;
}

SleepyObjClosure *sleepy_obj_closure_new(SleepyAllocator *alloc, SleepyObjFunction *fn) {
    SleepyObjClosure *closure = SLEEPY_ALLOCATE(alloc, SleepyObjClosure);
    if (!closure) return NULL;
    closure->obj.type = SLEEPY_OBJ_CLOSURE;
    closure->obj.is_marked = false;
    closure->obj.next = NULL;
    closure->function = fn;
    closure->upvalues = NULL;
    if (fn->upvalue_count > 0) {
        closure->upvalues = (SleepyObjUpvalue**)SLEEPY_MALLOC(alloc, sizeof(SleepyObjUpvalue*) * fn->upvalue_count);
        for (int i = 0; i < fn->upvalue_count; i++)
            closure->upvalues[i] = NULL;
    }
    return closure;
}

SleepyObjUpvalue *sleepy_obj_upvalue_new(SleepyAllocator *alloc, SleepyValue *slot) {
    SleepyObjUpvalue *uv = SLEEPY_ALLOCATE(alloc, SleepyObjUpvalue);
    if (!uv) return NULL;
    uv->obj.type = SLEEPY_OBJ_UPVALUE;
    uv->obj.is_marked = false;
    uv->obj.next = NULL;
    uv->location = slot;
    uv->closed = SLEEPY_NULL_VAL;
    return uv;
}

SleepyObjNative *sleepy_obj_native_new(SleepyAllocator *alloc, SleepyNativeFn fn, SleepyObjString *name) {
    SleepyObjNative *native = SLEEPY_ALLOCATE(alloc, SleepyObjNative);
    if (!native) return NULL;
    native->obj.type = SLEEPY_OBJ_NATIVE;
    native->obj.is_marked = false;
    native->obj.next = NULL;
    native->fn = fn;
    native->name = name;
    return native;
}

SleepyObjBridge *sleepy_obj_bridge_new(SleepyAllocator *alloc, SleepyObjString *keyword, SleepyObjString *name, SleepyObjClosure *closure) {
    SleepyObjBridge *bridge = SLEEPY_ALLOCATE(alloc, SleepyObjBridge);
    if (!bridge) return NULL;
    bridge->obj.type = SLEEPY_OBJ_BRIDGE;
    bridge->obj.is_marked = false;
    bridge->obj.next = NULL;
    bridge->keyword = keyword;
    bridge->name = name;
    bridge->closure = closure;
    return bridge;
}

SleepyObjContinuation *sleepy_obj_continuation_new(SleepyAllocator *alloc) {
    SleepyObjContinuation *cont = SLEEPY_ALLOCATE(alloc, SleepyObjContinuation);
    if (!cont) return NULL;
    cont->obj.type = SLEEPY_OBJ_CONTINUATION;
    cont->obj.is_marked = false;
    cont->obj.next = NULL;
    cont->frames = NULL;
    cont->frame_count = 0;
    cont->stack = NULL;
    cont->stack_count = 0;
    cont->saved_ip = NULL;
    return cont;
}
