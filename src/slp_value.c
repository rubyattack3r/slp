#include "slp_value.h"
#include "slp_chunk.h"
#include "slp_utils.h"

SlpValue slp_value_unwrap_taint(SlpValue value) {
    while (SLP_IS_OBJ(value) &&
           SLP_OBJ_TYPE(value) ==
               SLP_OBJ_TAINTED)
        value =
            SLP_AS_TAINTED(value)->value;
    return value;
}

bool slp_value_is_tainted(SlpValue value) {
    while (SLP_IS_OBJ(value) &&
           SLP_OBJ_TYPE(value) ==
               SLP_OBJ_SCALAR_CELL)
        value =
            SLP_AS_SCALAR_CELL(value)->value;
    return SLP_IS_OBJ(value) &&
           SLP_OBJ_TYPE(value) ==
               SLP_OBJ_TAINTED;
}

bool slp_value_is_falsy(SlpValue v) {
    while (SLP_IS_OBJ(v) &&
           (SLP_OBJ_TYPE(v) ==
                SLP_OBJ_SCALAR_CELL ||
            SLP_OBJ_TYPE(v) ==
                SLP_OBJ_TAINTED))
        v = SLP_OBJ_TYPE(v) ==
                    SLP_OBJ_SCALAR_CELL
                ? SLP_AS_SCALAR_CELL(v)->value
                : SLP_AS_TAINTED(v)->value;
    if (SLP_IS_NULL(v)) return true;
    if (SLP_IS_BOOL(v) && !SLP_AS_BOOL(v)) return true;
    if (SLP_IS_NUM(v) && SLP_AS_NUM(v) == 0.0) return true;
    if (SLP_IS_OBJ(v) && SLP_OBJ_TYPE(v) == SLP_OBJ_LONG)
        return SLP_AS_LONG(v)->value == 0;
    if (SLP_IS_OBJ(v) && SLP_OBJ_TYPE(v) == SLP_OBJ_DOUBLE)
        return SLP_AS_DOUBLE(v)->value == 0.0;
    return false;
}

static bool is_numeric_value(SlpValue value) {
    value = slp_value_unwrap_taint(value);
    return SLP_IS_NUM(value) ||
           (SLP_IS_OBJ(value) &&
            (SLP_OBJ_TYPE(value) == SLP_OBJ_LONG ||
             SLP_OBJ_TYPE(value) == SLP_OBJ_DOUBLE));
}

static double numeric_double_value(SlpValue value) {
    value = slp_value_unwrap_taint(value);
    if (SLP_IS_NUM(value)) return SLP_AS_NUM(value);
    if (SLP_OBJ_TYPE(value) == SLP_OBJ_LONG)
        return (double)SLP_AS_LONG(value)->value;
    return SLP_AS_DOUBLE(value)->value;
}

