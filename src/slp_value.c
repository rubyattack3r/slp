#include "slp_value.h"
#include "slp_chunk.h"
#include "slp_utils.h"

bool slp_value_is_falsy(SlpValue v) {
    if (SLP_IS_NULL(v)) return true;
    if (SLP_IS_BOOL(v) && !SLP_AS_BOOL(v)) return true;
    if (SLP_IS_NUM(v) && SLP_AS_NUM(v) == 0.0) return true;
    return false;
}

bool slp_value_equals(SlpValue a, SlpValue b) {
#ifdef SLP_NAN_TAGGING
    if (a == b) return true;
    if (SLP_IS_NUM(a) && SLP_IS_NUM(b))
        return SLP_AS_NUM(a) == SLP_AS_NUM(b);
    return false;
#else
    if (a.type != b.type) return false;
    switch (a.type) {
    case SLP_VAL_NULL: return true;
    case SLP_VAL_BOOL: return a.as.boolean == b.as.boolean;
    case SLP_VAL_NUM: return a.as.num == b.as.num;
    case SLP_VAL_OBJ: return a.as.obj == b.as.obj;
    }
    return false;
#endif
}

uint32_t slp_hash_string(const char *key, uint32_t length) {
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

uint32_t slp_hash_value(SlpValue v) {
    if (SLP_IS_NUM(v)) {
        double d = SLP_AS_NUM(v);
        uint32_t h;
        slp_utils_memcpy(&h, &d, 4);
        return h;
    }
    if (SLP_IS_OBJ(v)) {
        SlpObj *obj = SLP_AS_OBJ(v);
        if (obj->type == SLP_OBJ_STRING) {
            return ((SlpObjString*)obj)->hash;
        }
        uint32_t ptr;
        slp_utils_memcpy(&ptr, &obj, 4);
        return ptr;
    }
#ifdef SLP_NAN_TAGGING
    return (uint32_t)(v & 0xFFFFFFFF);
#else
    return (uint32_t)v.type;
#endif
}

SlpObjString *slp_find_interned_string(SlpObj *head, const char *chars, uint32_t length, uint32_t hash) {
    SlpObj *obj = head;
    while (obj != NULL) {
        if (obj->type == SLP_OBJ_STRING) {
            SlpObjString *s = (SlpObjString*)obj;
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

SlpObjString *slp_obj_string_new(SlpAllocator *alloc, const char *chars, uint32_t length) {
    size_t size = sizeof(SlpObjString) + length + 1;
    SlpObjString *str = (SlpObjString*)SLP_MALLOC(alloc, size);
    if (!str) return NULL;
    str->obj.type = SLP_OBJ_STRING;
    str->obj.is_marked = false;
    str->obj.next = NULL;
    str->length = length;
    str->hash = slp_hash_string(chars, length);
    slp_utils_memcpy(str->chars, chars, length);
    str->chars[length] = '\0';
    return str;
}

SlpObjString *slp_obj_string_copy(SlpAllocator *alloc, const char *chars, uint32_t length) {
    return slp_obj_string_new(alloc, chars, length);
}

SlpObjLong *slp_obj_long_new(SlpAllocator *alloc, int64_t value) {
    SlpObjLong *obj = SLP_ALLOCATE(alloc, SlpObjLong);
    if (!obj) return NULL;
    obj->obj.type = SLP_OBJ_LONG;
    obj->obj.is_marked = false;
    obj->obj.next = NULL;
    obj->value = value;
    return obj;
}

SlpObjArray *slp_obj_array_new(SlpAllocator *alloc) {
    SlpObjArray *arr = SLP_ALLOCATE(alloc, SlpObjArray);
    if (!arr) return NULL;
    arr->obj.type = SLP_OBJ_ARRAY;
    arr->obj.is_marked = false;
    arr->obj.next = NULL;
    arr->elements = NULL;
    arr->count = 0;
    arr->capacity = 0;
    return arr;
}

void slp_obj_array_push(SlpAllocator *alloc, SlpObjArray *arr, SlpValue val) {
    if (arr->count + 1 > arr->capacity) {
        int new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        arr->elements = (SlpValue*)SLP_REALLOC(alloc, arr->elements, sizeof(SlpValue) * new_cap);
        arr->capacity = new_cap;
    }
    arr->elements[arr->count++] = val;
}

SlpValue slp_obj_array_pop(SlpObjArray *arr) {
    if (arr->count == 0) return SLP_NULL_VAL;
    return arr->elements[--arr->count];
}

SlpValue slp_obj_array_get(SlpObjArray *arr, int index) {
    if (index < 0 || index >= arr->count) return SLP_NULL_VAL;
    return arr->elements[index];
}

void slp_obj_array_set(SlpAllocator *alloc, SlpObjArray *arr, int index, SlpValue val) {
    if (index < 0) return;
    while (index >= arr->capacity) {
        int new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        arr->elements = (SlpValue*)SLP_REALLOC(alloc, arr->elements, sizeof(SlpValue) * new_cap);
        for (int i = arr->capacity; i < new_cap; i++)
            arr->elements[i] = SLP_NULL_VAL;
        arr->capacity = new_cap;
    }
    arr->elements[index] = val;
    if (index >= arr->count) arr->count = index + 1;
}

static void hash_grow(SlpAllocator *alloc, SlpObjHash *hash) {
    int new_cap = hash->capacity == 0 ? 8 : hash->capacity * 2;
    SlpHashEntry *new_entries = (SlpHashEntry*)SLP_MALLOC(alloc, sizeof(SlpHashEntry) * new_cap);
    for (int i = 0; i < new_cap; i++) {
        new_entries[i].key = SLP_NULL_VAL;
        new_entries[i].value = SLP_NULL_VAL;
    }
    for (int i = 0; i < hash->capacity; i++) {
        if (SLP_IS_NULL(hash->entries[i].key)) continue;
        uint32_t idx = slp_hash_value(hash->entries[i].key) & (new_cap - 1);
        while (!SLP_IS_NULL(new_entries[idx].key)) {
            idx = (idx + 1) & (new_cap - 1);
        }
        new_entries[idx] = hash->entries[i];
    }
    if (hash->entries) SLP_FREE(alloc, hash->entries);
    hash->entries = new_entries;
    hash->capacity = new_cap;
}

SlpObjHash *slp_obj_hash_new(SlpAllocator *alloc) {
    SlpObjHash *h = SLP_ALLOCATE(alloc, SlpObjHash);
    if (!h) return NULL;
    h->obj.type = SLP_OBJ_HASH;
    h->obj.is_marked = false;
    h->obj.next = NULL;
    h->entries = NULL;
    h->capacity = 0;
    h->count = 0;
    return h;
}

bool slp_obj_hash_set(SlpAllocator *alloc, SlpObjHash *hash, SlpValue key, SlpValue value) {
    if (hash->count + 1 > hash->capacity * 3 / 4)
        hash_grow(alloc, hash);
    if (hash->capacity == 0)
        hash_grow(alloc, hash);
    uint32_t idx = slp_hash_value(key) & (hash->capacity - 1);
    int tombstone = -1;
    for (;;) {
        if (SLP_IS_NULL(hash->entries[idx].key)) {
            if (SLP_IS_TRUE(hash->entries[idx].value)) {
                if (tombstone == -1) tombstone = idx;
            } else {
                if (tombstone != -1) idx = tombstone;
                hash->entries[idx].key = key;
                hash->entries[idx].value = value;
                hash->count++;
                return true;
            }
        } else if (slp_value_equals(hash->entries[idx].key, key)) {
            hash->entries[idx].value = value;
            return true;
        }
        idx = (idx + 1) & (hash->capacity - 1);
    }
}

SlpValue slp_obj_hash_get(SlpObjHash *hash, SlpValue key) {
    if (hash->capacity == 0) return SLP_NULL_VAL;
    uint32_t idx = slp_hash_value(key) & (hash->capacity - 1);
    for (int i = 0; i < hash->capacity; i++) {
        if (SLP_IS_NULL(hash->entries[idx].key)) {
            if (SLP_IS_TRUE(hash->entries[idx].value)) {
                // Tombstone
                idx = (idx + 1) & (hash->capacity - 1);
                continue;
            } else {
                return SLP_NULL_VAL;
            }
        }
        if (slp_value_equals(hash->entries[idx].key, key))
            return hash->entries[idx].value;
        idx = (idx + 1) & (hash->capacity - 1);
    }
    return SLP_NULL_VAL;
}

bool slp_obj_hash_delete(SlpAllocator *alloc, SlpObjHash *hash, SlpValue key) {
    if (hash->capacity == 0) return false;
    uint32_t idx = slp_hash_value(key) & (hash->capacity - 1);
    for (int i = 0; i < hash->capacity; i++) {
        if (SLP_IS_NULL(hash->entries[idx].key))
            return false;
        if (slp_value_equals(hash->entries[idx].key, key)) {
            hash->entries[idx].key = SLP_NULL_VAL;
            hash->entries[idx].value = SLP_TRUE_VAL;
            hash->count--;
            return true;
        }
        idx = (idx + 1) & (hash->capacity - 1);
    }
    return false;
    (void)alloc;
}

SlpObjFunction *slp_obj_function_new(SlpAllocator *alloc) {
    SlpObjFunction *fn = SLP_ALLOCATE(alloc, SlpObjFunction);
    if (!fn) return NULL;
    fn->obj.type = SLP_OBJ_FUNCTION;
    fn->obj.is_marked = false;
    fn->obj.next = NULL;
    fn->arity = 0;
    fn->upvalue_count = 0;
    fn->chunk = SLP_ALLOCATE(alloc, SlpChunk);
    if (fn->chunk) slp_chunk_init(fn->chunk, alloc);
    fn->name = NULL;
    return fn;
}

SlpObjClosure *slp_obj_closure_new(SlpAllocator *alloc, SlpObjFunction *fn) {
    SlpObjClosure *closure = SLP_ALLOCATE(alloc, SlpObjClosure);
    if (!closure) return NULL;
    closure->obj.type = SLP_OBJ_CLOSURE;
    closure->obj.is_marked = false;
    closure->obj.next = NULL;
    closure->function = fn;
    closure->upvalues = NULL;
    if (fn->upvalue_count > 0) {
        closure->upvalues = (SlpObjUpvalue**)SLP_MALLOC(alloc, sizeof(SlpObjUpvalue*) * fn->upvalue_count);
        for (int i = 0; i < fn->upvalue_count; i++)
            closure->upvalues[i] = NULL;
    }
    closure->coroutine_stack = NULL;
    closure->coroutine_stack_count = 0;
    closure->coroutine_ip = NULL;
    return closure;
}

SlpObjUpvalue *slp_obj_upvalue_new(SlpAllocator *alloc, SlpValue *slot) {
    SlpObjUpvalue *uv = SLP_ALLOCATE(alloc, SlpObjUpvalue);
    if (!uv) return NULL;
    uv->obj.type = SLP_OBJ_UPVALUE;
    uv->obj.is_marked = false;
    uv->obj.next = NULL;
    uv->location = slot;
    uv->closed = SLP_NULL_VAL;
    return uv;
}

SlpObjNative *slp_obj_native_new(SlpAllocator *alloc, SlpNativeFn fn, SlpObjString *name) {
    SlpObjNative *native = SLP_ALLOCATE(alloc, SlpObjNative);
    if (!native) return NULL;
    native->obj.type = SLP_OBJ_NATIVE;
    native->obj.is_marked = false;
    native->obj.next = NULL;
    native->fn = fn;
    native->name = name;
    return native;
}

SlpObjBridge *slp_obj_bridge_new(SlpAllocator *alloc, SlpObjString *keyword, SlpObjString *name, SlpObjClosure *closure) {
    SlpObjBridge *bridge = SLP_ALLOCATE(alloc, SlpObjBridge);
    if (!bridge) return NULL;
    bridge->obj.type = SLP_OBJ_BRIDGE;
    bridge->obj.is_marked = false;
    bridge->obj.next = NULL;
    bridge->keyword = keyword;
    bridge->name = name;
    bridge->closure = closure;
    return bridge;
}

SlpObjIOHandle *slp_obj_io_handle_new(SlpAllocator *alloc) {
    SlpObjIOHandle *handle = SLP_ALLOCATE(alloc, SlpObjIOHandle);
    if (!handle) return NULL;
    handle->obj.type = SLP_OBJ_IO_HANDLE;
    handle->obj.is_marked = false;
    handle->obj.next = NULL;
    handle->file = NULL;
    handle->socket_fd = -1;
    handle->pid = -1;
    handle->is_socket = false;
    handle->is_pipeline = false;
    handle->is_eof = false;
    return handle;
}

SlpObjContinuation *slp_obj_continuation_new(SlpAllocator *alloc) {
    SlpObjContinuation *cont = SLP_ALLOCATE(alloc, SlpObjContinuation);
    if (!cont) return NULL;
    cont->obj.type = SLP_OBJ_CONTINUATION;
    cont->obj.is_marked = false;
    cont->obj.next = NULL;
    cont->frames = NULL;
    cont->frame_count = 0;
    cont->stack = NULL;
    cont->stack_count = 0;
    cont->saved_ip = NULL;
    return cont;
}