bool slp_value_equals(SlpValue a, SlpValue b) {
    a = slp_value_unwrap_taint(a);
    b = slp_value_unwrap_taint(b);
    if (is_numeric_value(a) && is_numeric_value(b))
        return numeric_double_value(a) == numeric_double_value(b);
#ifdef SLP_NAN_TAGGING
    if (a == b) return true;
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

bool slp_value_identity_equals(SlpValue a, SlpValue b) {
    while (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_SCALAR_CELL)
        a = SLP_AS_SCALAR_CELL(a)->value;
    while (SLP_IS_OBJ(b) && SLP_OBJ_TYPE(b) == SLP_OBJ_SCALAR_CELL)
        b = SLP_AS_SCALAR_CELL(b)->value;
    a = slp_value_unwrap_taint(a);
    b = slp_value_unwrap_taint(b);
    if (SLP_IS_NUM(a) || SLP_IS_NUM(b))
        return SLP_IS_NUM(a) && SLP_IS_NUM(b) &&
               SLP_AS_NUM(a) == SLP_AS_NUM(b);
    if (SLP_IS_OBJ(a) || SLP_IS_OBJ(b)) {
        if (!SLP_IS_OBJ(a) || !SLP_IS_OBJ(b) ||
            SLP_OBJ_TYPE(a) != SLP_OBJ_TYPE(b))
            return false;
        switch (SLP_OBJ_TYPE(a)) {
        case SLP_OBJ_LONG:
            return SLP_AS_LONG(a)->value == SLP_AS_LONG(b)->value;
        case SLP_OBJ_DOUBLE:
            return SLP_AS_DOUBLE(a)->value == SLP_AS_DOUBLE(b)->value;
        case SLP_OBJ_STRING: {
            SlpObjString *left = SLP_AS_STRING(a);
            SlpObjString *right = SLP_AS_STRING(b);
            return left->length == right->length &&
                   memcmp(left->chars, right->chars, left->length) == 0;
        }
        default:
            return SLP_AS_OBJ(a) == SLP_AS_OBJ(b);
        }
    }
    return slp_value_equals(a, b);
}

uint32_t slp_hash_string(const char *key, uint32_t length) {
    /* Match the supplemental String-key hash used by the Java 7 HashMap
       backing Sleep 2.1's ordinary hashes. */
    uint32_t hash = 0;
    for (uint32_t i = 0; i < length; i++) {
        hash = hash * 31u + (uint8_t)key[i];
    }
    hash ^= (hash >> 20) ^ (hash >> 12);
    return hash ^ (hash >> 7) ^ (hash >> 4);
}

static bool hash_key_repr(SlpValue value, const char **chars,
                          uint32_t *length, char *buffer,
                          size_t buffer_size) {
    while (SLP_IS_OBJ(value) &&
           SLP_OBJ_TYPE(value) == SLP_OBJ_SCALAR_CELL)
        value = SLP_AS_SCALAR_CELL(value)->value;
    value = slp_value_unwrap_taint(value);
    if (SLP_IS_OBJ(value) && SLP_OBJ_TYPE(value) == SLP_OBJ_STRING) {
        SlpObjString *string = SLP_AS_STRING(value);
        *chars = string->chars;
        *length = string->length;
        return true;
    }

    int written = -1;
    if (SLP_IS_NUM(value)) {
        double number = SLP_AS_NUM(value);
        if (number >= (double)INT32_MIN && number <= (double)INT32_MAX &&
            number == (double)(int32_t)number)
            written = snprintf(buffer, buffer_size, "%d", (int32_t)number);
        else
            written = snprintf(buffer, buffer_size, "%.17g", number);
    } else if (SLP_IS_OBJ(value) &&
               SLP_OBJ_TYPE(value) == SLP_OBJ_LONG) {
        written = snprintf(buffer, buffer_size, "%lld",
                           (long long)SLP_AS_LONG(value)->value);
    } else if (SLP_IS_OBJ(value) &&
               SLP_OBJ_TYPE(value) == SLP_OBJ_DOUBLE) {
        double number = SLP_AS_DOUBLE(value)->value;
        written = snprintf(buffer, buffer_size, "%.17g", number);
        if (written > 0 && (size_t)written + 2 < buffer_size &&
            strchr(buffer, '.') == NULL && strchr(buffer, 'e') == NULL &&
            strchr(buffer, 'E') == NULL) {
            buffer[written++] = '.';
            buffer[written++] = '0';
            buffer[written] = '\0';
        }
    } else if (SLP_IS_BOOL(value)) {
        written = snprintf(buffer, buffer_size, "%s",
                           SLP_AS_BOOL(value) ? "1" : "");
    } else if (SLP_IS_NULL(value)) {
        written = snprintf(buffer, buffer_size, "%s", "");
    }
    if (written < 0 || (size_t)written >= buffer_size) return false;
    *chars = buffer;
    *length = (uint32_t)written;
    return true;
}

static bool hash_key_equals(SlpValue left, SlpValue right) {
    char left_buffer[64];
    char right_buffer[64];
    const char *left_chars;
    const char *right_chars;
    uint32_t left_length;
    uint32_t right_length;
    if (hash_key_repr(left, &left_chars, &left_length,
                      left_buffer, sizeof(left_buffer)) &&
        hash_key_repr(right, &right_chars, &right_length,
                      right_buffer, sizeof(right_buffer))) {
        return left_length == right_length &&
               memcmp(left_chars, right_chars, left_length) == 0;
    }
    return slp_value_equals(left, right);
}

uint32_t slp_hash_value(SlpValue v) {
    char buffer[64];
    const char *chars;
    uint32_t length;
    if (hash_key_repr(v, &chars, &length, buffer, sizeof(buffer)))
        return slp_hash_string(chars, length);
    if (SLP_IS_OBJ(v)) {
        SlpObj *obj = SLP_AS_OBJ(v);
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

SlpObjClass *slp_obj_class_new(SlpAllocator *alloc, SlpObjString *name,
                               bool is_interface) {
    SlpObjClass *obj = SLP_ALLOCATE(alloc, SlpObjClass);
    if (!obj) return NULL;
    obj->obj.type = SLP_OBJ_CLASS;
    obj->obj.is_marked = false;
    obj->obj.next = NULL;
    obj->name = name;
    obj->is_interface = is_interface;
    obj->interfaces = NULL;
    obj->fields = NULL;
    return obj;
}

SlpObjJavaObject *slp_obj_java_object_new(
    SlpAllocator *alloc, SlpObjClass *class_object,
    SlpJavaObjectKind kind) {
    SlpObjJavaObject *obj =
        SLP_ALLOCATE(alloc, SlpObjJavaObject);
    if (!obj) return NULL;
    obj->obj.type = SLP_OBJ_JAVA_OBJECT;
    obj->obj.is_marked = false;
    obj->obj.next = NULL;
    obj->class_object = class_object;
    obj->kind = kind;
    obj->list = NULL;
    obj->map = NULL;
    obj->fields = NULL;
    obj->value = SLP_NULL_VAL;
    return obj;
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

SlpObjDouble *slp_obj_double_new(SlpAllocator *alloc, double value) {
    SlpObjDouble *obj = SLP_ALLOCATE(alloc, SlpObjDouble);
    if (!obj) return NULL;
    obj->obj.type = SLP_OBJ_DOUBLE;
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
    arr->mutation_version = 0;
    arr->view_source = NULL;
    arr->view_offset = 0;
    arr->view_source_version = 0;
    arr->view_invalid = false;
    arr->read_only = false;
    return arr;
}

static void ensure_array_capacity(SlpAllocator *alloc, SlpObjArray *arr,
                                  int needed) {
    if (needed <= arr->capacity) return;
    int new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
    while (new_cap < needed) new_cap *= 2;
    arr->elements = (SlpValue*)SLP_REALLOC(
        alloc, arr->elements, sizeof(SlpValue) * new_cap);
    for (int i = arr->capacity; i < new_cap; i++)
        arr->elements[i] = SLP_NULL_VAL;
    arr->capacity = new_cap;
}

static void array_insert_internal(SlpAllocator *alloc, SlpObjArray *arr,
                                  int index, SlpValue val,
                                  bool from_child_view) {
    if (index < 0) index = 0;
    if (index > arr->count) index = arr->count;
    ensure_array_capacity(alloc, arr, arr->count + 1);
    for (int i = arr->count; i > index; i--)
        arr->elements[i] = arr->elements[i - 1];
    arr->elements[index] = val;
    arr->count++;
    arr->mutation_version++;
    if (from_child_view && arr->view_source)
        arr->view_invalid = true;
    if (arr->view_source) {
        array_insert_internal(
            alloc, arr->view_source, arr->view_offset + index,
            val, true);
        if (!from_child_view)
            arr->view_source_version =
                arr->view_source->mutation_version;
    }
}

void slp_obj_array_insert(SlpAllocator *alloc, SlpObjArray *arr, int index,
                          SlpValue val) {
    array_insert_internal(alloc, arr, index, val, false);
}

void slp_obj_array_push(SlpAllocator *alloc, SlpObjArray *arr, SlpValue val) {
    slp_obj_array_insert(alloc, arr, arr->count, val);
}

static SlpValue array_remove_internal(SlpObjArray *arr, int index,
                                      bool from_child_view) {
    if (index < 0 || index >= arr->count) return SLP_NULL_VAL;
    SlpValue result = arr->elements[index];
    for (int i = index + 1; i < arr->count; i++)
        arr->elements[i - 1] = arr->elements[i];
    arr->count--;
    arr->mutation_version++;
    if (from_child_view && arr->view_source)
        arr->view_invalid = true;
    if (arr->view_source) {
        array_remove_internal(
            arr->view_source, arr->view_offset + index, true);
        if (!from_child_view)
            arr->view_source_version =
                arr->view_source->mutation_version;
    }
    return result;
}

SlpValue slp_obj_array_remove_at(SlpObjArray *arr, int index) {
    return array_remove_internal(arr, index, false);
}

SlpValue slp_obj_array_pop(SlpObjArray *arr) {
    return slp_obj_array_remove_at(arr, arr->count - 1);
}

SlpValue slp_obj_array_get(SlpObjArray *arr, int index) {
    if (index < 0 || index >= arr->count) return SLP_NULL_VAL;
    SlpValue value = arr->elements[index];
    if (SLP_IS_OBJ(value) && SLP_OBJ_TYPE(value) == SLP_OBJ_SCALAR_CELL)
        return SLP_AS_SCALAR_CELL(value)->value;
    return value;
}

static void array_set_internal(SlpAllocator *alloc, SlpObjArray *arr,
                               int index, SlpValue val,
                               bool from_child_view) {
    if (index < 0) return;
    ensure_array_capacity(alloc, arr, index + 1);
    int old_count = arr->count;
    if (index < old_count && SLP_IS_OBJ(arr->elements[index]) &&
        SLP_OBJ_TYPE(arr->elements[index]) == SLP_OBJ_SCALAR_CELL) {
        SlpValue assigned = val;
        if (SLP_IS_OBJ(assigned) &&
            SLP_OBJ_TYPE(assigned) == SLP_OBJ_SCALAR_CELL)
            assigned = SLP_AS_SCALAR_CELL(assigned)->value;
        SLP_AS_SCALAR_CELL(arr->elements[index])->value = assigned;
    } else {
        arr->elements[index] = val;
    }
    if (index >= arr->count) arr->count = index + 1;
    arr->mutation_version++;
    if (from_child_view && arr->view_source)
        arr->view_invalid = true;
    if (arr->view_source) {
        for (int i = old_count; i < index; i++)
            array_insert_internal(
                alloc, arr->view_source, arr->view_offset + i,
                SLP_NULL_VAL, true);
        if (index < old_count)
            array_set_internal(
                alloc, arr->view_source, arr->view_offset + index,
                val, true);
        else
            array_insert_internal(
                alloc, arr->view_source, arr->view_offset + index,
                val, true);
        if (!from_child_view)
            arr->view_source_version =
                arr->view_source->mutation_version;
    }
}

void slp_obj_array_set(SlpAllocator *alloc, SlpObjArray *arr, int index,
                       SlpValue val) {
    array_set_internal(alloc, arr, index, val, false);
}

void slp_obj_array_sync_view(SlpAllocator *alloc, SlpObjArray *arr) {
    if (!arr->view_source) return;
    for (int i = 0; i < arr->count; i++)
        slp_obj_array_set(alloc, arr->view_source,
                          arr->view_offset + i, arr->elements[i]);
}

bool slp_obj_array_view_is_valid(SlpObjArray *arr) {
    return !arr || !arr->view_source ||
           (!arr->view_invalid &&
            arr->view_source_version ==
                arr->view_source->mutation_version);
}

static void hash_grow(SlpAllocator *alloc, SlpObjHash *hash) {
    int new_cap = hash->capacity == 0 ? 16 : hash->capacity * 2;
    SlpHashEntry *new_entries = (SlpHashEntry*)SLP_MALLOC(alloc, sizeof(SlpHashEntry) * new_cap);
    for (int i = 0; i < new_cap; i++) {
        new_entries[i].key = SLP_NULL_VAL;
        new_entries[i].value = SLP_NULL_VAL;
        new_entries[i].sequence = 0;
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
    h->next_sequence = 0;
    h->order_mode = 0;
    h->miss_policy = NULL;
    h->removal_policy = NULL;
    return h;
}

bool slp_obj_hash_set(SlpAllocator *alloc, SlpObjHash *hash, SlpValue key, SlpValue value) {
    if (hash->count + 1 > hash->capacity * 3 / 4)
        hash_grow(alloc, hash);
    if (hash->capacity == 0)
        hash_grow(alloc, hash);
    uint32_t idx = slp_hash_value(key) & (hash->capacity - 1);
    int tombstone = -1;
    for (int scanned = 0; scanned < hash->capacity; scanned++) {
        if (SLP_IS_NULL(hash->entries[idx].key)) {
            if (SLP_IS_TRUE(hash->entries[idx].value)) {
                if (tombstone == -1) tombstone = idx;
            } else {
                if (tombstone != -1) idx = tombstone;
                hash->entries[idx].key = key;
                hash->entries[idx].value = value;
                hash->entries[idx].sequence = ++hash->next_sequence;
                hash->count++;
                return true;
            }
        } else if (hash_key_equals(hash->entries[idx].key, key)) {
            hash->entries[idx].value = value;
            return true;
        }
        idx = (idx + 1) & (hash->capacity - 1);
    }

    /*
     * A deletion leaves a tombstone so probe chains remain valid. A table can
     * consequently have no never-used slot even though count is low. Reuse the
     * first tombstone after a complete probe instead of looping forever.
     */
    if (tombstone != -1) {
        hash->entries[tombstone].key = key;
        hash->entries[tombstone].value = value;
        hash->entries[tombstone].sequence = ++hash->next_sequence;
        hash->count++;
        return true;
    }

    /* Defensive fallback for a genuinely full table. */
    hash_grow(alloc, hash);
    return slp_obj_hash_set(alloc, hash, key, value);
}

SlpValue slp_obj_hash_get_raw(SlpObjHash *hash, SlpValue key) {
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
        if (hash_key_equals(hash->entries[idx].key, key)) {
            if (hash->order_mode == 2 &&
                !SLP_IS_NULL(hash->entries[idx].value))
                hash->entries[idx].sequence = ++hash->next_sequence;
            return hash->entries[idx].value;
        }
        idx = (idx + 1) & (hash->capacity - 1);
    }
    return SLP_NULL_VAL;
}

SlpValue slp_obj_hash_get(SlpObjHash *hash, SlpValue key) {
    SlpValue value = slp_obj_hash_get_raw(hash, key);
    while (SLP_IS_OBJ(value) && SLP_OBJ_TYPE(value) == SLP_OBJ_SCALAR_CELL)
        value = SLP_AS_SCALAR_CELL(value)->value;
    return value;
}

bool slp_obj_hash_contains(SlpObjHash *hash, SlpValue key) {
    if (hash->capacity == 0) return false;
    uint32_t idx = slp_hash_value(key) & (hash->capacity - 1);
    for (int i = 0; i < hash->capacity; i++) {
        if (SLP_IS_NULL(hash->entries[idx].key)) {
            if (!SLP_IS_TRUE(hash->entries[idx].value))
                return false;
        } else if (hash_key_equals(hash->entries[idx].key, key)) {
            return true;
        }
        idx = (idx + 1) & (hash->capacity - 1);
    }
    return false;
}

bool slp_obj_hash_delete(SlpAllocator *alloc, SlpObjHash *hash, SlpValue key) {
    if (hash->capacity == 0) return false;
    uint32_t idx = slp_hash_value(key) & (hash->capacity - 1);
    for (int i = 0; i < hash->capacity; i++) {
        if (SLP_IS_NULL(hash->entries[idx].key)) {
            if (SLP_IS_TRUE(hash->entries[idx].value)) {
                idx = (idx + 1) & (hash->capacity - 1);
                continue;
            }
            return false;
        }
        if (hash_key_equals(hash->entries[idx].key, key)) {
            hash->entries[idx].key = SLP_NULL_VAL;
            hash->entries[idx].value = SLP_TRUE_VAL;
            hash->entries[idx].sequence = 0;
            hash->count--;
            return true;
        }
        idx = (idx + 1) & (hash->capacity - 1);
    }
    return false;
    (void)alloc;
}

static bool hash_entry_precedes(SlpObjHash *hash, int left, int right) {
    if (hash->order_mode != 0)
        return hash->entries[left].sequence <
               hash->entries[right].sequence;

    uint32_t left_bucket =
        slp_hash_value(hash->entries[left].key) &
        (uint32_t)(hash->capacity - 1);
    uint32_t right_bucket =
        slp_hash_value(hash->entries[right].key) &
        (uint32_t)(hash->capacity - 1);
    if (left_bucket != right_bucket)
        return left_bucket < right_bucket;
    return hash->entries[left].sequence > hash->entries[right].sequence;
}

int slp_obj_hash_ordered_index(SlpObjHash *hash, int ordinal) {
    if (!hash || ordinal < 0 || ordinal >= hash->count)
        return -1;
    for (int candidate = 0; candidate < hash->capacity; candidate++) {
        if (SLP_IS_NULL(hash->entries[candidate].key)) continue;
        int rank = 0;
        for (int other = 0; other < hash->capacity; other++) {
            if (SLP_IS_NULL(hash->entries[other].key) ||
                other == candidate)
                continue;
            if (hash_entry_precedes(hash, other, candidate))
                rank++;
        }
        if (rank == ordinal)
            return candidate;
    }
    return -1;
}

int slp_obj_hash_visible_count(SlpObjHash *hash) {
    if (!hash) return 0;
    int count = 0;
    for (int i = 0; i < hash->capacity; i++) {
        if (!SLP_IS_NULL(hash->entries[i].key) &&
            !SLP_IS_NULL(hash->entries[i].value))
            count++;
    }
    return count;
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
    fn->source_name = NULL;
    fn->line_start = 0;
    fn->line_end = 0;
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
    closure->scope = NULL;
    closure->is_inline = false;
    closure->call_name = NULL;
    closure->identity = 0;
    closure->regex_states = NULL;
    closure->regex_state_count = 0;
    closure->regex_state_capacity = 0;
    closure->next_regex_sequence = 0;
    closure->last_regex_matches = NULL;
    if (fn->upvalue_count > 0) {
        closure->upvalues = (SlpObjUpvalue**)SLP_MALLOC(alloc, sizeof(SlpObjUpvalue*) * fn->upvalue_count);
        for (int i = 0; i < fn->upvalue_count; i++)
            closure->upvalues[i] = NULL;
    }
    closure->coroutine_stack = NULL;
    closure->coroutine_stack_count = 0;
    closure->coroutine_frames = NULL;
    closure->coroutine_frame_count = 0;
    closure->coroutine_needs_result = false;
    closure->coroutine_try_handlers = NULL;
    closure->coroutine_try_handler_count = 0;
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
    native->preserve_references = false;
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
    handle->is_memory = false;
    handle->is_console = false;
    handle->memory_readable = false;
    handle->memory_text_buffered = false;
    handle->text_encoding = 0;
    handle->token = SLP_NULL_VAL;
    handle->token_ready = false;
    handle->memory_mark = 0;
    handle->object_values = NULL;
    handle->object_count = 0;
    handle->object_capacity = 0;
    handle->object_read_index = 0;
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
    cont->coroutine_owner = NULL;
    cont->try_handlers = NULL;
    cont->try_handler_count = 0;
    cont->resume_frame_index = -1;
    cont->return_trace_call = NULL;
    cont->return_trace_source = NULL;
    cont->return_trace_line = -1;
    cont->return_trace_enabled = false;
    return cont;
}

SlpObjKeyValue *slp_obj_key_value_new(SlpAllocator *alloc, SlpValue key, SlpValue value) {
    SlpObjKeyValue *kv = SLP_ALLOCATE(alloc, SlpObjKeyValue);
    if (!kv) return NULL;
    kv->obj.type = SLP_OBJ_KEY_VALUE;
    kv->obj.is_marked = false;
    kv->obj.next = NULL;
    kv->key = key;
    kv->value = value;
    return kv;
}

SlpObjTainted *slp_obj_tainted_new(
    SlpAllocator *alloc, SlpValue value) {
    SlpObjTainted *tainted =
        SLP_ALLOCATE(alloc, SlpObjTainted);
    if (!tainted) return NULL;
    tainted->obj.type = SLP_OBJ_TAINTED;
    tainted->obj.is_marked = false;
    tainted->obj.next = NULL;
    tainted->value = value;
    return tainted;
}

SlpObjScalarCell *slp_obj_scalar_cell_new(SlpAllocator *alloc, SlpValue value) {
    SlpObjScalarCell *cell = SLP_ALLOCATE(alloc, SlpObjScalarCell);
    if (!cell) return NULL;
    cell->obj.type = SLP_OBJ_SCALAR_CELL;
    cell->obj.is_marked = false;
    cell->obj.next = NULL;
    cell->value = value;
    cell->watch_name = NULL;
    return cell;
}
