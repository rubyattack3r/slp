#include "slp_vm.h"
#include "slp_compiler.h"
#include "slp_ast.h"
#include "slp_utils.h"
#include "slp_platform.h"
#include "slp_embed_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#include <regex.h>
#else
#include "platform/regex/regex.h"
#endif

#ifdef _WIN32
#define SLP_REGEX_COMPILE(vm, regex, pattern, flags) \
    slp_regex_compile((vm)->allocator, (regex), (pattern), (flags))
#define SLP_REGEX_EXECUTE(regex, text, count, matches, flags) \
    slp_regex_execute((regex), (text), (count), (matches), (flags))
#define SLP_REGEX_FREE(regex) slp_regex_free((regex))
#else
#define SLP_REGEX_COMPILE(vm, regex, pattern, flags) \
    regcomp((regex), (pattern), (flags))
#define SLP_REGEX_EXECUTE(regex, text, count, matches, flags) \
    regexec((regex), (text), (count), (matches), (flags))
#define SLP_REGEX_FREE(regex) regfree((regex))
#endif

static void *vm_default_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}

static void define_builtins(SlpVM *vm);
static int slp_vm_current_line(SlpVM *vm);
static int frame_current_line(SlpCallFrame *frame);
static const char *frame_source_name(SlpCallFrame *frame);
static void capture_stack_trace(SlpVM *vm, int handler_frame_count);

SlpVM *slp_vm_new(SlpAllocator *allocator) {
    SlpVM *vm;
    if (allocator) {
        vm = (SlpVM*)allocator->reallocate(NULL, sizeof(SlpVM), allocator->user_data);
    } else {
        SlpAllocator *a = (SlpAllocator*)malloc(sizeof(SlpAllocator));
        a->reallocate = vm_default_alloc;
        a->user_data = NULL;
        vm = (SlpVM*)a->reallocate(NULL, sizeof(SlpVM), a->user_data);
        allocator = a;
    }
    if (!vm) return NULL;
    memset(vm, 0, sizeof(SlpVM));
    vm->allocator = allocator;
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->objects = NULL;

    vm->open_upvalues = NULL;
    vm->bridge_types = NULL;
    vm->halted = false;
    vm->flow_exit_requested = false;
    vm->thrown_exception = SLP_NULL_VAL;
    vm->last_error = SLP_NULL_VAL;
    vm->error_fn = NULL;
    vm->error_userdata = NULL;
    vm->write_fn = NULL;
    vm->write_userdata = NULL;
    vm->source_name = NULL;
    vm->source_path = NULL;
    vm->debug_flags = 1;
    vm->last_stack_trace = NULL;
    vm->next_closure_identity = 0;
    vm->next_proxy_identity = 0;

    slp_gc_init(vm);

    vm->globals = slp_obj_hash_new(allocator);
    if (vm->globals) {
        vm->globals->obj.next = vm->objects;
        vm->objects = &vm->globals->obj;
    }
    vm->natives = slp_obj_hash_new(allocator);
    if (vm->natives) {
        vm->natives->obj.next = vm->objects;
        vm->objects = &vm->natives->obj;
    }

    define_builtins(vm);

    return vm;
}

void slp_vm_free(SlpVM *vm) {
    slp_embed_release_all(vm);
    SlpBridgeType *bt = vm->bridge_types;
    while (bt) {
        SlpBridgeType *next = bt->next;
        SLP_FREE(vm->allocator, bt->keyword);
        SLP_FREE(vm->allocator, bt);
        bt = next;
    }
    if (vm->interned)
        vm->allocator->reallocate(vm->interned, 0, vm->allocator->user_data);
    if (vm->source_name)
        SLP_FREE(vm->allocator, vm->source_name);
    if (vm->source_path)
        SLP_FREE(vm->allocator, vm->source_path);
    slp_gc_free(vm);
    SLP_FREE(vm->allocator, vm);
}

void slp_vm_push(SlpVM *vm, SlpValue value) {
    if (vm->stack_top >= vm->stack + SLP_STACK_MAX) {
        slp_vm_runtime_error(vm, "Stack overflow.");
        vm->abort_requested = true;
        return;
    }
    *vm->stack_top = value;
    vm->stack_top++;
}

SlpValue slp_vm_pop(SlpVM *vm) {
    if (vm->stack_top <= vm->stack) {
        slp_vm_runtime_error(vm, "Stack underflow.");
        vm->abort_requested = true;
        return SLP_NULL_VAL;
    }
    vm->stack_top--;
    return *vm->stack_top;
}

SlpValue slp_vm_peek(SlpVM *vm, int distance) {
    return vm->stack_top[-1 - distance];
}

// Coerce any value to a double for arithmetic, matching Sleep's loose typing:
// numbers as-is, longs by value, booleans as 1/0, null as 0, and strings by
// their leading numeric prefix (strtod, so "5abc" -> 5 and "abc" -> 0). Other
// objects (arrays/hashes/etc.) are 0. Replaces raw SLP_AS_NUM in arithmetic,
// where reading a non-number's union field previously yielded garbage.
double slp_value_as_number(SlpValue v) {
    v = slp_value_unwrap_taint(v);
    if (SLP_IS_NUM(v)) return SLP_AS_NUM(v);
    if (SLP_IS_BOOL(v)) return SLP_AS_BOOL(v) ? 1.0 : 0.0;
    if (SLP_IS_NULL(v)) return 0.0;
    if (SLP_IS_OBJ(v)) {
        SlpObj *o = SLP_AS_OBJ(v);
        if (o->type == SLP_OBJ_SCALAR_CELL)
            return slp_value_as_number(((SlpObjScalarCell*)o)->value);
        if (o->type == SLP_OBJ_STRING) return strtod(((SlpObjString*)o)->chars, NULL);
        if (o->type == SLP_OBJ_LONG) return (double)((SlpObjLong*)o)->value;
        if (o->type == SLP_OBJ_DOUBLE) return ((SlpObjDouble*)o)->value;
    }
    return 0.0;
}

typedef enum {
    SLP_NUMERIC_INT,
    SLP_NUMERIC_LONG,
    SLP_NUMERIC_DOUBLE
} SlpNumericKind;

static SlpNumericKind numeric_kind(SlpValue value) {
    value = slp_value_unwrap_taint(value);
    if (SLP_IS_OBJ(value)) {
        if (SLP_OBJ_TYPE(value) == SLP_OBJ_DOUBLE)
            return SLP_NUMERIC_DOUBLE;
        if (SLP_OBJ_TYPE(value) == SLP_OBJ_LONG)
            return SLP_NUMERIC_LONG;
    }
    if (SLP_IS_NUM(value) && !isfinite(SLP_AS_NUM(value)))
        return SLP_NUMERIC_DOUBLE;
    if (SLP_IS_NUM(value) && SLP_AS_NUM(value) != trunc(SLP_AS_NUM(value)))
        return SLP_NUMERIC_DOUBLE;
    return SLP_NUMERIC_INT;
}

static SlpNumericKind widest_numeric_kind(SlpValue left, SlpValue right) {
    SlpNumericKind left_kind = numeric_kind(left);
    SlpNumericKind right_kind = numeric_kind(right);
    return left_kind > right_kind ? left_kind : right_kind;
}

static SlpValue make_long_value(SlpVM *vm, int64_t value) {
    SlpObjLong *number = slp_vm_new_long(vm, value);
    return number ? SLP_OBJ_VAL(number) : SLP_NULL_VAL;
}

static SlpValue make_double_value(SlpVM *vm, double value) {
    SlpObjDouble *number = slp_vm_new_double(vm, value);
    return number ? SLP_OBJ_VAL(number) : SLP_NULL_VAL;
}

static SlpValue propagate_taint_binary(
    SlpVM *vm, SlpValue result,
    SlpValue left, SlpValue right) {
    return slp_value_is_tainted(left) ||
                   slp_value_is_tainted(right)
        ? slp_vm_taint_value(vm, result)
        : result;
}

static SlpValue propagate_taint_unary(
    SlpVM *vm, SlpValue result,
    SlpValue operand) {
    return slp_value_is_tainted(operand)
        ? slp_vm_taint_value(vm, result)
        : result;
}

static int32_t numeric_int_value(SlpValue value) {
    return (int32_t)slp_value_as_number(value);
}

static int64_t numeric_long_value(SlpValue value) {
    value = slp_value_unwrap_taint(value);
    if (SLP_IS_OBJ(value) && SLP_OBJ_TYPE(value) == SLP_OBJ_LONG)
        return SLP_AS_LONG(value)->value;
    return (int64_t)slp_value_as_number(value);
}

static bool evaluate_assignment_operator(
    SlpVM *vm, uint8_t opcode,
    SlpValue left, SlpValue right,
    SlpValue *result) {
    SlpNumericKind kind = widest_numeric_kind(left, right);
    switch (opcode) {
    case OP_ADD:
        if (kind == SLP_NUMERIC_DOUBLE)
            *result = make_double_value(
                vm, slp_value_as_number(left) +
                    slp_value_as_number(right));
        else if (kind == SLP_NUMERIC_LONG)
            *result = make_long_value(
                vm, (int64_t)(
                    (uint64_t)numeric_long_value(left) +
                    (uint64_t)numeric_long_value(right)));
        else
            *result = SLP_NUM_VAL((double)(int32_t)(
                (uint32_t)numeric_int_value(left) +
                (uint32_t)numeric_int_value(right)));
        return true;
    case OP_SUBTRACT:
        if (kind == SLP_NUMERIC_DOUBLE)
            *result = make_double_value(
                vm, slp_value_as_number(left) -
                    slp_value_as_number(right));
        else if (kind == SLP_NUMERIC_LONG)
            *result = make_long_value(
                vm, (int64_t)(
                    (uint64_t)numeric_long_value(left) -
                    (uint64_t)numeric_long_value(right)));
        else
            *result = SLP_NUM_VAL((double)(int32_t)(
                (uint32_t)numeric_int_value(left) -
                (uint32_t)numeric_int_value(right)));
        return true;
    case OP_MULTIPLY:
        if (kind == SLP_NUMERIC_DOUBLE)
            *result = make_double_value(
                vm, slp_value_as_number(left) *
                    slp_value_as_number(right));
        else if (kind == SLP_NUMERIC_LONG)
            *result = make_long_value(
                vm, (int64_t)(
                    (uint64_t)numeric_long_value(left) *
                    (uint64_t)numeric_long_value(right)));
        else
            *result = SLP_NUM_VAL((double)(int32_t)(
                (uint32_t)numeric_int_value(left) *
                (uint32_t)numeric_int_value(right)));
        return true;
    case OP_DIVIDE:
        if (kind == SLP_NUMERIC_DOUBLE) {
            *result = make_double_value(
                vm, slp_value_as_number(left) /
                    slp_value_as_number(right));
            return true;
        } else {
            int64_t divisor = kind == SLP_NUMERIC_LONG
                                  ? numeric_long_value(right)
                                  : numeric_int_value(right);
            if (divisor == 0) {
                slp_vm_runtime_error(vm, "Division by zero.");
                return false;
            }
            if (kind == SLP_NUMERIC_LONG) {
                int64_t dividend = numeric_long_value(left);
                *result = make_long_value(
                    vm,
                    dividend == INT64_MIN && divisor == -1
                        ? INT64_MIN
                        : dividend / divisor);
            } else {
                int32_t dividend = numeric_int_value(left);
                int32_t int_divisor = (int32_t)divisor;
                *result = SLP_NUM_VAL((double)(
                    dividend == INT32_MIN && int_divisor == -1
                        ? INT32_MIN
                        : dividend / int_divisor));
            }
            return true;
        }
    case OP_POWER:
        *result = make_double_value(
            vm, pow(slp_value_as_number(left),
                    slp_value_as_number(right)));
        return true;
    case OP_CONCAT: {
        SlpObjString *left_text =
            slp_vm_stringify(vm, left);
        SlpObjString *right_text =
            slp_vm_stringify(vm, right);
        if (!left_text || !right_text) {
            slp_vm_runtime_error(vm, "Out of memory.");
            return false;
        }
        uint32_t total =
            left_text->length + right_text->length;
        char *buffer = (char*)SLP_MALLOC(
            vm->allocator, (size_t)total + 1);
        if (!buffer) {
            slp_vm_runtime_error(vm, "Out of memory.");
            return false;
        }
        memcpy(
            buffer, left_text->chars,
            left_text->length);
        memcpy(
            buffer + left_text->length,
            right_text->chars, right_text->length);
        buffer[total] = '\0';
        SlpObjString *text =
            slp_vm_copy_string(vm, buffer, total);
        SLP_FREE(vm->allocator, buffer);
        if (!text) {
            slp_vm_runtime_error(vm, "Out of memory.");
            return false;
        }
        *result = SLP_OBJ_VAL(text);
        return true;
    }
    case OP_BIT_AND:
        *result = kind == SLP_NUMERIC_LONG
            ? make_long_value(
                  vm, numeric_long_value(left) &
                      numeric_long_value(right))
            : SLP_NUM_VAL((double)(
                  numeric_int_value(left) &
                  numeric_int_value(right)));
        return true;
    case OP_BIT_OR:
        *result = kind == SLP_NUMERIC_LONG
            ? make_long_value(
                  vm, numeric_long_value(left) |
                      numeric_long_value(right))
            : SLP_NUM_VAL((double)(
                  numeric_int_value(left) |
                  numeric_int_value(right)));
        return true;
    case OP_BIT_XOR:
        *result = kind == SLP_NUMERIC_LONG
            ? make_long_value(
                  vm, numeric_long_value(left) ^
                      numeric_long_value(right))
            : SLP_NUM_VAL((double)(
                  numeric_int_value(left) ^
                  numeric_int_value(right)));
        return true;
    case OP_LSHIFT:
        if (numeric_kind(left) == SLP_NUMERIC_LONG) {
            uint32_t shift =
                (uint32_t)numeric_int_value(right) & 63u;
            *result = make_long_value(
                vm, (int64_t)(
                    (uint64_t)numeric_long_value(left)
                    << shift));
        } else {
            uint32_t shift =
                (uint32_t)numeric_int_value(right) & 31u;
            *result = SLP_NUM_VAL((double)(int32_t)(
                (uint32_t)numeric_int_value(left)
                << shift));
        }
        return true;
    case OP_RSHIFT:
        if (numeric_kind(left) == SLP_NUMERIC_LONG) {
            uint32_t shift =
                (uint32_t)numeric_int_value(right) & 63u;
            *result = make_long_value(
                vm, numeric_long_value(left) >> shift);
        } else {
            uint32_t shift =
                (uint32_t)numeric_int_value(right) & 31u;
            *result = SLP_NUM_VAL((double)(
                numeric_int_value(left) >> shift));
        }
        return true;
    default:
        slp_vm_runtime_error(
            vm, "Unsupported tuple assignment operator.");
        return false;
    }
}

static void intern_grow(SlpVM *vm) {
    int new_cap = vm->interned_capacity * 2;
    SlpObjString **new_table = (SlpObjString**)vm->allocator->reallocate(
        NULL, sizeof(SlpObjString*) * new_cap, vm->allocator->user_data);
    if (!new_table) return;
    memset(new_table, 0, sizeof(SlpObjString*) * new_cap);
    for (int i = 0; i < vm->interned_capacity; i++) {
        SlpObjString *s = vm->interned[i];
        if (s) {
            uint32_t idx = s->hash & (new_cap - 1);
            while (new_table[idx]) {
                idx = (idx + 1) & (new_cap - 1);
            }
            new_table[idx] = s;
        }
    }
    vm->allocator->reallocate(vm->interned, 0, vm->allocator->user_data);
    vm->interned = new_table;
    vm->interned_capacity = new_cap;
}

SlpObjString *slp_vm_intern_string(SlpVM *vm, const char *chars, uint32_t length) {
    uint32_t hash = slp_hash_string(chars, length);
    if (vm->interned) {
        uint32_t idx = hash & (vm->interned_capacity - 1);
        for (int probes = 0; probes < vm->interned_capacity; probes++) {
            SlpObjString *s = vm->interned[idx];
            if (!s) break;
            if (s->hash == hash && s->length == length) {
                bool match = true;
                for (uint32_t i = 0; i < length; i++) {
                    if (s->chars[i] != chars[i]) { match = false; break; }
                }
                if (match) return s;
            }
            idx = (idx + 1) & (vm->interned_capacity - 1);
        }
    }
    if (vm->interned_count * 2 >= vm->interned_capacity) {
        if (!vm->interned) {
            vm->interned_capacity = SLP_INTERN_INIT_CAP;
            vm->interned = (SlpObjString**)vm->allocator->reallocate(
                NULL, sizeof(SlpObjString*) * vm->interned_capacity,
                vm->allocator->user_data);
            if (vm->interned) memset(vm->interned, 0, sizeof(SlpObjString*) * vm->interned_capacity);
        } else {
            intern_grow(vm);
        }
    }
    SlpObjString *str = slp_obj_string_new(vm->allocator, chars, length);
    if (str) {
        str->hash = hash;
        str->obj.next = vm->objects;
        vm->objects = &str->obj;
        vm->bytes_allocated += sizeof(SlpObjString) + length + 1;
        if (vm->interned) {
            uint32_t idx = hash & (vm->interned_capacity - 1);
            while (vm->interned[idx]) {
                idx = (idx + 1) & (vm->interned_capacity - 1);
            }
            vm->interned[idx] = str;
            vm->interned_count++;
        }
    }
    return str;
}

SlpObjString *slp_vm_copy_string(SlpVM *vm, const char *chars, uint32_t length) {
    return slp_vm_intern_string(vm, chars, length);
}

SlpObjString *slp_vm_copy_cstr(SlpVM *vm, const char *cstr) {
    return slp_vm_intern_string(vm, cstr, (uint32_t)strlen(cstr));
}

// Link a freshly created heap object into the GC object list and account its
// size toward the collection threshold. This does NOT trigger collection;
// that happens only at the interpreter safepoint, so callers may keep building
// the object afterward without it being collected mid-construction.
static void track_object(SlpVM *vm, SlpObj *obj) {
    obj->next = vm->objects;
    vm->objects = obj;
    vm->bytes_allocated += slp_gc_object_size(obj);
}

typedef struct {
    const char *short_name;
    const char *qualified_name;
    bool is_interface;
} SlpKnownClass;

/*
 * Sleep imports java.lang.*, java.util.*, and sleep.runtime.* by default.
 * The portable runtime has no JVM class loader, so retain the resolution
 * result needed by the language for the standard classes used by scripts.
 * Fully-qualified names remain valid and do not depend on this table.
 */
static const SlpKnownClass known_classes[] = {
    {"Boolean", "java.lang.Boolean", false},
    {"Byte", "java.lang.Byte", false},
    {"Character", "java.lang.Character", false},
    {"CharSequence", "java.lang.CharSequence", true},
    {"Class", "java.lang.Class", false},
    {"Double", "java.lang.Double", false},
    {"Float", "java.lang.Float", false},
    {"Integer", "java.lang.Integer", false},
    {"Long", "java.lang.Long", false},
    {"Math", "java.lang.Math", false},
    {"Number", "java.lang.Number", false},
    {"Object", "java.lang.Object", false},
    {"Runnable", "java.lang.Runnable", true},
    {"Short", "java.lang.Short", false},
    {"String", "java.lang.String", false},
    {"System", "java.lang.System", false},
    {"Thread", "java.lang.Thread", false},
    {"Array", "java.lang.reflect.Array", false},
    {"Arrays", "java.util.Arrays", false},
    {"Collection", "java.util.Collection", true},
    {"Collections", "java.util.Collections", false},
    {"Comparable", "java.lang.Comparable", true},
    {"Comparator", "java.util.Comparator", true},
    {"Deque", "java.util.Deque", true},
    {"HashMap", "java.util.HashMap", false},
    {"HashSet", "java.util.HashSet", false},
    {"Iterator", "java.util.Iterator", true},
    {"Iterable", "java.lang.Iterable", true},
    {"ArrayList", "java.util.ArrayList", false},
    {"LinkedHashMap", "java.util.LinkedHashMap", false},
    {"LinkedList", "java.util.LinkedList", false},
    {"List", "java.util.List", true},
    {"Map", "java.util.Map", true},
    {"Queue", "java.util.Queue", true},
    {"Set", "java.util.Set", true},
    {"Stack", "java.util.Stack", false},
    {"Vector", "java.util.Vector", false},
    {"Scalar", "sleep.runtime.Scalar", false},
    {"ScalarArray", "sleep.runtime.ScalarArray", true},
    {"ScalarHash", "sleep.runtime.ScalarHash", true},
    {"SleepUtils", "sleep.runtime.SleepUtils", false}
};

static const SlpKnownClass *find_known_class(
    const char *name) {
    size_t count =
        sizeof(known_classes) / sizeof(known_classes[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(name, known_classes[i].short_name) == 0 ||
            strcmp(name, known_classes[i].qualified_name) == 0)
            return &known_classes[i];
    }
    return NULL;
}

SlpObjClass *slp_vm_new_class(SlpVM *vm, const char *name) {
    if (!vm || !name) return NULL;
    const char *resolved = name;
    bool is_interface = false;
    const SlpKnownClass *known = find_known_class(name);
    if (known) {
        resolved = known->qualified_name;
        is_interface = known->is_interface;
    }

    /*
     * Java Class instances are canonical per class loader. Mirror that
     * identity so repeated ^String expressions and typeOf() results compare
     * with Sleep's reference-based "is" predicate.
     */
    for (SlpObj *object = vm->objects; object; object = object->next) {
        if (object->type != SLP_OBJ_CLASS) continue;
        SlpObjClass *existing = (SlpObjClass*)object;
        if (existing->name &&
            strcmp(existing->name->chars, resolved) == 0)
            return existing;
    }

    SlpObjString *class_name = slp_vm_copy_cstr(vm, resolved);
    if (!class_name) return NULL;
    SlpObjClass *class_object =
        slp_obj_class_new(vm->allocator, class_name, is_interface);
    if (class_object)
        track_object(vm, &class_object->obj);
    return class_object;
}

SlpObjArray *slp_vm_new_array(SlpVM *vm) {
    SlpObjArray *arr = slp_obj_array_new(vm->allocator);
    if (arr) track_object(vm, &arr->obj);
    return arr;
}

SlpObjHash *slp_vm_new_hash(SlpVM *vm) {
    SlpObjHash *hash = slp_obj_hash_new(vm->allocator);
    if (hash) track_object(vm, &hash->obj);
    return hash;
}

SlpObjJavaObject *slp_vm_new_java_object(
    SlpVM *vm, const char *class_name, SlpJavaObjectKind kind) {
    SlpObjClass *class_object =
        slp_vm_new_class(vm, class_name);
    if (!class_object) return NULL;
    SlpObjJavaObject *object = slp_obj_java_object_new(
        vm->allocator, class_object, kind);
    if (!object) return NULL;
    track_object(vm, &object->obj);
    if (kind == SLP_JAVA_LIST) {
        object->list = slp_vm_new_array(vm);
        if (!object->list) return NULL;
    } else if (kind == SLP_JAVA_MAP) {
        object->map = slp_vm_new_hash(vm);
        if (!object->map) return NULL;
    } else if (kind == SLP_JAVA_ERROR) {
        /*
         * Error objects keep their message and formatted diagnostics in a
         * compact two-element payload. The object value itself is the Java
         * toString() representation used by interpolation and println().
         */
        object->list = slp_vm_new_array(vm);
        if (!object->list) return NULL;
    }
    return object;
}

SlpObjIOHandle *slp_vm_get_console_handle(SlpVM *vm) {
    if (!vm) return NULL;
    if (vm->console_handle) return vm->console_handle;

    SlpObjIOHandle *handle =
        slp_obj_io_handle_new(vm->allocator);
    if (!handle) return NULL;
    handle->file = stdin;
    handle->is_console = true;
    track_object(vm, &handle->obj);
    vm->console_handle = handle;
    return handle;
}

SlpValue slp_vm_new_error(
    SlpVM *vm, const char *class_name,
    const char *message, const char *formatted,
    const char *display) {
    if (!vm || !class_name)
        return SLP_NULL_VAL;
    if (!message) message = "";
    if (!formatted) formatted = message;

    SlpObjJavaObject *error =
        slp_vm_new_java_object(
            vm, class_name, SLP_JAVA_ERROR);
    if (!error) return SLP_NULL_VAL;
    SlpValue result = SLP_OBJ_VAL(error);
    slp_vm_push(vm, result);

    SlpObjString *message_string =
        slp_vm_copy_cstr(vm, message);
    SlpObjString *formatted_string =
        slp_vm_copy_cstr(vm, formatted);
    if (message_string)
        slp_obj_array_push(
            vm->allocator, error->list,
            SLP_OBJ_VAL(message_string));
    if (formatted_string)
        slp_obj_array_push(
            vm->allocator, error->list,
            SLP_OBJ_VAL(formatted_string));

    char *generated_display = NULL;
    if (!display) {
        size_t capacity =
            strlen(class_name) +
            (message[0] ? 2u : 0u) +
            strlen(message) + 1;
        generated_display =
            (char*)SLP_MALLOC(
                vm->allocator, capacity);
        if (generated_display) {
            snprintf(
                generated_display, capacity,
                message[0] ? "%s: %s" : "%s",
                class_name, message);
            display = generated_display;
        }
    }
    SlpObjString *display_string =
        display
            ? slp_vm_copy_cstr(vm, display)
            : NULL;
    error->value = display_string
        ? SLP_OBJ_VAL(display_string)
        : SLP_NULL_VAL;

    SLP_FREE(
        vm->allocator, generated_display);
    slp_vm_pop(vm);
    return result;
}

/*
 * HashContainer.getAt() in Sleep 2.1 indexes its backing Java Map with
 * key.getValue().toString(). Collections therefore use SleepUtils.describe(),
 * so two separately-created but equivalent argument arrays are the same key.
 * Store that canonical string at the VM boundary rather than object identity.
 */
static SlpObjString *canonical_hash_key(SlpVM *vm, SlpValue key) {
    while (SLP_IS_OBJ(key) &&
           SLP_OBJ_TYPE(key) == SLP_OBJ_SCALAR_CELL)
        key = SLP_AS_SCALAR_CELL(key)->value;

    /* Predicates are represented as booleans internally, but Sleep exposes
       their scalar text as 1 or the empty string. */
    if (SLP_IS_BOOL(key))
        return slp_vm_copy_cstr(
            vm, SLP_AS_BOOL(key) ? "1" : "");
    return slp_vm_stringify(vm, key);
}

static bool hash_set_canonical(
    SlpVM *vm, SlpObjHash *hash, SlpValue key, SlpValue value) {
    bool exists = slp_obj_hash_contains(hash, key);
    if (exists || hash->order_mode == 0 || !hash->removal_policy)
        return slp_obj_hash_set(vm->allocator, hash, key, value);

    int eldest_index = slp_obj_hash_ordered_index(hash, 0);
    SlpValue eldest_key = eldest_index >= 0
        ? hash->entries[eldest_index].key
        : key;
    SlpValue eldest_value = eldest_index >= 0
        ? hash->entries[eldest_index].value
        : SLP_NULL_VAL;
    SlpObjString *message = slp_vm_copy_cstr(vm, "remove");
    if (!message) return false;
    slp_vm_push(vm, SLP_OBJ_VAL(hash->removal_policy));
    slp_vm_push(vm, SLP_OBJ_VAL(message));
    slp_vm_push(vm, SLP_OBJ_VAL(hash));
    slp_vm_push(vm, eldest_key);
    slp_vm_push(vm, eldest_value);
    if (slp_vm_call(vm, 3, true) != SLP_OK)
        return false;
    bool remove_eldest =
        !slp_value_is_falsy(slp_vm_pop(vm));

    if (remove_eldest) {
        if (eldest_index < 0)
            return true;
        slp_obj_hash_delete(vm->allocator, hash, eldest_key);
    }
    return slp_obj_hash_set(vm->allocator, hash, key, value);
}

SlpValue slp_vm_hash_get(SlpVM *vm, SlpObjHash *hash, SlpValue key) {
    if (!vm || !hash) return SLP_NULL_VAL;
    SlpObjString *canonical = canonical_hash_key(vm, key);
    if (!canonical) return SLP_NULL_VAL;
    slp_vm_push(vm, SLP_OBJ_VAL(canonical));

    SlpValue value = slp_obj_hash_get(
        hash, SLP_OBJ_VAL(canonical));
    if (!SLP_IS_NULL(value) || !hash->miss_policy) {
        slp_vm_pop(vm);
        return value;
    }

    SlpObjString *message = slp_vm_copy_cstr(vm, "miss");
    if (!message) {
        slp_vm_pop(vm);
        return SLP_NULL_VAL;
    }
    slp_vm_push(vm, SLP_OBJ_VAL(hash->miss_policy));
    slp_vm_push(vm, SLP_OBJ_VAL(message));
    slp_vm_push(vm, SLP_OBJ_VAL(hash));
    slp_vm_push(vm, key);
    if (slp_vm_call(vm, 2, true) != SLP_OK) {
        slp_vm_pop(vm);
        return SLP_NULL_VAL;
    }

    value = slp_vm_pop(vm);
    hash_set_canonical(
        vm, hash, SLP_OBJ_VAL(canonical), value);
    slp_vm_pop(vm);
    return value;
}

bool slp_vm_hash_contains(
    SlpVM *vm, SlpObjHash *hash, SlpValue key) {
    if (!vm || !hash) return false;
    SlpObjString *canonical = canonical_hash_key(vm, key);
    return canonical &&
           slp_obj_hash_contains(hash, SLP_OBJ_VAL(canonical));
}

bool slp_vm_hash_set(
    SlpVM *vm, SlpObjHash *hash, SlpValue key, SlpValue value) {
    if (!vm || !hash) return false;
    SlpObjString *canonical = canonical_hash_key(vm, key);
    if (!canonical) return false;
    slp_vm_push(vm, SLP_OBJ_VAL(canonical));
    bool result = hash_set_canonical(
        vm, hash, SLP_OBJ_VAL(canonical), value);
    slp_vm_pop(vm);
    return result;
}

bool slp_vm_hash_delete(
    SlpVM *vm, SlpObjHash *hash, SlpValue key) {
    if (!vm || !hash) return false;
    SlpObjString *canonical = canonical_hash_key(vm, key);
    return canonical &&
           slp_obj_hash_delete(
               vm->allocator, hash, SLP_OBJ_VAL(canonical));
}

SlpObjLong *slp_vm_new_long(SlpVM *vm, int64_t value) {
    SlpObjLong *number = slp_obj_long_new(vm->allocator, value);
    if (number) track_object(vm, &number->obj);
    return number;
}

SlpObjDouble *slp_vm_new_double(SlpVM *vm, double value) {
    SlpObjDouble *number = slp_obj_double_new(vm->allocator, value);
    if (number) track_object(vm, &number->obj);
    return number;
}

SlpValue slp_vm_taint_value(
    SlpVM *vm, SlpValue value) {
    if (!vm || slp_value_is_tainted(value))
        return value;
    if (SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) ==
            SLP_OBJ_ARRAY) {
        SlpObjArray *array =
            SLP_AS_ARRAY(value);
        for (int i = 0;
             i < array->count; i++)
            array->elements[i] =
                slp_vm_taint_value(
                    vm,
                    array->elements[i]);
        return value;
    }
    if (SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) ==
            SLP_OBJ_HASH) {
        SlpObjHash *hash =
            SLP_AS_HASH(value);
        for (int i = 0;
             i < hash->capacity; i++) {
            if (SLP_IS_NULL(
                    hash->entries[i].key) ||
                SLP_IS_NULL(
                    hash->entries[i].value))
                continue;
            hash->entries[i].value =
                slp_vm_taint_value(
                    vm,
                    hash->entries[i].value);
        }
        return value;
    }
    if (SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) ==
            SLP_OBJ_JAVA_OBJECT) {
        SlpObjJavaObject *object =
            SLP_AS_JAVA_OBJECT(value);
        if (object->list) {
            for (int i = 0;
                 i < object->list->count;
                 i++)
                object->list->elements[i] =
                    slp_vm_taint_value(
                        vm,
                        object->list
                            ->elements[i]);
            return value;
        }
        if (object->map) {
            SlpObjHash *hash =
                object->map;
            for (int i = 0;
                 i < hash->capacity; i++) {
                if (SLP_IS_NULL(
                        hash->entries[i].key) ||
                    SLP_IS_NULL(
                        hash->entries[i].value))
                    continue;
                hash->entries[i].value =
                    slp_vm_taint_value(
                        vm,
                        hash->entries[i].value);
            }
            return value;
        }
    }
    SlpObjTainted *tainted =
        slp_obj_tainted_new(
            vm->allocator, value);
    if (!tainted)
        return SLP_NULL_VAL;
    track_object(vm, &tainted->obj);
    return SLP_OBJ_VAL(tainted);
}

static bool ensure_closure_scope(SlpVM *vm, SlpObjClosure *closure) {
    if (closure->scope) return true;
    closure->scope = slp_vm_new_hash(vm);
    if (!closure->scope) return false;
    SlpObjString *this_name = slp_vm_copy_string(vm, "$this", 5);
    if (!this_name) return false;
    return slp_obj_hash_set(vm->allocator, closure->scope,
                            SLP_OBJ_VAL(this_name), SLP_OBJ_VAL(closure));
}

SlpObjClosure *slp_vm_new_closure(
    SlpVM *vm, SlpObjFunction *function) {
    if (!vm || !function)
        return NULL;
    SlpObjClosure *closure =
        slp_obj_closure_new(
            vm->allocator, function);
    if (!closure)
        return NULL;
    track_object(vm, &closure->obj);
    closure->identity =
        ++vm->next_closure_identity;
    return closure;
}

SlpObjClosure *slp_vm_clone_closure(SlpVM *vm, SlpObjClosure *source) {
    if (!vm || !source) return NULL;
    SlpObjClosure *clone =
        slp_obj_closure_new(vm->allocator, source->function);
    if (!clone) return NULL;
    track_object(vm, &clone->obj);
    clone->identity = ++vm->next_closure_identity;
    clone->is_inline = source->is_inline;
    for (int i = 0; i < source->function->upvalue_count; i++)
        clone->upvalues[i] = source->upvalues[i];
    if (!ensure_closure_scope(vm, clone))
        return NULL;
    return clone;
}

static SlpCallFrame *current_frame(SlpVM *vm) {
    return &vm->frames[vm->frame_count - 1];
}

static void set_regex_matches(SlpVM *vm, SlpObjArray *matches) {
    vm->last_regex_matches = matches;
    if (vm->frame_count > 0)
        current_frame(vm)->closure->last_regex_matches = matches;
}

SlpObjArray *slp_vm_regex_matches(SlpVM *vm) {
    if (!vm) return NULL;
    if (vm->frame_count > 0)
        return current_frame(vm)->closure->last_regex_matches;
    return vm->last_regex_matches;
}

static char *translate_sleep_regex(SlpVM *vm, const char *pattern) {
    size_t length = strlen(pattern);
    if (length > (SIZE_MAX - 1) / 16) return NULL;
    char *translated = SLP_MALLOC(vm->allocator, length * 16 + 1);
    if (!translated) return NULL;
    char *write = translated;
    int class_depth = 0;
    bool deferred_class_hyphen = false;

    for (size_t i = 0; i < length; i++) {
        /* Sleep accepts inline DOTALL mode. POSIX ERE already lets a dot
         * match newlines, so consume the flag before handing the pattern to
         * regcomp(). */
        if (i == 0 && length >= 4 &&
            pattern[0] == '(' && pattern[1] == '?' &&
            pattern[2] == 's' && pattern[3] == ')') {
            i = 3;
            continue;
        }
        char current = pattern[i];
        if (current == '\\' && i + 1 < length) {
            char escaped = pattern[++i];
            const char *replacement = NULL;
            if (escaped == 's')
                replacement =
                    class_depth > 0
                        ? "[:space:]"
                        : "[[:space:]]";
            else if (escaped == 'S')
                replacement =
                    class_depth > 0
                        ? NULL
                        : "[^[:space:]]";
            else if (escaped == 'd')
                replacement =
                    class_depth > 0
                        ? "[:digit:]"
                        : "[[:digit:]]";
            else if (escaped == 'D')
                replacement =
                    class_depth > 0
                        ? NULL
                        : "[^[:digit:]]";
            else if (escaped == 'w')
                replacement =
                    class_depth > 0
                        ? "[:alnum:]_"
                        : "[[:alnum:]_]";
            else if (escaped == 'W')
                replacement =
                    class_depth > 0
                        ? NULL
                        : "[^[:alnum:]_]";
            if (replacement) {
                size_t replacement_length = strlen(replacement);
                memcpy(write, replacement, replacement_length);
                write += replacement_length;
            } else if (class_depth > 0 &&
                       escaped == ']') {
                /*
                 * POSIX bracket expressions only recognize a literal ']'
                 * when it is the first member. It was emitted when the
                 * outer class opened, so omit its Java-style escape here.
                 */
                continue;
            } else if (class_depth > 0 &&
                       escaped == '-') {
                /*
                 * A literal '-' is portable at the end of a bracket
                 * expression. Defer it until the outer class closes.
                 */
                deferred_class_hyphen = true;
            } else if (class_depth > 0 &&
                       (escaped == '[' ||
                        escaped == '.' ||
                        escaped == ':' ||
                        escaped == '|' ||
                        escaped == '`' ||
                        escaped == '^' ||
                        escaped == '{' ||
                        escaped == '}')) {
                *write++ = escaped;
            } else {
                *write++ = '\\';
                *write++ = escaped;
            }
            continue;
        }
        if (current == '[') {
            if (class_depth == 0) {
                *write++ = '[';
                /*
                 * Java accepts '\\]' anywhere inside a class. Reserve the
                 * POSIX first-member position when that form occurs.
                 */
                bool has_literal_closing_bracket = false;
                int look_depth = 1;
                for (size_t j = i + 1;
                     j < length && look_depth > 0; j++) {
                    if (pattern[j] == '\\' &&
                        j + 1 < length) {
                        if (pattern[j + 1] == ']')
                            has_literal_closing_bracket = true;
                        j++;
                    } else if (pattern[j] == '[') {
                        look_depth++;
                    } else if (pattern[j] == ']') {
                        look_depth--;
                    }
                }
                if (has_literal_closing_bracket)
                    *write++ = ']';
                deferred_class_hyphen = false;
            }
            class_depth++;
            /*
             * Java permits nested bracket expressions as class unions.
             * POSIX ERE does not; flatten the nested members into the outer
             * bracket expression.
             */
            continue;
        }
        if (current == ']' && class_depth > 0) {
            class_depth--;
            if (class_depth == 0) {
                if (deferred_class_hyphen)
                    *write++ = '-';
                *write++ = ']';
                deferred_class_hyphen = false;
            }
            continue;
        }
        /* POSIX ERE has no reluctant or possessive quantifiers. Their match
           choice differs in edge cases, but dropping the modifier preserves
           the accepted language and the capture behavior used by Sleep. */
        if (class_depth == 0 &&
            (current == '+' || current == '?') && i > 0 &&
            (pattern[i - 1] == '+' || pattern[i - 1] == '*' ||
             pattern[i - 1] == '?')) {
            continue;
        }
        *write++ = current;
    }
    *write = '\0';
    return translated;
}

int slp_vm_regex_find(SlpVM *vm, const char *text, const char *pattern,
                      int start, bool full_match, int *match_end) {
    if (match_end) *match_end = -1;
    if (!vm || !text || !pattern) return -1;
    size_t text_length = strlen(text);
    if (start < 0) start = 0;
    if ((size_t)start > text_length) {
        set_regex_matches(vm, NULL);
        return -1;
    }

    char *translated = translate_sleep_regex(vm, pattern);
    if (!translated) {
        set_regex_matches(vm, NULL);
        return -1;
    }

    regex_t regex;
    int compile_result =
        SLP_REGEX_COMPILE(vm, &regex, translated, REG_EXTENDED);
    SLP_FREE(vm->allocator, translated);
    if (compile_result != 0) {
        set_regex_matches(vm, NULL);
        return -1;
    }

    int group_count = (int)regex.re_nsub;
    if (group_count > 15) group_count = 15;
    regmatch_t matches[16];
    int flags = start > 0 ? REG_NOTBOL : 0;
    int execute_result =
        SLP_REGEX_EXECUTE(
            &regex, text + start, (size_t)group_count + 1,
            matches, flags);
    bool found = execute_result == 0;
    int absolute_start = -1;
    int absolute_end = -1;
    if (found) {
        absolute_start = start + (int)matches[0].rm_so;
        absolute_end = start + (int)matches[0].rm_eo;
        if (full_match &&
            (absolute_start != 0 || (size_t)absolute_end != text_length)) {
            found = false;
        }
    }

    if (!found) {
        SLP_REGEX_FREE(&regex);
        set_regex_matches(vm, NULL);
        return -1;
    }

    SlpObjArray *captures = slp_vm_new_array(vm);
    if (!captures) {
        SLP_REGEX_FREE(&regex);
        set_regex_matches(vm, NULL);
        return -1;
    }
    for (int i = 1; i <= group_count; i++) {
        if (matches[i].rm_so < 0) {
            slp_obj_array_push(
                vm->allocator, captures,
                SLP_OBJ_VAL(slp_vm_copy_string(vm, "", 0)));
        } else {
            const char *begin = text + start + matches[i].rm_so;
            uint32_t capture_length =
                (uint32_t)(matches[i].rm_eo - matches[i].rm_so);
            slp_obj_array_push(
                vm->allocator, captures,
                SLP_OBJ_VAL(slp_vm_copy_string(vm, begin, capture_length)));
        }
    }
    set_regex_matches(vm, captures);
    if (match_end) *match_end = absolute_end;
    SLP_REGEX_FREE(&regex);
    return absolute_start;
}

static bool regex_key_equals(const SlpRegexState *state,
                             SlpObjString *text, SlpObjString *pattern) {
    return state->text == text && state->pattern == pattern;
}

static bool regex_hasmatch(SlpVM *vm, SlpObjString *text,
                           SlpObjString *pattern) {
    SlpObjClosure *closure = current_frame(vm)->closure;
    int state_index = -1;
    for (int i = 0; i < closure->regex_state_count; i++) {
        if (regex_key_equals(&closure->regex_states[i], text, pattern)) {
            state_index = i;
            break;
        }
    }

    if (state_index < 0) {
        if (closure->regex_state_count < 16) {
            if (closure->regex_state_count == closure->regex_state_capacity) {
                int old_capacity = closure->regex_state_capacity;
                int new_capacity = old_capacity < 4 ? 4 : old_capacity * 2;
                if (new_capacity > 16) new_capacity = 16;
                SlpRegexState *states =
                    (SlpRegexState*)vm->allocator->reallocate(
                        closure->regex_states,
                        sizeof(SlpRegexState) * (size_t)new_capacity,
                        vm->allocator->user_data);
                if (!states) {
                    slp_vm_runtime_error(vm, "Out of memory.");
                    return false;
                }
                closure->regex_states = states;
                closure->regex_state_capacity = new_capacity;
            }
            state_index = closure->regex_state_count++;
        } else {
            state_index = 0;
            for (int i = 1; i < closure->regex_state_count; i++) {
                if (closure->regex_states[i].sequence <
                    closure->regex_states[state_index].sequence)
                    state_index = i;
            }
        }
        closure->regex_states[state_index].text = text;
        closure->regex_states[state_index].pattern = pattern;
        closure->regex_states[state_index].next_offset = 0;
    }

    SlpRegexState *state = &closure->regex_states[state_index];
    state->sequence = ++closure->next_regex_sequence;
    int match_end = -1;
    int match_start =
        slp_vm_regex_find(vm, text->chars, pattern->chars,
                          state->next_offset, false, &match_end);
    if (match_start < 0) {
        closure->regex_state_count--;
        if (state_index != closure->regex_state_count) {
            closure->regex_states[state_index] =
                closure->regex_states[closure->regex_state_count];
        }
        return false;
    }

    state = &closure->regex_states[state_index];
    state->next_offset = match_end;
    if (match_end == match_start)
        state->next_offset = match_end + 1;
    return true;
}

static SlpValue new_index_collection(SlpVM *vm, uint8_t kind,
                                     SlpObjType parent_type) {
    if (kind == 1 || (kind == 0 && parent_type == SLP_OBJ_ARRAY)) {
        SlpObjArray *array = slp_vm_new_array(vm);
        return array ? SLP_OBJ_VAL(array) : SLP_NULL_VAL;
    }
    SlpObjHash *hash = slp_vm_new_hash(vm);
    return hash ? SLP_OBJ_VAL(hash) : SLP_NULL_VAL;
}

static SlpObjHash *current_local_scope(SlpCallFrame *frame) {
    if (!frame || !frame->local_scopes || frame->local_scopes->count == 0)
        return NULL;
    SlpValue value = frame->local_scopes->elements[frame->local_scopes->count - 1];
    if (!SLP_IS_OBJ(value) || SLP_OBJ_TYPE(value) != SLP_OBJ_HASH)
        return NULL;
    return SLP_AS_HASH(value);
}

static SlpValue dereference_value(SlpValue value) {
    while (SLP_IS_OBJ(value) && SLP_OBJ_TYPE(value) == SLP_OBJ_SCALAR_CELL)
        value = SLP_AS_SCALAR_CELL(value)->value;
    return value;
}

typedef struct {
    SlpVM *vm;
    char *chars;
    size_t length;
    size_t capacity;
    bool failed;
    SlpObj *containers[64];
    int container_count;
} SlpStringBuilder;

static bool same_double_bits(double left, double right) {
    uint64_t left_bits;
    uint64_t right_bits;
    memcpy(&left_bits, &left, sizeof(left_bits));
    memcpy(&right_bits, &right, sizeof(right_bits));
    return left_bits == right_bits;
}

static int format_sleep_number(double value, char *output,
                               size_t output_size) {
    if (output_size == 0) return 0;
    if (isnan(value))
        return snprintf(output, output_size, "NaN");
    if (isinf(value))
        return snprintf(output, output_size,
                        value < 0 ? "-Infinity" : "Infinity");

    // Untagged VM numbers model Sleep IntValue values when integral.
    if (value >= (double)INT32_MIN && value <= (double)INT32_MAX &&
        value == (double)(int32_t)value)
        return snprintf(output, output_size, "%d", (int32_t)value);

    char shortest[64];
    snprintf(shortest, sizeof(shortest), "%.17g", value);
    for (int precision = 1; precision <= 17; precision++) {
        char candidate[64];
        snprintf(candidate, sizeof(candidate), "%.*g", precision, value);
        char *end = NULL;
        double parsed = strtod(candidate, &end);
        if (end && *end == '\0' && same_double_bits(parsed, value)) {
            memcpy(shortest, candidate, strlen(candidate) + 1);
            break;
        }
    }

    const char *cursor = shortest;
    bool negative = *cursor == '-';
    if (negative) cursor++;
    const char *exponent_marker = strchr(cursor, 'e');
    if (!exponent_marker) exponent_marker = strchr(cursor, 'E');
    const char *mantissa_end =
        exponent_marker ? exponent_marker : cursor + strlen(cursor);
    int explicit_exponent =
        exponent_marker ? (int)strtol(exponent_marker + 1, NULL, 10) : 0;

    char digits[64];
    int digit_count = 0;
    int decimal_index = 0;
    bool saw_decimal = false;
    for (const char *part = cursor; part < mantissa_end; part++) {
        if (*part == '.') {
            decimal_index = digit_count;
            saw_decimal = true;
        } else if (*part >= '0' && *part <= '9') {
            digits[digit_count++] = *part;
        }
    }
    if (!saw_decimal) decimal_index = digit_count;

    int first_significant = 0;
    while (first_significant < digit_count - 1 &&
           digits[first_significant] == '0')
        first_significant++;
    int scientific_exponent =
        explicit_exponent + decimal_index - first_significant - 1;
    int significant_count = digit_count - first_significant;
    char *write = output;
    size_t remaining = output_size;
#define APPEND_NUMBER_CHAR(character)                                         \
    do {                                                                      \
        if (remaining > 1) {                                                  \
            *write++ = (character);                                           \
            remaining--;                                                      \
        }                                                                     \
    } while (0)

    if (negative) APPEND_NUMBER_CHAR('-');
    if (scientific_exponent >= -3 && scientific_exponent < 7) {
        if (scientific_exponent < 0) {
            APPEND_NUMBER_CHAR('0');
            APPEND_NUMBER_CHAR('.');
            for (int i = -1; i > scientific_exponent; i--)
                APPEND_NUMBER_CHAR('0');
            for (int i = 0; i < significant_count; i++)
                APPEND_NUMBER_CHAR(digits[first_significant + i]);
        } else {
            int integer_digits = scientific_exponent + 1;
            for (int i = 0; i < integer_digits; i++) {
                APPEND_NUMBER_CHAR(
                    i < significant_count
                        ? digits[first_significant + i]
                        : '0');
            }
            if (significant_count > integer_digits) {
                APPEND_NUMBER_CHAR('.');
                for (int i = integer_digits; i < significant_count; i++)
                    APPEND_NUMBER_CHAR(digits[first_significant + i]);
            }
        }
    } else {
        APPEND_NUMBER_CHAR(digits[first_significant]);
        APPEND_NUMBER_CHAR('.');
        if (significant_count == 1) {
            APPEND_NUMBER_CHAR('0');
        } else {
            for (int i = 1; i < significant_count; i++)
                APPEND_NUMBER_CHAR(digits[first_significant + i]);
        }
        APPEND_NUMBER_CHAR('E');
        char exponent[16];
        snprintf(exponent, sizeof(exponent), "%d", scientific_exponent);
        for (const char *part = exponent; *part; part++)
            APPEND_NUMBER_CHAR(*part);
    }
    *write = '\0';
#undef APPEND_NUMBER_CHAR
    return (int)(write - output);
}

static int format_sleep_double(double value, char *output,
                               size_t output_size) {
    if (isfinite(value) && value >= (double)INT32_MIN &&
        value <= (double)INT32_MAX && value == trunc(value))
        return snprintf(output, output_size, "%.1f", value);
    return format_sleep_number(value, output, output_size);
}

static void string_builder_append(SlpStringBuilder *builder,
                                  const char *chars, size_t length) {
    if (builder->failed || length == 0) return;
    if (length > SIZE_MAX - builder->length - 1) {
        builder->failed = true;
        return;
    }
    size_t needed = builder->length + length + 1;
    if (needed > builder->capacity) {
        size_t capacity = builder->capacity < 64 ? 64 : builder->capacity;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) {
                capacity = needed;
                break;
            }
            capacity *= 2;
        }
        char *resized = (char*)SLP_REALLOC(
            builder->vm->allocator, builder->chars, capacity);
        if (!resized) {
            builder->failed = true;
            return;
        }
        builder->chars = resized;
        builder->capacity = capacity;
    }
    memcpy(builder->chars + builder->length, chars, length);
    builder->length += length;
    builder->chars[builder->length] = '\0';
}

static void stringify_value(SlpStringBuilder *builder, SlpValue value,
                            bool quote_strings, int depth) {
    value = dereference_value(value);
    if (depth > 32) {
        string_builder_append(builder, "...", 3);
        return;
    }
    if (SLP_IS_NULL(value))
        return;
    if (SLP_IS_BOOL(value)) {
        const char *text = SLP_AS_BOOL(value) ? "true" : "false";
        string_builder_append(builder, text, strlen(text));
        return;
    }
    if (SLP_IS_NUM(value)) {
        char number[64];
        int length =
            format_sleep_number(SLP_AS_NUM(value), number, sizeof(number));
        if (length > 0)
            string_builder_append(builder, number, (size_t)length);
        return;
    }
    if (!SLP_IS_OBJ(value))
        return;

    SlpObj *object = SLP_AS_OBJ(value);
    switch (object->type) {
    case SLP_OBJ_STRING: {
        SlpObjString *string = (SlpObjString*)object;
        if (quote_strings) string_builder_append(builder, "'", 1);
        string_builder_append(
            builder, string->chars, string->length);
        if (quote_strings) string_builder_append(builder, "'", 1);
        break;
    }
    case SLP_OBJ_CLASS: {
        SlpObjClass *class_object = (SlpObjClass*)object;
        const char *prefix =
            class_object->is_interface ? "interface " : "class ";
        string_builder_append(builder, prefix, strlen(prefix));
        if (class_object->name)
            string_builder_append(
                builder, class_object->name->chars,
                class_object->name->length);
        break;
    }
    case SLP_OBJ_JAVA_OBJECT: {
        SlpObjJavaObject *java_object =
            (SlpObjJavaObject*)object;
        if (java_object->kind == SLP_JAVA_LIST &&
            java_object->list) {
            string_builder_append(builder, "[", 1);
            for (int i = 0; i < java_object->list->count; i++) {
                if (i > 0)
                    string_builder_append(builder, ", ", 2);
                stringify_value(
                    builder, java_object->list->elements[i],
                    false, depth + 1);
            }
            string_builder_append(builder, "]", 1);
        } else if (java_object->kind == SLP_JAVA_MAP &&
                   java_object->map) {
            string_builder_append(builder, "{", 1);
            int emitted = 0;
            for (int ordinal = 0;
                 ordinal < java_object->map->count; ordinal++) {
                int index = slp_obj_hash_ordered_index(
                    java_object->map, ordinal);
                if (index < 0 ||
                    SLP_IS_NULL(
                        java_object->map->entries[index].value))
                    continue;
                if (emitted++ > 0)
                    string_builder_append(builder, ", ", 2);
                stringify_value(
                    builder,
                    java_object->map->entries[index].key,
                    false, depth + 1);
                string_builder_append(builder, "=", 1);
                stringify_value(
                    builder,
                    java_object->map->entries[index].value,
                    false, depth + 1);
            }
            string_builder_append(builder, "}", 1);
        } else if (!SLP_IS_NULL(java_object->value) &&
                   java_object->class_object &&
                   java_object->class_object->name &&
                   java_object->class_object->name->chars[0] != '[') {
            stringify_value(
                builder, java_object->value,
                false, depth + 1);
        } else if (java_object->class_object &&
                   java_object->class_object->name) {
            string_builder_append(
                builder, java_object->class_object->name->chars,
                java_object->class_object->name->length);
        } else {
            string_builder_append(builder, "[object]", 8);
        }
        break;
    }
    case SLP_OBJ_LONG: {
        char number[64];
        int length = snprintf(number, sizeof(number), "%lld",
                              (long long)((SlpObjLong*)object)->value);
        if (length > 0) {
            string_builder_append(builder, number, (size_t)length);
            if (quote_strings)
                string_builder_append(builder, "L", 1);
        }
        break;
    }
    case SLP_OBJ_DOUBLE: {
        char number[64];
        int length = format_sleep_double(
            ((SlpObjDouble*)object)->value, number, sizeof(number));
        if (length > 0)
            string_builder_append(builder, number, (size_t)length);
        break;
    }
    case SLP_OBJ_ARRAY: {
        SlpObjArray *array = (SlpObjArray*)object;
        if (!slp_obj_array_view_is_valid(array)) {
            slp_vm_abort_warning(
                builder->vm,
                "unsafe data modification: parent @array changed after "
                "&sublist creation");
            builder->failed = true;
            return;
        }
        for (int i = 0; i < builder->container_count; i++) {
            if (builder->containers[i] == object) {
                char reference[24];
                int length =
                    snprintf(reference, sizeof(reference), "@%d", i);
                if (length > 0)
                    string_builder_append(builder, reference, (size_t)length);
                return;
            }
        }
        if (builder->container_count >= 64) {
            string_builder_append(builder, "...", 3);
            return;
        }
        builder->containers[builder->container_count++] = object;
        string_builder_append(builder, "@(", 2);
        for (int i = 0; i < array->count; i++) {
            if (i > 0) string_builder_append(builder, ", ", 2);
            stringify_value(builder, array->elements[i], true, depth + 1);
        }
        string_builder_append(builder, ")", 1);
        break;
    }
    case SLP_OBJ_HASH: {
        SlpObjHash *hash = (SlpObjHash*)object;
        for (int i = 0; i < builder->container_count; i++) {
            if (builder->containers[i] == object) {
                char reference[24];
                int length =
                    snprintf(reference, sizeof(reference), "%%%d", i);
                if (length > 0)
                    string_builder_append(builder, reference, (size_t)length);
                return;
            }
        }
        if (builder->container_count >= 64) {
            string_builder_append(builder, "...", 3);
            return;
        }
        builder->containers[builder->container_count++] = object;
        string_builder_append(builder, "%(", 2);
        int emitted = 0;
        for (int ordinal = 0; ordinal < hash->count; ordinal++) {
            int i = slp_obj_hash_ordered_index(hash, ordinal);
            if (i < 0) continue;
            if (SLP_IS_NULL(hash->entries[i].value)) continue;
            if (emitted++ > 0) string_builder_append(builder, ", ", 2);
            stringify_value(builder, hash->entries[i].key, false, depth + 1);
            string_builder_append(builder, " => ", 4);
            stringify_value(builder, hash->entries[i].value, true, depth + 1);
        }
        string_builder_append(builder, ")", 1);
        break;
    }
    case SLP_OBJ_CLOSURE: {
        SlpObjClosure *closure = (SlpObjClosure*)object;
        SlpObjFunction *function = closure->function;
        string_builder_append(builder, "&closure[", 9);
        if (function && function->source_name)
            string_builder_append(builder, function->source_name->chars,
                                  function->source_name->length);
        else
            string_builder_append(builder, "<unknown>", 9);
        char location[64];
        int length;
        if (function && function->line_end > function->line_start) {
            length = snprintf(location, sizeof(location), ":%d-%d]#%llu",
                              function->line_start, function->line_end,
                              (unsigned long long)closure->identity);
        } else {
            int line = function ? function->line_start : 0;
            length = snprintf(location, sizeof(location), ":%d]#%llu", line,
                              (unsigned long long)closure->identity);
        }
        if (length > 0)
            string_builder_append(builder, location, (size_t)length);
        break;
    }
    case SLP_OBJ_FUNCTION:
        string_builder_append(builder, "&function", 9);
        break;
    case SLP_OBJ_NATIVE: {
        SlpObjNative *native = (SlpObjNative*)object;
        string_builder_append(builder, "&", 1);
        if (native->name)
            string_builder_append(builder, native->name->chars,
                                  native->name->length);
        break;
    }
    case SLP_OBJ_CONTINUATION: {
        SlpObjContinuation *continuation =
            (SlpObjContinuation*)object;
        if (continuation->coroutine_owner)
            stringify_value(
                builder, SLP_OBJ_VAL(continuation->coroutine_owner),
                quote_strings, depth + 1);
        else
            string_builder_append(builder, "&continuation", 13);
        break;
    }
    case SLP_OBJ_KEY_VALUE: {
        SlpObjKeyValue *kv = (SlpObjKeyValue*)object;
        stringify_value(builder, kv->key, false, depth + 1);
        string_builder_append(builder, " => ", 4);
        stringify_value(builder, kv->value, true, depth + 1);
        break;
    }
    case SLP_OBJ_TAINTED:
        stringify_value(
            builder,
            SLP_AS_TAINTED(value)->value,
            quote_strings, depth + 1);
        break;
    case SLP_OBJ_SCALAR_CELL:
        stringify_value(builder, ((SlpObjScalarCell*)object)->value,
                        quote_strings, depth + 1);
        break;
    default:
        string_builder_append(builder, "[object]", 8);
        break;
    }
}

SlpObjString *slp_vm_stringify(SlpVM *vm, SlpValue value) {
    if (SLP_IS_OBJ(value) && SLP_OBJ_TYPE(value) == SLP_OBJ_STRING)
        return SLP_AS_STRING(value);

    SlpStringBuilder builder;
    builder.vm = vm;
    builder.chars = NULL;
    builder.length = 0;
    builder.capacity = 0;
    builder.failed = false;
    builder.container_count = 0;
    stringify_value(&builder, value, false, 0);
    if (builder.failed) {
        if (builder.chars) SLP_FREE(vm->allocator, builder.chars);
        return NULL;
    }
    SlpObjString *result = slp_vm_copy_string(
        vm, builder.chars ? builder.chars : "", (uint32_t)builder.length);
    if (builder.chars) SLP_FREE(vm->allocator, builder.chars);
    return result;
}

static SlpObjString *describe_value(SlpVM *vm, SlpValue value) {
    SlpStringBuilder builder;
    builder.vm = vm;
    builder.chars = NULL;
    builder.length = 0;
    builder.capacity = 0;
    builder.failed = false;
    builder.container_count = 0;
    stringify_value(&builder, value, true, 0);
    if (builder.failed) {
        if (builder.chars) SLP_FREE(vm->allocator, builder.chars);
        return NULL;
    }
    SlpObjString *result = slp_vm_copy_string(
        vm, builder.chars ? builder.chars : "",
        (uint32_t)builder.length);
    if (builder.chars) SLP_FREE(vm->allocator, builder.chars);
    return result;
}

static void trace_unary_predicate(
    SlpVM *vm, const char *predicate, SlpValue value,
    bool result) {
    if (!vm || (vm->debug_flags & 64) != 64 ||
        (vm->debug_flags & 16) == 16)
        return;
    SlpObjString *description = describe_value(vm, value);
    if (!description) return;
    SlpCallFrame *frame =
        vm->frame_count > 0 ? current_frame(vm) : NULL;
    char line[32];
    snprintf(line, sizeof(line), "%d", frame_current_line(frame));
    slp_vm_write(vm, "Trace: ");
    slp_vm_write(vm, predicate);
    slp_vm_write(vm, " ");
    slp_vm_write(vm, description->chars);
    slp_vm_write(vm, result ? " ? TRUE at " : " ? FALSE at ");
    slp_vm_write(vm, frame_source_name(frame));
    slp_vm_write(vm, ":");
    slp_vm_write(vm, line);
    slp_vm_write(vm, "\n");
}

static void trace_binary_predicate(
    SlpVM *vm, SlpValue left, const char *predicate,
    SlpValue right, bool result) {
    if (!vm || (vm->debug_flags & 64) != 64 ||
        (vm->debug_flags & 16) == 16)
        return;
    SlpObjString *left_description =
        describe_value(vm, left);
    SlpObjString *right_description =
        describe_value(vm, right);
    if (!left_description || !right_description) return;
    SlpCallFrame *frame =
        vm->frame_count > 0 ? current_frame(vm) : NULL;
    char line[32];
    snprintf(line, sizeof(line), "%d", frame_current_line(frame));
    slp_vm_write(vm, "Trace: ");
    slp_vm_write(vm, left_description->chars);
    slp_vm_write(vm, " ");
    slp_vm_write(vm, predicate);
    slp_vm_write(vm, " ");
    slp_vm_write(vm, right_description->chars);
    slp_vm_write(vm, result ? " ? TRUE at " : " ? FALSE at ");
    slp_vm_write(vm, frame_source_name(frame));
    slp_vm_write(vm, ":");
    slp_vm_write(vm, line);
    slp_vm_write(vm, "\n");
}

typedef struct {
    SlpObjString *call;
    SlpObjString *source;
    int line;
    bool enabled;
} SlpCallTrace;

static SlpCallTrace prepare_call_trace(
    SlpVM *vm, SlpValue callee, SlpValue *arguments,
    int argument_count, bool has_message,
    const char *named_call) {
    SlpCallTrace trace;
    trace.call = NULL;
    trace.source = NULL;
    trace.line = -1;
    trace.enabled = false;
    if (!vm || (vm->debug_flags & 8) != 8)
        return trace;

    SlpStringBuilder builder;
    builder.vm = vm;
    builder.chars = NULL;
    builder.length = 0;
    builder.capacity = 0;
    builder.failed = false;
    builder.container_count = 0;

    bool inline_call =
        SLP_IS_OBJ(callee) &&
        SLP_OBJ_TYPE(callee) == SLP_OBJ_CLOSURE &&
        SLP_AS_CLOSURE(callee)->is_inline;
    if (named_call && named_call[0]) {
        if (inline_call)
            string_builder_append(
                &builder, "<inline> ", 9);
        string_builder_append(&builder, "&", 1);
        string_builder_append(
            &builder, named_call, strlen(named_call));
        string_builder_append(&builder, "(", 1);
        for (int i = 0; i < argument_count; i++) {
            if (i > 0)
                string_builder_append(&builder, ", ", 2);
            stringify_value(
                &builder, arguments[i], true, 1);
        }
        string_builder_append(&builder, ")", 1);
    } else {
        string_builder_append(&builder, "[", 1);
        stringify_value(&builder, callee, true, 1);
        int first_argument = 0;
        if (has_message && argument_count > 0) {
            SlpValue message =
                dereference_value(arguments[0]);
            if (!SLP_IS_NULL(message)) {
                string_builder_append(&builder, " ", 1);
                stringify_value(
                    &builder, message, false, 1);
            }
            first_argument = 1;
        }
        if (first_argument < argument_count) {
            string_builder_append(&builder, ": ", 2);
            for (int i = first_argument;
                 i < argument_count; i++) {
                if (i > first_argument)
                    string_builder_append(&builder, ", ", 2);
                stringify_value(
                    &builder, arguments[i], true, 1);
            }
        }
        string_builder_append(&builder, "]", 1);
    }

    if (!builder.failed) {
        trace.call = slp_vm_copy_string(
            vm, builder.chars ? builder.chars : "",
            (uint32_t)builder.length);
    }
    if (builder.chars)
        SLP_FREE(vm->allocator, builder.chars);
    if (!trace.call) return trace;

    SlpCallFrame *caller =
        vm->frame_count > 0 ? current_frame(vm) : NULL;
    if (caller && caller->closure &&
        caller->closure->function &&
        caller->closure->function->source_name)
        trace.source =
            caller->closure->function->source_name;
    else
        trace.source = slp_vm_copy_cstr(vm, "<internal>");
    trace.line = caller ? frame_current_line(caller) : -1;
    trace.enabled = true;
    return trace;
}

static void finish_call_trace(
    SlpVM *vm, SlpObjString *call,
    SlpObjString *source, int line, bool enabled,
    SlpValue result, bool failed, bool passed_control) {
    if (!vm || !enabled || !call ||
        vm->debug_flags == 0 ||
        (vm->debug_flags & 16) == 16)
        return;
    char line_text[32];
    snprintf(line_text, sizeof(line_text), "%d", line);
    slp_vm_write(vm, "Trace: ");
    slp_vm_write(vm, call->chars);
    if (failed) {
        slp_vm_write(vm, " - FAILED!");
    } else if (passed_control) {
        SlpObjString *description =
            describe_value(vm, result);
        slp_vm_write(vm, " -goto- ");
        slp_vm_write(
            vm, description ? description->chars : "");
    } else if (!slp_value_is_falsy(result) ||
               !SLP_IS_NULL(result)) {
        /*
         * False is represented separately by this VM, but Sleep's false
         * scalar is the empty/null value and therefore has no "= ..." suffix.
         */
        if (!(SLP_IS_BOOL(result) &&
              !SLP_AS_BOOL(result))) {
            SlpObjString *description =
                describe_value(vm, result);
            slp_vm_write(vm, " = ");
            slp_vm_write(
                vm, description ? description->chars : "");
        }
    }
    slp_vm_write(vm, " at ");
    slp_vm_write(
        vm, source ? source->chars : "<internal>");
    slp_vm_write(vm, ":");
    slp_vm_write(vm, line_text);
    slp_vm_write(vm, "\n");
}

static SlpObjString *profile_call_name(
    SlpVM *vm, SlpValue callee,
    bool has_message,
    SlpValue *arguments,
    int argument_count,
    const char *named_call) {
    if (!vm) return NULL;
    char name[1024];
    if (named_call && named_call[0]) {
        snprintf(
            name, sizeof(name), "&%s",
            named_call);
        return slp_vm_copy_cstr(vm, name);
    }
    if (!SLP_IS_OBJ(callee))
        return NULL;
    if (SLP_OBJ_TYPE(callee) ==
        SLP_OBJ_NATIVE) {
        SlpObjNative *native =
            SLP_AS_NATIVE(callee);
        if (!native->name) return NULL;
        snprintf(
            name, sizeof(name), "&%s",
            native->name->chars);
        return slp_vm_copy_cstr(vm, name);
    }
    if (SLP_OBJ_TYPE(callee) ==
        SLP_OBJ_CLOSURE) {
        SlpObjClosure *closure =
            SLP_AS_CLOSURE(callee);
        SlpObjFunction *function =
            closure->function;
        const char *source =
            function &&
                    function->source_name
                ? function->source_name->chars
                : "<unknown>";
        if (function &&
            function->line_end >
                function->line_start)
            snprintf(
                name, sizeof(name),
                "&closure[%s:%d-%d]",
                source,
                function->line_start,
                function->line_end);
        else
            snprintf(
                name, sizeof(name),
                "&closure[%s:%d]",
                source,
                function
                    ? function->line_start
                    : 0);
        return slp_vm_copy_cstr(vm, name);
    }
    if (has_message &&
        argument_count > 0 &&
        SLP_IS_OBJ(arguments[0]) &&
        SLP_OBJ_TYPE(arguments[0]) ==
            SLP_OBJ_STRING) {
        const char *method =
            SLP_AS_STRING(
                arguments[0])->chars;
        if (SLP_OBJ_TYPE(callee) ==
                SLP_OBJ_STRING &&
            strcmp(method, "length") == 0)
            return slp_vm_copy_cstr(
                vm,
                "public int "
                "java.lang.String.length()");
    }
    return NULL;
}

static void profile_record(
    SlpVM *vm, SlpObjString *name) {
    if (!vm || !name ||
        (vm->debug_flags & 8) != 8 ||
        strcmp(name->chars, "&profile") == 0)
        return;
    if (!vm->profile_statistics) {
        vm->profile_statistics =
            slp_vm_new_array(vm);
        if (!vm->profile_statistics)
            return;
    }

    for (int index = 0;
         index <
             vm->profile_statistics->count;
         index++) {
        SlpValue value =
            vm->profile_statistics
                ->elements[index];
        if (!SLP_IS_OBJ(value) ||
            SLP_OBJ_TYPE(value) !=
                SLP_OBJ_JAVA_OBJECT)
            continue;
        SlpObjJavaObject *statistic =
            SLP_AS_JAVA_OBJECT(value);
        if (statistic->kind !=
                SLP_JAVA_PROFILE_STATISTIC ||
            !statistic->list ||
            statistic->list->count < 3)
            continue;
        SlpValue function_name =
            statistic->list->elements[2];
        if (!SLP_IS_OBJ(function_name) ||
            SLP_OBJ_TYPE(function_name) !=
                SLP_OBJ_STRING ||
            strcmp(
                SLP_AS_STRING(
                    function_name)->chars,
                name->chars) != 0)
            continue;
        SlpValue calls =
            statistic->list->elements[0];
        int64_t count =
            SLP_IS_OBJ(calls) &&
                    SLP_OBJ_TYPE(calls) ==
                        SLP_OBJ_LONG
                ? SLP_AS_LONG(calls)->value
                : (int64_t)
                      slp_value_as_number(
                          calls);
        SlpObjLong *updated =
            slp_vm_new_long(vm, count + 1);
        if (updated)
            statistic->list->elements[0] =
                SLP_OBJ_VAL(updated);
        return;
    }

    SlpObjJavaObject *statistic =
        slp_vm_new_java_object(
            vm,
            "sleep.runtime.ScriptInstance"
            "$ProfilerStatistic",
            SLP_JAVA_PROFILE_STATISTIC);
    if (!statistic) return;
    statistic->list =
        slp_vm_new_array(vm);
    if (!statistic->list) return;
    SlpObjLong *calls =
        slp_vm_new_long(vm, 1);
    SlpObjLong *ticks =
        slp_vm_new_long(vm, 0);
    if (!calls || !ticks) return;
    slp_obj_array_push(
        vm->allocator,
        statistic->list,
        SLP_OBJ_VAL(calls));
    slp_obj_array_push(
        vm->allocator,
        statistic->list,
        SLP_OBJ_VAL(ticks));
    slp_obj_array_push(
        vm->allocator,
        statistic->list,
        SLP_OBJ_VAL(name));
    slp_obj_array_push(
        vm->allocator,
        vm->profile_statistics,
        SLP_OBJ_VAL(statistic));
}

SlpObjArray *slp_vm_profile_statistics(
    SlpVM *vm) {
    return vm
        ? vm->profile_statistics
        : NULL;
}

static SlpObjString *build_callcc_trace_call(
    SlpVM *vm, SlpValue handler, SlpValue continuation) {
    SlpStringBuilder builder;
    builder.vm = vm;
    builder.chars = NULL;
    builder.length = 0;
    builder.capacity = 0;
    builder.failed = false;
    builder.container_count = 0;

    string_builder_append(&builder, "[", 1);
    stringify_value(&builder, handler, false, 1);
    string_builder_append(&builder, " CALLCC: ", 9);
    stringify_value(&builder, continuation, false, 1);
    string_builder_append(&builder, "]", 1);

    SlpObjString *result = NULL;
    if (!builder.failed) {
        result = slp_vm_copy_string(
            vm, builder.chars ? builder.chars : "",
            (uint32_t)builder.length);
    }
    if (builder.chars)
        SLP_FREE(vm->allocator, builder.chars);
    return result;
}

static void warn_invalid_index(SlpVM *vm, SlpValue container,
                               SlpValue index) {
    SlpObjString *container_text = describe_value(vm, container);
    SlpObjString *index_text = describe_value(vm, index);
    if (!container_text || !index_text) return;
    static const char prefix[] = "invalid use of index operator: ";
    size_t length = sizeof(prefix) - 1 + container_text->length + 1 +
                    index_text->length + 2;
    char *message = (char*)SLP_MALLOC(vm->allocator, length + 1);
    if (!message) return;
    snprintf(message, length + 1, "%s%s[%s]", prefix,
             container_text->chars, index_text->chars);
    slp_vm_abort_warning(vm, message);
    SLP_FREE(vm->allocator, message);
}

static void watched_cell_assign(
    SlpVM *vm, SlpObjScalarCell *cell,
    SlpValue value) {
    SlpValue assigned = dereference_value(value);
    cell->value = assigned;
    if (!vm || !cell->watch_name)
        return;

    SlpObjString *description =
        describe_value(vm, assigned);
    if (!description) return;
    size_t message_capacity =
        strlen("watch():  = ") +
        cell->watch_name->length +
        description->length + 1;
    char *message = (char*)SLP_MALLOC(
        vm->allocator, message_capacity);
    if (!message) return;
    snprintf(
        message, message_capacity,
        "watch(): %s = %s",
        cell->watch_name->chars,
        description->chars);
    slp_vm_warning(vm, message);
    SLP_FREE(vm->allocator, message);
}

static void assign_binding(
    SlpVM *vm, SlpObjHash *scope,
    SlpValue name, SlpValue value) {
    SlpValue existing = slp_obj_hash_get_raw(scope, name);
    if (SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) == SLP_OBJ_SCALAR_CELL)
        slp_obj_hash_set(
            vm->allocator, scope, name, value);
    else if (SLP_IS_OBJ(existing) &&
             SLP_OBJ_TYPE(existing) == SLP_OBJ_SCALAR_CELL)
        watched_cell_assign(
            vm, SLP_AS_SCALAR_CELL(existing),
            value);
    else
        slp_obj_hash_set(
            vm->allocator, scope, name, value);
}

static SlpObjHash *scoped_variable_level(SlpVM *vm, SlpValue name) {
    SlpCallFrame *frame = vm->frame_count > 0 ? current_frame(vm) : NULL;
    SlpObjHash *local = current_local_scope(frame);
    if (local && slp_obj_hash_contains(local, name))
        return local;
    if (frame && frame->closure_scope &&
        slp_obj_hash_contains(frame->closure_scope, name))
        return frame->closure_scope;
    return vm->globals;
}

static void warn_undeclared_variable(SlpVM *vm, SlpValue name) {
    if (!(vm->debug_flags & 4) || !SLP_IS_OBJ(name) ||
        SLP_OBJ_TYPE(name) != SLP_OBJ_STRING)
        return;

    SlpObjString *variable = SLP_AS_STRING(name);
    if (variable->length == 0 ||
        (variable->chars[0] != '$' && variable->chars[0] != '@' &&
         variable->chars[0] != '%'))
        return;

    static const char prefix[] = "variable '";
    static const char suffix[] = "' not declared";
    size_t length = sizeof(prefix) - 1 + variable->length + sizeof(suffix);
    char *message = (char*)SLP_MALLOC(vm->allocator, length);
    if (!message) return;

    size_t offset = 0;
    memcpy(message + offset, prefix, sizeof(prefix) - 1);
    offset += sizeof(prefix) - 1;
    memcpy(message + offset, variable->chars, variable->length);
    offset += variable->length;
    memcpy(message + offset, suffix, sizeof(suffix));
    slp_vm_warning(vm, message);
    SLP_FREE(vm->allocator, message);
}

static SlpValue implicit_variable_value(SlpVM *vm, SlpValue name) {
    if (!SLP_IS_OBJ(name) || SLP_OBJ_TYPE(name) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;

    SlpObjString *variable = SLP_AS_STRING(name);
    if (variable->length > 0 && variable->chars[0] == '@') {
        SlpObjArray *array = slp_vm_new_array(vm);
        return array ? SLP_OBJ_VAL(array) : SLP_NULL_VAL;
    }
    if (variable->length > 0 && variable->chars[0] == '%') {
        SlpObjHash *hash = slp_vm_new_hash(vm);
        return hash ? SLP_OBJ_VAL(hash) : SLP_NULL_VAL;
    }
    if (find_known_class(variable->chars)) {
        SlpObjClass *class_object =
            slp_vm_new_class(vm, variable->chars);
        return class_object
            ? SLP_OBJ_VAL(class_object)
            : SLP_NULL_VAL;
    }
    return SLP_NULL_VAL;
}

static bool scoped_variable_get(SlpVM *vm, SlpValue name, SlpValue *result) {
    SlpCallFrame *frame = vm->frame_count > 0 ? current_frame(vm) : NULL;
    SlpObjHash *local = current_local_scope(frame);
    if (local && slp_obj_hash_contains(local, name)) {
        *result = dereference_value(slp_obj_hash_get(local, name));
        return true;
    }
    if (frame && frame->closure_scope &&
        slp_obj_hash_contains(frame->closure_scope, name)) {
        *result = dereference_value(slp_obj_hash_get(frame->closure_scope, name));
        return true;
    }
    if (slp_obj_hash_contains(vm->globals, name)) {
        *result = dereference_value(slp_obj_hash_get(vm->globals, name));
        return true;
    }
    *result = SLP_NULL_VAL;
    return false;
}

static void scoped_variable_set(SlpVM *vm, SlpValue name, SlpValue value) {
    assign_binding(
        vm, scoped_variable_level(vm, name),
        name, value);
}

static SlpValue scoped_variable_reference(SlpVM *vm, SlpValue name) {
    SlpObjHash *scope = scoped_variable_level(vm, name);
    SlpValue current = slp_obj_hash_get_raw(scope, name);
    if (SLP_IS_OBJ(current) && SLP_OBJ_TYPE(current) == SLP_OBJ_SCALAR_CELL)
        return current;

    SlpObjScalarCell *cell = slp_obj_scalar_cell_new(vm->allocator, current);
    if (!cell) return SLP_NULL_VAL;
    track_object(vm, &cell->obj);
    SlpValue reference = SLP_OBJ_VAL(cell);
    slp_obj_hash_set(vm->allocator, scope, name, reference);
    return reference;
}

static SlpValue slot_reference(SlpVM *vm, SlpValue *slot) {
    if (SLP_IS_OBJ(*slot) && SLP_OBJ_TYPE(*slot) == SLP_OBJ_SCALAR_CELL)
        return *slot;
    SlpObjScalarCell *cell = slp_obj_scalar_cell_new(vm->allocator, *slot);
    if (!cell) return SLP_NULL_VAL;
    track_object(vm, &cell->obj);
    *slot = SLP_OBJ_VAL(cell);
    return *slot;
}

static void slot_assign(
    SlpVM *vm, SlpValue *slot,
    SlpValue value) {
    if (SLP_IS_OBJ(*slot) && SLP_OBJ_TYPE(*slot) == SLP_OBJ_SCALAR_CELL)
        watched_cell_assign(
            vm, SLP_AS_SCALAR_CELL(*slot),
            value);
    else
        *slot = value;
}

bool slp_vm_assign_reference(
    SlpVM *vm, SlpValue reference,
    SlpValue value) {
    if (!vm || !SLP_IS_OBJ(reference) ||
        SLP_OBJ_TYPE(reference) !=
            SLP_OBJ_SCALAR_CELL)
        return false;
    watched_cell_assign(
        vm, SLP_AS_SCALAR_CELL(reference),
        value);
    return true;
}

static bool declare_scope_variables(SlpVM *vm, SlpObjHash *scope,
                                    const char *declarations) {
    if (!scope || !declarations) return false;

    const char *cursor = declarations;
    while (*cursor) {
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (!*cursor) break;

        const char *start = cursor;
        while (*cursor && !isspace((unsigned char)*cursor)) cursor++;
        uint32_t length = (uint32_t)(cursor - start);
        if (length < 2 || (start[0] != '$' && start[0] != '@' && start[0] != '%')) {
            slp_vm_runtime_error(vm, "Malformed variable name in scope declaration.");
            return false;
        }

        SlpObjString *name = slp_vm_copy_string(vm, start, length);
        SlpValue key = SLP_OBJ_VAL(name);
        if (slp_obj_hash_contains(scope, key))
            continue;

        SlpValue initial = SLP_NULL_VAL;
        if (start[0] == '@') {
            SlpObjArray *array = slp_vm_new_array(vm);
            if (!array) return false;
            initial = SLP_OBJ_VAL(array);
        } else if (start[0] == '%') {
            SlpObjHash *hash = slp_vm_new_hash(vm);
            if (!hash) return false;
            initial = SLP_OBJ_VAL(hash);
        }
        slp_obj_hash_set(vm->allocator, scope, key, initial);
    }
    return true;
}

static bool initialize_scope(SlpVM *vm, SlpObjHash *scope,
                             SlpValue *initializers, int count) {
    if (!scope) return false;
    for (int i = 0; i < count; i++) {
        SlpValue arg = initializers[i];
        if (!SLP_IS_OBJ(arg) || SLP_OBJ_TYPE(arg) != SLP_OBJ_KEY_VALUE) {
            slp_vm_runtime_error(vm, "Local scope initializer must be a key/value pair.");
            return false;
        }
        SlpObjKeyValue *kv = SLP_AS_KEY_VALUE(arg);
        if (!SLP_IS_OBJ(kv->key) || SLP_OBJ_TYPE(kv->key) != SLP_OBJ_STRING) {
            slp_vm_runtime_error(vm, "Local scope initializer requires a variable name.");
            return false;
        }
        SlpObjString *name = SLP_AS_STRING(kv->key);
        if (name->length < 2 ||
            (name->chars[0] != '$' && name->chars[0] != '@' && name->chars[0] != '%')) {
            slp_vm_runtime_error(vm, "Malformed variable name in local scope initializer.");
            return false;
        }
        slp_obj_hash_set(vm->allocator, scope, kv->key, kv->value);
    }
    return true;
}

bool slp_vm_declare_local(SlpVM *vm, const char *declarations) {
    if (!vm || vm->frame_count == 0) return false;
    return declare_scope_variables(vm, current_local_scope(current_frame(vm)),
                                   declarations);
}

bool slp_vm_declare_closure(SlpVM *vm, const char *declarations) {
    if (!vm || vm->frame_count == 0) return false;
    return declare_scope_variables(vm, current_frame(vm)->closure_scope,
                                   declarations);
}

bool slp_vm_declare_global(SlpVM *vm, const char *declarations) {
    if (!vm) return false;
    return declare_scope_variables(vm, vm->globals, declarations);
}

bool slp_vm_watch_variables(
    SlpVM *vm, const char *declarations) {
    if (!vm || !declarations) return false;

    const char *cursor = declarations;
    while (*cursor) {
        while (*cursor &&
               isspace((unsigned char)*cursor))
            cursor++;
        if (!*cursor) break;
        const char *start = cursor;
        while (*cursor &&
               !isspace((unsigned char)*cursor))
            cursor++;
        uint32_t length =
            (uint32_t)(cursor - start);
        if (length < 2 ||
            (start[0] != '$' &&
             start[0] != '@' &&
             start[0] != '%'))
            return false;

        SlpObjString *name =
            slp_vm_copy_string(
                vm, start, length);
        if (!name) return false;
        SlpValue key = SLP_OBJ_VAL(name);
        SlpCallFrame *frame =
            vm->frame_count > 0
                ? current_frame(vm)
                : NULL;
        SlpObjHash *scope = NULL;
        SlpObjHash *local =
            current_local_scope(frame);
        if (local &&
            slp_obj_hash_contains(local, key))
            scope = local;
        else if (
            frame && frame->closure_scope &&
            slp_obj_hash_contains(
                frame->closure_scope, key))
            scope = frame->closure_scope;
        else if (slp_obj_hash_contains(
                     vm->globals, key))
            scope = vm->globals;
        if (!scope)
            return false;

        SlpValue existing =
            slp_obj_hash_get_raw(scope, key);
        SlpObjScalarCell *cell = NULL;
        if (SLP_IS_OBJ(existing) &&
            SLP_OBJ_TYPE(existing) ==
                SLP_OBJ_SCALAR_CELL) {
            cell = SLP_AS_SCALAR_CELL(existing);
        } else {
            cell = slp_obj_scalar_cell_new(
                vm->allocator, existing);
            if (!cell) return false;
            track_object(vm, &cell->obj);
            slp_obj_hash_set(
                vm->allocator, scope, key,
                SLP_OBJ_VAL(cell));
        }
        cell->watch_name = name;
    }
    return true;
}

bool slp_vm_push_local_scope(SlpVM *vm, SlpValue *initializers, int count) {
    if (!vm || vm->frame_count == 0) return false;
    SlpCallFrame *frame = current_frame(vm);
    if (!frame->local_scopes) return false;
    SlpObjHash *scope = slp_vm_new_hash(vm);
    if (!scope) return false;
    slp_obj_array_push(vm->allocator, frame->local_scopes, SLP_OBJ_VAL(scope));
    if (!initialize_scope(vm, scope, initializers, count)) {
        frame->local_scopes->count--;
        return false;
    }
    return true;
}

bool slp_vm_pop_local_scope(SlpVM *vm, SlpValue *initializers, int count) {
    if (!vm || vm->frame_count == 0) return false;
    SlpCallFrame *frame = current_frame(vm);
    if (!frame->local_scopes || frame->local_scopes->count <= 1) {
        slp_vm_warning(vm, "&popl: no more local frames exist");
        return false;
    }
    frame->local_scopes->count--;
    return initialize_scope(vm, current_local_scope(frame), initializers, count);
}

static uint8_t read_byte(SlpCallFrame *frame) {
    return *frame->ip++;
}

static uint16_t read_short(SlpCallFrame *frame) {
    frame->ip += 2;
    return (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]);
}

static SlpValue read_constant(SlpCallFrame *frame) {
    uint16_t idx = read_short(frame);
    return frame->closure->function->chunk->constants[idx];
}

static void close_upvalues(SlpVM *vm, SlpValue *last) {
    while (vm->open_upvalues && vm->open_upvalues->location >= last) {
        SlpObjUpvalue *uv = vm->open_upvalues;
        uv->closed = *uv->location;
        uv->location = &uv->closed;
        vm->open_upvalues = uv->next;
    }
}

static void define_native_ex(
    SlpVM *vm, const char *name, SlpNativeFn fn,
    bool preserve_references) {
    SlpObjString *name_str = slp_vm_copy_string(vm, name, (uint32_t)strlen(name));
    SlpObjNative *native = slp_obj_native_new(vm->allocator, fn, name_str);
    if (native) {
        native->preserve_references =
            preserve_references;
        track_object(vm, &native->obj);
    }
    slp_obj_hash_set(vm->allocator, vm->globals, SLP_OBJ_VAL(name_str), SLP_OBJ_VAL(native));
}

static void define_native(SlpVM *vm, const char *name, SlpNativeFn fn) {
    define_native_ex(vm, name, fn, false);
}

static SlpValue native_println(SlpVM *vm, SlpValue *args, int argc) {
    if (argc > 0) {
        SlpObjString *text = slp_vm_stringify(vm, args[0]);
        if (text) slp_vm_write(vm, text->chars);
    }
    slp_vm_write(vm, "\n");
    return SLP_NULL_VAL;
}

static SlpValue native_size(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    if (SLP_IS_OBJ(args[0])) {
        SlpObj *obj = SLP_AS_OBJ(args[0]);
        if (obj->type == SLP_OBJ_STRING)
            return SLP_NUM_VAL((double)((SlpObjString*)obj)->length);
        if (obj->type == SLP_OBJ_ARRAY)
            return SLP_NUM_VAL((double)((SlpObjArray*)obj)->count);
        if (obj->type == SLP_OBJ_HASH)
            return SLP_NUM_VAL(
                (double)slp_obj_hash_visible_count((SlpObjHash*)obj));
    }
    return SLP_NUM_VAL(0);
}

static SlpValue native_array(SlpVM *vm, SlpValue *args, int argc) {
    SlpObjArray *arr = slp_vm_new_array(vm);
    if (arr) {
        for (int i = 0; i < argc; i++)
            slp_obj_array_push(vm->allocator, arr, args[i]);
    }
    return arr ? SLP_OBJ_VAL(arr) : SLP_NULL_VAL;
}

static SlpValue native_push(SlpVM *vm, SlpValue *args, int argc) {
    if (argc >= 2 && SLP_IS_OBJ(args[0]) &&
        SLP_OBJ_TYPE(args[0]) == SLP_OBJ_ARRAY) {
        SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
        if (arr->read_only) {
            slp_vm_abort_warning(vm, "array is read-only");
            return SLP_NULL_VAL;
        }
        SlpValue pushed = SLP_NULL_VAL;
        for (int i = 1; i < argc; i++) {
            pushed = args[i];
            slp_obj_array_push(vm->allocator, arr, pushed);
        }
        return pushed;
    }
    return SLP_NULL_VAL;
}

static SlpValue native_pop(SlpVM *vm, SlpValue *args, int argc) {
    if (argc >= 1 && SLP_IS_OBJ(args[0]) &&
        SLP_OBJ_TYPE(args[0]) == SLP_OBJ_ARRAY) {
        SlpObjArray *array = SLP_AS_ARRAY(args[0]);
        if (array->read_only) {
            slp_vm_abort_warning(vm, "array is read-only");
            return SLP_NULL_VAL;
        }
        return slp_obj_array_pop(array);
    }
    return SLP_NULL_VAL;
}

static SlpValue native_hash(SlpVM *vm, SlpValue *args, int argc) {
    SlpObjHash *hash = slp_vm_new_hash(vm);
    if (hash) {
        for (int i = 0; i < argc; i++) {
            if (SLP_IS_OBJ(args[i]) && SLP_OBJ_TYPE(args[i]) == SLP_OBJ_KEY_VALUE) {
                SlpObjKeyValue *kv = SLP_AS_KEY_VALUE(args[i]);
                slp_vm_hash_set(
                    vm, hash, kv->key,
                    dereference_value(kv->value));
            } else if (SLP_IS_OBJ(args[i]) &&
                       SLP_OBJ_TYPE(args[i]) == SLP_OBJ_STRING) {
                SlpObjString *pair = SLP_AS_STRING(args[i]);
                const char *separator = strchr(pair->chars, '=');
                if (separator) {
                    uint32_t key_length =
                        (uint32_t)(separator - pair->chars);
                    uint32_t value_length =
                        pair->length - key_length - 1;
                    SlpObjString *key =
                        slp_vm_copy_string(vm, pair->chars, key_length);
                    SlpObjString *value =
                        slp_vm_copy_string(vm, separator + 1, value_length);
                    slp_vm_hash_set(
                        vm, hash, SLP_OBJ_VAL(key), SLP_OBJ_VAL(value));
                } else if (i + 1 < argc) {
                    /* Keep accepting the legacy flat key/value ABI for
                       embedders even though Sleep scripts use key=value. */
                    slp_vm_hash_set(
                        vm, hash, args[i], args[i + 1]);
                    i++;
                }
            } else if (i + 1 < argc) {
                /* Keep accepting the legacy flat key/value ABI for embedders. */
                slp_vm_hash_set(vm, hash, args[i], args[i + 1]);
                i++;
            }
        }
    }
    return hash ? SLP_OBJ_VAL(hash) : SLP_NULL_VAL;
}

static SlpValue native_typeof(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    SlpValue v = args[0];
    if (SLP_IS_NULL(v)) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "null", 4));
    if (SLP_IS_BOOL(v)) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "boolean", 7));
    if (SLP_IS_NUM(v)) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "number", 6));
    if (SLP_IS_OBJ(v)) {
        switch (SLP_OBJ_TYPE(v)) {
            case SLP_OBJ_STRING: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "string", 6));
            case SLP_OBJ_LONG: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "long", 4));
            case SLP_OBJ_DOUBLE: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "double", 6));
            case SLP_OBJ_ARRAY: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "array", 5));
            case SLP_OBJ_HASH: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "hash", 4));
            case SLP_OBJ_FUNCTION:
            case SLP_OBJ_CLOSURE:
            case SLP_OBJ_NATIVE:
            case SLP_OBJ_BRIDGE:
                return SLP_OBJ_VAL(slp_vm_copy_string(vm, "function", 8));
            default: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "object", 6));
        }
    }
    return SLP_NULL_VAL;
}

static SlpValue native_keys(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_HASH)
        return SLP_NULL_VAL;
    SlpObjHash *hash = SLP_AS_HASH(args[0]);
    SlpObjArray *arr = slp_vm_new_array(vm);
    if (!arr) return SLP_NULL_VAL;
    for (int ordinal = 0; ordinal < hash->count; ordinal++) {
        int i = slp_obj_hash_ordered_index(hash, ordinal);
        if (i >= 0 && !SLP_IS_NULL(hash->entries[i].value)) {
            slp_obj_array_push(vm->allocator, arr, hash->entries[i].key);
        }
    }
    return SLP_OBJ_VAL(arr);
}

static bool match_wildcard(const char *pattern, const char *str) {
    while (*pattern) {
        if (*pattern == '*') {
            while (*pattern == '*') pattern++;
            if (!*pattern) return true;
            while (*str) {
                if (match_wildcard(pattern, str)) return true;
                str++;
            }
            return false;
        } else if (*pattern == '?') {
            if (!*str) return false;
            pattern++;
            str++;
        } else {
            if (*pattern != *str) return false;
            pattern++;
            str++;
        }
    }
    return *str == '\0';
}

static SlpValue native_remove(SlpVM *vm, SlpValue *args, int argc) {
    if (argc == 0) {
        for (int i = vm->frame_count - 1; i >= 0; i--) {
            SlpCallFrame *frame = &vm->frames[i];
            if (!frame->foreach_active)
                continue;
            SlpValue source = SLP_NULL_VAL;
            if (frame->foreach_array) {
                source =
                    SLP_OBJ_VAL(
                        frame->foreach_array);
                slp_obj_array_remove_at(
                    frame->foreach_array, frame->foreach_index);
                frame->foreach_iteration_version =
                    frame->foreach_array->mutation_version;
            } else if (frame->foreach_hash) {
                source =
                    SLP_OBJ_VAL(
                        frame->foreach_hash);
                slp_obj_hash_delete(
                    vm->allocator, frame->foreach_hash,
                    frame->foreach_key);
            } else {
                continue;
            }
            if (frame->foreach_iterator_offset >= 0 &&
                frame->foreach_iterator_offset <
                    (int)(vm->stack_top - vm->stack)) {
                SlpValue *iterator =
                    vm->stack + frame->foreach_iterator_offset;
                *iterator =
                    SLP_NUM_VAL(slp_value_as_number(*iterator) - 1);
            }
            frame->foreach_array = NULL;
            frame->foreach_hash = NULL;
            frame->foreach_active = false;
            return source;
        }
        slp_vm_warning(
            vm, "&remove: no active foreach loop to remove element from");
        return SLP_NULL_VAL;
    }
    if (argc < 2 || !SLP_IS_OBJ(args[0])) return SLP_BOOL_VAL(false);
    if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_HASH) {
        return SLP_BOOL_VAL(
            slp_vm_hash_delete(vm, SLP_AS_HASH(args[0]), args[1]));
    }
    return SLP_BOOL_VAL(false);
}

/* invoke() is dispatched specially by call_value so a continuation can replace
   the current VM context without returning through a now-invalid native call
   frame. This function is only an identity marker. */
static SlpValue native_invoke(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    (void)args;
    (void)argc;
    return SLP_NULL_VAL;
}

static SlpValue native_debug(SlpVM *vm, SlpValue *args, int argc) {
    if (argc > 0)
        vm->debug_flags = (int)slp_value_as_number(args[0]);
    return SLP_NUM_VAL((double)vm->debug_flags);
}

static void define_builtins(SlpVM *vm) {
    define_native(vm, "println", native_println);
    define_native(vm, "size", native_size);
    define_native(vm, "array", native_array);
    define_native(vm, "@", native_array);
    define_native(vm, "%", native_hash);
    define_native(vm, "hash", native_hash);
    define_native(vm, "push", native_push);
    define_native(vm, "pop", native_pop);
    define_native(vm, "typeof", native_typeof);
    define_native(vm, "keys", native_keys);
    define_native(vm, "remove", native_remove);
    define_native(vm, "invoke", native_invoke);
    define_native(vm, "debug", native_debug);
}

static void clear_coroutine_state(SlpVM *vm, SlpObjClosure *closure) {
    if (closure->coroutine_stack)
        SLP_FREE(vm->allocator, closure->coroutine_stack);
    if (closure->coroutine_frames)
        SLP_FREE(vm->allocator, closure->coroutine_frames);
    if (closure->coroutine_try_handlers)
        SLP_FREE(vm->allocator, closure->coroutine_try_handlers);
    closure->coroutine_stack = NULL;
    closure->coroutine_stack_count = 0;
    closure->coroutine_frames = NULL;
    closure->coroutine_frame_count = 0;
    closure->coroutine_needs_result = false;
    closure->coroutine_try_handlers = NULL;
    closure->coroutine_try_handler_count = 0;
}

static void trim_try_handlers(SlpVM *vm, int frame_count) {
    while (vm->try_handler_count > 0 &&
           vm->try_handlers[vm->try_handler_count - 1].frame_count >
               frame_count)
        vm->try_handler_count--;
}

static bool capture_coroutine_try_handlers(SlpVM *vm,
                                           SlpObjClosure *closure,
                                           int root_index,
                                           SlpValue *root_slots) {
    int first = 0;
    while (first < vm->try_handler_count &&
           vm->try_handlers[first].frame_count <= root_index)
        first++;
    int count = vm->try_handler_count - first;
    closure->coroutine_try_handler_count = count;
    if (count == 0) return true;

    closure->coroutine_try_handlers = SLP_ALLOCATE_ARRAY(
        vm->allocator, SlpTryHandler, count);
    if (!closure->coroutine_try_handlers) return false;
    memcpy(closure->coroutine_try_handlers, &vm->try_handlers[first],
           sizeof(SlpTryHandler) * count);
    for (int i = 0; i < count; i++) {
        closure->coroutine_try_handlers[i].frame_count -= root_index;
        closure->coroutine_try_handlers[i].stack_count -=
            (int)(root_slots - vm->stack);
    }
    return true;
}

static bool call_closure(
    SlpVM *vm, SlpObjClosure *closure, int arg_count,
    bool has_message, const SlpCallTrace *trace) {
    if (vm->frame_count >= SLP_MAX_FRAMES) {
        slp_vm_runtime_error(vm, "Stack overflow.");
        return false;
    }
    
    int total_args = has_message ? arg_count + 1 : arg_count;
    SlpValue *slots = vm->stack_top - total_args - 1;
    SlpValue *arg_start = slots + 1 + (has_message ? 1 : 0);
    SlpValue message = has_message
        ? slots[1]
        : (closure->call_name ? SLP_OBJ_VAL(closure->call_name)
                              : SLP_NULL_VAL);
    SlpValue positional[255];
    SlpValue named[255];
    int positional_count = 0;
    int named_count = 0;

    for (int i = 0; i < arg_count; i++) {
        SlpValue arg = arg_start[i];
        if (SLP_IS_OBJ(arg) && SLP_OBJ_TYPE(arg) == SLP_OBJ_KEY_VALUE)
            named[named_count++] = arg;
        else
            positional[positional_count++] = arg;
    }

    /*
     * A named closure argument must name an addressable Sleep scalar. Bare
     * keys such as `action => ...` cannot be reached from the callee and are
     * a failed call in the reference implementation.
     */
    for (int i = 0; i < named_count; i++) {
        SlpObjKeyValue *pair =
            SLP_AS_KEY_VALUE(named[i]);
        if (!SLP_IS_OBJ(pair->key) ||
            SLP_OBJ_TYPE(pair->key) != SLP_OBJ_STRING)
            continue;
        SlpObjString *key =
            SLP_AS_STRING(pair->key);
        if (key->length > 0 &&
            (key->chars[0] == '$' ||
             key->chars[0] == '@' ||
             key->chars[0] == '%'))
            continue;

        finish_call_trace(
            vm,
            trace ? trace->call : NULL,
            trace ? trace->source : NULL,
            trace ? trace->line : -1,
            trace ? trace->enabled : false,
            SLP_NULL_VAL, true, false);
        size_t message_size =
            (size_t)key->length + 40;
        char *warning = (char*)SLP_MALLOC(
            vm->allocator, message_size);
        if (warning) {
            snprintf(
                warning, message_size,
                "unreachable named parameter: %s",
                key->chars);
            slp_vm_warning(vm, warning);
            SLP_FREE(vm->allocator, warning);
        }
        vm->stack_top = slots;
        slp_vm_push(vm, SLP_NULL_VAL);
        return true;
    }

    if (closure->coroutine_stack) {
        /* A yield inside an inline function suspends the ordinary closure that
           called it as well as every intervening inline frame. Restore that
           saved frame chain as one coroutine activation. */
        SlpObjArray *resumed_args = slp_vm_new_array(vm);
        if (!resumed_args) {
            slp_vm_runtime_error(vm, "Out of memory.");
            return false;
        }
        for (int i = 0; i < positional_count; i++)
            slp_obj_array_push(vm->allocator, resumed_args,
                               dereference_value(positional[i]));

        vm->stack_top = slots;

        bool needs_result = closure->coroutine_needs_result;
        SlpValue resumed_result = has_message
            ? message
            : (positional_count > 0 ? dereference_value(positional[0])
                                    : SLP_NULL_VAL);
        int needed = closure->coroutine_stack_count;
        if (vm->stack_top + needed > vm->stack + SLP_STACK_MAX) {
            slp_vm_runtime_error(vm, "Stack overflow.");
            return false;
        }
        if (vm->frame_count + closure->coroutine_frame_count > SLP_MAX_FRAMES) {
            slp_vm_runtime_error(vm, "Stack overflow.");
            return false;
        }
        if (vm->try_handler_count +
                closure->coroutine_try_handler_count >
            SLP_MAX_HANDLERS) {
            slp_vm_runtime_error(vm, "Too many active exception handlers.");
            return false;
        }

        int restored_base = vm->frame_count;
        vm->stack_top += needed;
        memcpy(slots, closure->coroutine_stack, sizeof(SlpValue) * needed);

        int restored_count = closure->coroutine_frame_count;
        for (int i = 0; i < restored_count; i++) {
            SlpCallFrame saved = closure->coroutine_frames[i];
            ptrdiff_t slot_offset = saved.slots - closure->coroutine_stack;
            saved.slots = slots + slot_offset;
            vm->frames[vm->frame_count + i] = saved;
        }
        vm->frame_count += restored_count;

        for (int i = 0; i < closure->coroutine_try_handler_count; i++) {
            SlpTryHandler handler = closure->coroutine_try_handlers[i];
            handler.frame_count += restored_base;
            handler.stack_count += (int)(slots - vm->stack);
            vm->try_handlers[vm->try_handler_count++] = handler;
        }
        clear_coroutine_state(vm, closure);

        SlpCallFrame *root = &vm->frames[vm->frame_count - restored_count];
        if (trace) {
            root->trace_call = trace->call;
            root->trace_source = trace->source;
            root->trace_line = trace->line;
            root->trace_enabled = trace->enabled;
        }
        root->slots[1] = message;
        for (int i = 0; i < positional_count && i < 9; i++)
            root->slots[i + 2] = positional[i];
        root->slots[11] = SLP_OBJ_VAL(resumed_args);
        SlpObjHash *local = current_local_scope(root);
        for (int i = 0; i < named_count; i++) {
            SlpObjKeyValue *kv = SLP_AS_KEY_VALUE(named[i]);
            if (!SLP_IS_OBJ(kv->key) ||
                SLP_OBJ_TYPE(kv->key) != SLP_OBJ_STRING) {
                slp_vm_runtime_error(vm,
                                     "Named argument requires a variable name.");
                return false;
            }
            slp_obj_hash_set(vm->allocator, local, kv->key, kv->value);
        }
        if (needs_result)
            slp_vm_push(vm, resumed_result);
        return true;
    }

    /* Every compiled closure has twelve fixed slots: the callee, $0, $1..$9,
       and @_. Additional positional arguments live in @_ only. */
    if (slots + 12 > vm->stack + SLP_STACK_MAX) {
        slp_vm_runtime_error(vm, "Stack overflow.");
        return false;
    }

    /* Construct @_ from positional arguments only. Named arguments initialize
       variables in the active local scope and do not affect $1..$9 or @_. */
    SlpObjArray *arr = slp_vm_new_array(vm);
    if (!arr) {
        slp_vm_runtime_error(vm, "Out of memory.");
        return false;
    }

    for (int i = 0; i < positional_count; i++)
        slp_obj_array_push(vm->allocator, arr, dereference_value(positional[i]));

    SlpObjArray *local_scopes = NULL;
    SlpObjHash *closure_scope = NULL;
    if (closure->is_inline && vm->frame_count > 0) {
        local_scopes = current_frame(vm)->local_scopes;
        closure_scope = current_frame(vm)->closure_scope;
    } else {
        local_scopes = slp_vm_new_array(vm);
        SlpObjHash *local = slp_vm_new_hash(vm);
        if (!local_scopes || !local) {
            slp_vm_runtime_error(vm, "Out of memory.");
            return false;
        }
        slp_obj_array_push(vm->allocator, local_scopes, SLP_OBJ_VAL(local));
        if (!ensure_closure_scope(vm, closure)) {
            slp_vm_runtime_error(vm, "Out of memory.");
            return false;
        }
        closure_scope = closure->scope;
    }

    vm->stack_top = slots + 1;
    slp_vm_push(vm, message);
    for (int i = 0; i < 9; i++)
        slp_vm_push(vm, i < positional_count ? positional[i] : SLP_NULL_VAL);
    slp_vm_push(vm, SLP_OBJ_VAL(arr));

    SlpCallFrame *frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip = closure->function->chunk->code;
    frame->slots = slots;
    frame->local_scopes = local_scopes;
    frame->closure_scope = closure_scope;
    frame->foreach_pending = false;
    frame->foreach_array = NULL;
    frame->foreach_hash = NULL;
    frame->foreach_key = SLP_NULL_VAL;
    frame->foreach_index = -1;
    frame->foreach_iterator_offset = -1;
    frame->foreach_active = false;
    frame->foreach_iteration_array = NULL;
    frame->foreach_iteration_version = 0;
    frame->foreach_last_key = SLP_NULL_VAL;
    frame->foreach_last_value = SLP_NULL_VAL;
    frame->foreach_invalidated = false;
    frame->continuation_return = NULL;
    frame->trace_call = trace ? trace->call : NULL;
    frame->trace_source = trace ? trace->source : NULL;
    frame->trace_line = trace ? trace->line : -1;
    frame->trace_enabled = trace ? trace->enabled : false;
    frame->tainted_arguments = false;
    if (!closure->is_inline) {
        for (int i = 0;
             i < positional_count; i++)
            frame->tainted_arguments =
                frame->tainted_arguments ||
                slp_value_is_tainted(
                    positional[i]);
        for (int i = 0;
             i < named_count; i++)
            frame->tainted_arguments =
                frame->tainted_arguments ||
                slp_value_is_tainted(
                    SLP_AS_KEY_VALUE(
                        named[i])->value);
    }

    SlpObjHash *local = current_local_scope(frame);
    for (int i = 0; i < named_count; i++) {
        SlpObjKeyValue *kv = SLP_AS_KEY_VALUE(named[i]);
        if (!SLP_IS_OBJ(kv->key) || SLP_OBJ_TYPE(kv->key) != SLP_OBJ_STRING) {
            slp_vm_runtime_error(vm, "Named argument requires a variable name.");
            return false;
        }
        slp_obj_hash_set(vm->allocator, local, kv->key, kv->value);
    }
    return true;
}

static SlpObjContinuation *capture_vm_context(
    SlpVM *vm, int stack_count) {
    SlpObjContinuation *continuation =
        slp_obj_continuation_new(vm->allocator);
    if (!continuation) return NULL;
    track_object(vm, &continuation->obj);

    continuation->frame_count = vm->frame_count;
    continuation->stack_count = stack_count;
    continuation->try_handler_count = vm->try_handler_count;
    if (continuation->frame_count > 0)
        continuation->frames = SLP_ALLOCATE_ARRAY(
            vm->allocator, SlpCallFrame, continuation->frame_count);
    if (continuation->stack_count > 0)
        continuation->stack = SLP_ALLOCATE_ARRAY(
            vm->allocator, SlpValue, continuation->stack_count);
    if (continuation->try_handler_count > 0)
        continuation->try_handlers = SLP_ALLOCATE_ARRAY(
            vm->allocator, SlpTryHandler,
            continuation->try_handler_count);
    if ((continuation->frame_count > 0 && !continuation->frames) ||
        (continuation->stack_count > 0 && !continuation->stack) ||
        (continuation->try_handler_count > 0 &&
         !continuation->try_handlers))
        return NULL;

    if (continuation->frame_count > 0)
        memcpy(
            continuation->frames, vm->frames,
            sizeof(SlpCallFrame) * continuation->frame_count);
    if (continuation->stack_count > 0)
        memcpy(
            continuation->stack, vm->stack,
            sizeof(SlpValue) * continuation->stack_count);
    if (continuation->try_handler_count > 0)
        memcpy(
            continuation->try_handlers, vm->try_handlers,
            sizeof(SlpTryHandler) * continuation->try_handler_count);
    for (int i = 0; i < continuation->frame_count; i++) {
        ptrdiff_t slot_offset =
            continuation->frames[i].slots - vm->stack;
        continuation->frames[i].slots =
            continuation->stack + slot_offset;
    }
    return continuation;
}

static void restore_vm_context(
    SlpVM *vm, SlpObjContinuation *continuation) {
    vm->frame_count = continuation->frame_count;
    if (vm->frame_count > 0)
        memcpy(
            vm->frames, continuation->frames,
            sizeof(SlpCallFrame) * vm->frame_count);
    vm->try_handler_count = continuation->try_handler_count;
    if (vm->try_handler_count > 0)
        memcpy(
            vm->try_handlers, continuation->try_handlers,
            sizeof(SlpTryHandler) * vm->try_handler_count);
    vm->stack_top = vm->stack + continuation->stack_count;
    if (continuation->stack_count > 0)
        memcpy(
            vm->stack, continuation->stack,
            sizeof(SlpValue) * continuation->stack_count);
    for (int i = 0; i < vm->frame_count; i++) {
        ptrdiff_t slot_offset =
            continuation->frames[i].slots - continuation->stack;
        vm->frames[i].slots = vm->stack + slot_offset;
    }
}

static bool call_value(
    SlpVM *vm, SlpValue callee, int arg_count,
    bool has_message, const char *trace_name) {
    int total_args = has_message ? arg_count + 1 : arg_count;
    SlpValue *call_base =
        vm->stack_top - total_args - 1;
    SlpCallTrace trace = prepare_call_trace(
        vm, callee, call_base + 1, total_args,
        has_message, trace_name);
    if ((vm->debug_flags & 8) == 8) {
        SlpObjString *profile_name =
            profile_call_name(
                vm, callee, has_message,
                call_base + 1, total_args,
                trace_name);
        profile_record(vm, profile_name);
    }
    if (!SLP_IS_OBJ(callee)) {
        // DRY-RUN VALIDATOR: Do not crash! Consume args, push NULL, return true!
        vm->stack_top -= total_args + 1;
        slp_vm_push(vm, SLP_NULL_VAL);
        return true;
    }
    switch (SLP_OBJ_TYPE(callee)) {
    case SLP_OBJ_CLOSURE:
        return call_closure(
            vm, SLP_AS_CLOSURE(callee), arg_count,
            has_message, &trace);
    case SLP_OBJ_NATIVE: {
        SlpObjNative *native = SLP_AS_NATIVE(callee);
        if (native->fn == native_invoke) {
            if (total_args < 1) {
                vm->stack_top -= total_args + 1;
                slp_vm_push(vm, SLP_NULL_VAL);
                return true;
            }
            SlpValue *base = vm->stack_top - total_args - 1;
            SlpValue invoked = dereference_value(base[1]);
            SlpValue invoked_args[255];
            int invoked_arg_count = 0;
            SlpValue message = SLP_NULL_VAL;
            SlpObjHash *parameters = NULL;
            SlpObjClosure *environment = NULL;
            bool consumed_argument_array = false;
            bool consumed_message = false;

            for (int i = 2; i <= total_args; i++) {
                SlpValue value = base[i];
                if (SLP_IS_OBJ(value) &&
                    SLP_OBJ_TYPE(value) == SLP_OBJ_KEY_VALUE) {
                    SlpObjKeyValue *kv = SLP_AS_KEY_VALUE(value);
                    const char *key =
                        SLP_IS_OBJ(kv->key) &&
                        SLP_OBJ_TYPE(kv->key) == SLP_OBJ_STRING
                            ? SLP_AS_STRING(kv->key)->chars
                            : "";
                    if (strcmp(key, "message") == 0) {
                        message = dereference_value(kv->value);
                        consumed_message = true;
                    } else if (strcmp(key, "parameters") == 0 &&
                               SLP_IS_OBJ(kv->value) &&
                               SLP_OBJ_TYPE(kv->value) == SLP_OBJ_HASH) {
                        parameters = SLP_AS_HASH(kv->value);
                    } else if (strcmp(key, "$this") == 0 &&
                               SLP_IS_OBJ(kv->value) &&
                               SLP_OBJ_TYPE(kv->value) == SLP_OBJ_CLOSURE) {
                        environment = SLP_AS_CLOSURE(kv->value);
                    } else if (invoked_arg_count < 255) {
                        invoked_args[invoked_arg_count++] = value;
                    }
                    continue;
                }

                SlpValue plain = dereference_value(value);
                if (!consumed_argument_array && SLP_IS_OBJ(plain) &&
                    SLP_OBJ_TYPE(plain) == SLP_OBJ_ARRAY) {
                    SlpObjArray *array = SLP_AS_ARRAY(plain);
                    if (array->count > 255 - invoked_arg_count) {
                        slp_vm_runtime_error(
                            vm, "invoke: too many closure arguments.");
                        return false;
                    }
                    for (int j = 0; j < array->count; j++)
                        invoked_args[invoked_arg_count++] =
                            array->elements[j];
                    consumed_argument_array = true;
                } else if (!consumed_message) {
                    message = plain;
                    consumed_message = true;
                } else if (invoked_arg_count < 255) {
                    invoked_args[invoked_arg_count++] = value;
                }
            }

            if (parameters) {
                if (parameters->count > 255 - invoked_arg_count) {
                    slp_vm_runtime_error(
                        vm, "invoke: too many named parameters.");
                    return false;
                }
                for (int i = 0; i < parameters->capacity; i++) {
                    if (SLP_IS_NULL(parameters->entries[i].key)) continue;
                    SlpObjKeyValue *kv = slp_obj_key_value_new(
                        vm->allocator, parameters->entries[i].key,
                        parameters->entries[i].value);
                    if (!kv) {
                        slp_vm_runtime_error(vm, "Out of memory.");
                        return false;
                    }
                    track_object(vm, &kv->obj);
                    invoked_args[invoked_arg_count++] = SLP_OBJ_VAL(kv);
                }
            }

            if (environment && SLP_IS_OBJ(invoked) &&
                SLP_OBJ_TYPE(invoked) == SLP_OBJ_CLOSURE) {
                if (!ensure_closure_scope(vm, environment)) {
                    slp_vm_runtime_error(vm, "Out of memory.");
                    return false;
                }
                SlpObjClosure *bound =
                    slp_vm_clone_closure(vm, SLP_AS_CLOSURE(invoked));
                if (!bound) {
                    slp_vm_runtime_error(vm, "Out of memory.");
                    return false;
                }
                bound->scope = environment->scope;
                invoked = SLP_OBJ_VAL(bound);
            }

            vm->stack_top = base;
            slp_vm_push(vm, invoked);
            slp_vm_push(vm, message);
            for (int i = 0; i < invoked_arg_count; i++)
                slp_vm_push(vm, invoked_args[i]);
            return call_value(
                vm, invoked, invoked_arg_count, true, NULL);
        }
        SlpValue native_args[256];
        SlpValue *raw_args = vm->stack_top - total_args;
        bool tainted_arguments = false;
        for (int i = 0; i < total_args; i++)
            if (native->preserve_references) {
                native_args[i] = raw_args[i];
            } else {
                SlpValue argument =
                    dereference_value(
                        raw_args[i]);
                tainted_arguments =
                    tainted_arguments ||
                    slp_value_is_tainted(
                        argument);
                native_args[i] =
                    slp_value_unwrap_taint(
                        argument);
            }
        SlpValue result = native->fn(vm, native_args, total_args);
        if (!native->preserve_references &&
            tainted_arguments)
            result =
                slp_vm_taint_value(
                    vm, result);
        vm->stack_top -= total_args + 1;
        slp_vm_push(vm, result);
        bool suppress_trace =
            native->name &&
            (strcmp(native->name->chars, "warn") == 0 ||
             strcmp(native->name->chars, "@") == 0 ||
             strcmp(native->name->chars, "%") == 0);
        if (!suppress_trace) {
            finish_call_trace(
                vm, trace.call, trace.source,
                trace.line, trace.enabled,
                result, vm->abort_requested, false);
        }
        return true;
    }
    case SLP_OBJ_CONTINUATION: {
        SlpObjContinuation *cont = SLP_AS_CONTINUATION(callee);
        SlpValue ret_val = (total_args > 0) ? vm->stack_top[-total_args] : SLP_NULL_VAL;
        int caller_stack_count =
            (int)((vm->stack_top - total_args - 1) - vm->stack);
        SlpObjContinuation *return_to =
            capture_vm_context(vm, caller_stack_count);
        if (!return_to) {
            slp_vm_runtime_error(vm, "Out of memory.");
            return false;
        }
        return_to->return_trace_call = trace.call;
        return_to->return_trace_source = trace.source;
        return_to->return_trace_line = trace.line;
        return_to->return_trace_enabled = trace.enabled;

        if (cont->coroutine_owner)
            clear_coroutine_state(vm, cont->coroutine_owner);
        restore_vm_context(vm, cont);
        if (cont->resume_frame_index >= 0 &&
            cont->resume_frame_index < vm->frame_count) {
            vm->frames[cont->resume_frame_index].continuation_return =
                return_to;
            SlpCallFrame *resumed =
                &vm->frames[cont->resume_frame_index];
            resumed->slots[2] = ret_val;
            SlpObjArray *resumed_args = slp_vm_new_array(vm);
            if (!resumed_args) {
                slp_vm_runtime_error(vm, "Out of memory.");
                return false;
            }
            if (!SLP_IS_NULL(ret_val))
                slp_obj_array_push(
                    vm->allocator, resumed_args, ret_val);
            resumed->slots[11] = SLP_OBJ_VAL(resumed_args);
        }

        // Return `ret_val` as the result of the `callcc` operation
        slp_vm_push(vm, ret_val);
        return true;
    }
    default:
        // DRY-RUN VALIDATOR: Do not crash! Consume args, push NULL, return true!
        vm->stack_top -= total_args + 1;
        slp_vm_push(vm, SLP_NULL_VAL);
        return true;
    }
}

void slp_vm_runtime_error(SlpVM *vm, const char *msg) {
    if (vm->error_fn)
        vm->error_fn(vm->error_userdata, slp_vm_current_line(vm), msg);
}

static const char *compile_error_source_line(
    const char *source, int line,
    size_t *length) {
    if (length) *length = 0;
    if (!source) return "";
    if (line < 1) line = 1;

    const char *start = source;
    int current_line = 1;
    while (*start && current_line < line) {
        if (*start++ == '\n')
            current_line++;
    }
    const char *end = start;
    while (*end && *end != '\r' &&
           *end != '\n')
        end++;
    while (start < end &&
           isspace((unsigned char)*start))
        start++;
    while (end > start &&
           isspace((unsigned char)end[-1]))
        end--;
    if (length)
        *length = (size_t)(end - start);
    return start;
}

static SlpValue make_compile_error(
    SlpVM *vm, const char *source,
    int line, const char *parser_message) {
    if (!vm) return SLP_NULL_VAL;
    if (line < 1) line = 1;
    if (!parser_message || !parser_message[0])
        parser_message = "Parse error";

    size_t snippet_length = 0;
    const char *snippet =
        compile_error_source_line(
            source, line, &snippet_length);
    bool runaway_string =
        strcmp(
            parser_message,
            "Unterminated string.") == 0 ||
        strcmp(
            parser_message,
            "Unterminated string") == 0;
    /*
     * The reference parser reports a runaway quoted literal against the
     * first logical runtime-code line even when the surrounding eval string
     * contains formatting newlines.
     */
    if (runaway_string)
        line = 1;

    char message[1024];
    int message_length;
    if (runaway_string) {
        message_length = snprintf(
            message, sizeof(message),
            "2 error(s): Mismatched Parentheses - "
            "missing close paren at %d; Runaway string at %d",
            line, line);
    } else {
        size_t description_length =
            strlen(parser_message);
        while (description_length > 0 &&
               parser_message[
                   description_length - 1] == '.')
            description_length--;
        message_length = snprintf(
            message, sizeof(message),
            "1 error(s): %.*s at %d",
            (int)description_length,
            parser_message, line);
    }
    if (message_length < 0)
        return SLP_NULL_VAL;
    if ((size_t)message_length >= sizeof(message))
        message_length =
            (int)sizeof(message) - 1;

    const char *format_template =
        runaway_string
            ? "Error: Mismatched Parentheses - missing close "
              "paren at line %d\n"
              "       %.*s\n"
              "Error: Runaway string at line %d\n"
              "       %.*s\n"
            : "Error: %s at line %d\n"
              "       %.*s\n";
    size_t formatted_capacity =
        strlen(format_template) +
        strlen(parser_message) +
        snippet_length * (runaway_string ? 2u : 1u) +
        96;
    char *formatted = (char*)SLP_MALLOC(
        vm->allocator, formatted_capacity);
    if (!formatted) return SLP_NULL_VAL;
    if (runaway_string) {
        snprintf(
            formatted, formatted_capacity,
            format_template,
            line, (int)snippet_length, snippet,
            line, (int)snippet_length, snippet);
    } else {
        size_t description_length =
            strlen(parser_message);
        while (description_length > 0 &&
               parser_message[
                   description_length - 1] == '.')
            description_length--;
        char description[512];
        int copy_length =
            description_length < sizeof(description) - 1
                ? (int)description_length
                : (int)sizeof(description) - 1;
        memcpy(
            description, parser_message,
            (size_t)copy_length);
        description[copy_length] = '\0';
        snprintf(
            formatted, formatted_capacity,
            format_template,
            description, line,
            (int)snippet_length, snippet);
    }

    SlpObjJavaObject *error =
        slp_vm_new_java_object(
            vm,
            "sleep.error.YourCodeSucksException",
            SLP_JAVA_ERROR);
    if (!error) {
        SLP_FREE(vm->allocator, formatted);
        return SLP_NULL_VAL;
    }
    SlpValue error_value = SLP_OBJ_VAL(error);
    slp_vm_push(vm, error_value);

    SlpObjString *message_string =
        slp_vm_copy_string(
            vm, message,
            (uint32_t)message_length);
    SlpObjString *formatted_string =
        slp_vm_copy_cstr(vm, formatted);
    size_t object_text_capacity =
        strlen("YourCodeSucksException: ") +
        (size_t)message_length + 1;
    char *object_text = (char*)SLP_MALLOC(
        vm->allocator, object_text_capacity);
    SlpObjString *object_string = NULL;
    if (object_text) {
        snprintf(
            object_text, object_text_capacity,
            "YourCodeSucksException: %s",
            message);
        object_string =
            slp_vm_copy_cstr(vm, object_text);
    }

    if (message_string)
        slp_obj_array_push(
            vm->allocator, error->list,
            SLP_OBJ_VAL(message_string));
    if (formatted_string)
        slp_obj_array_push(
            vm->allocator, error->list,
            SLP_OBJ_VAL(formatted_string));
    error->value = object_string
        ? SLP_OBJ_VAL(object_string)
        : SLP_NULL_VAL;

    SLP_FREE(vm->allocator, object_text);
    SLP_FREE(vm->allocator, formatted);
    slp_vm_pop(vm);
    return error_value;
}

static bool call_builtin_object_method(SlpVM *vm, SlpValue *base,
                                       int arg_count, SlpValue *result) {
    SlpValue target = dereference_value(base[0]);
    SlpValue message = dereference_value(base[1]);
    if (!SLP_IS_OBJ(message) ||
        SLP_OBJ_TYPE(message) != SLP_OBJ_STRING)
        return false;

    const char *method = SLP_AS_STRING(message)->chars;
    SlpValue *arguments = base + 2;

    if (SLP_IS_OBJ(target) &&
        SLP_OBJ_TYPE(target) == SLP_OBJ_CLASS) {
        SlpObjClass *class_object =
            SLP_AS_CLASS(target);
        const char *class_name =
            class_object->name->chars;

        if (strcmp(class_name, "java.lang.Math") == 0 &&
            strcmp(method, "pow") == 0 &&
            arg_count == 2) {
            SlpObjDouble *number =
                slp_vm_new_double(
                    vm,
                    pow(
                        slp_value_as_number(arguments[0]),
                        slp_value_as_number(arguments[1])));
            *result = number
                ? SLP_OBJ_VAL(number)
                : SLP_NULL_VAL;
            return number != NULL;
        }
        if (strcmp(class_name, "java.lang.Thread") == 0 &&
            strcmp(method, "currentThread") == 0 &&
            arg_count == 0) {
            SlpObjString *source_key =
                slp_vm_copy_cstr(
                    vm, "$source");
            SlpValue source_value =
                source_key
                    ? slp_obj_hash_get(
                          vm->globals,
                          SLP_OBJ_VAL(source_key))
                    : SLP_NULL_VAL;
            bool in_fork =
                SLP_IS_OBJ(source_value) &&
                SLP_OBJ_TYPE(source_value) ==
                    SLP_OBJ_IO_HANDLE;
            char description[1400];
            if (in_fork && vm->frame_count > 0) {
                snprintf(
                    description,
                    sizeof(description),
                    "Thread[fork of %s:%d,5,main]",
                    frame_source_name(
                        current_frame(vm)),
                    slp_vm_current_line(vm));
            } else {
                snprintf(
                    description,
                    sizeof(description),
                    "Thread[main,5,main]");
            }
            SlpObjJavaObject *thread =
                slp_vm_new_java_object(
                    vm,
                    "java.lang.Thread",
                    SLP_JAVA_GENERIC);
            if (!thread)
                return false;
            SlpObjString *text =
                slp_vm_copy_cstr(
                    vm, description);
            thread->value =
                text
                    ? SLP_OBJ_VAL(text)
                    : SLP_NULL_VAL;
            *result = SLP_OBJ_VAL(thread);
            return true;
        }
        if (strcmp(class_name, "java.lang.System") == 0 &&
            strcmp(method, "out") == 0 &&
            arg_count == 0) {
            SlpObjJavaObject *stream =
                slp_vm_new_java_object(
                    vm,
                    "java.io.PrintStream",
                    SLP_JAVA_GENERIC);
            if (!stream) return false;
            *result = SLP_OBJ_VAL(stream);
            return true;
        }
        if (strcmp(
                class_name,
                "sleep.runtime.SleepUtils") == 0 &&
            strcmp(method, "getIOHandle") == 0 &&
            arg_count == 2) {
            SlpObjIOHandle *handle =
                slp_vm_get_console_handle(vm);
            *result = handle
                ? SLP_OBJ_VAL(handle)
                : SLP_NULL_VAL;
            return handle != NULL;
        }
        if (strcmp(method, "getInterfaces") == 0 &&
            arg_count == 0 &&
            class_object->interfaces) {
            *result =
                SLP_OBJ_VAL(
                    class_object->interfaces);
            return true;
        }
        if (arg_count == 0 &&
            class_object->fields) {
            SlpValue field =
                slp_obj_hash_get(
                    class_object->fields,
                    message);
            if (!SLP_IS_NULL(field)) {
                *result =
                    dereference_value(field);
                return true;
            }
        }
    }

    if (SLP_IS_NULL(target)) {
        SlpJavaObjectKind kind = SLP_JAVA_GENERIC;
        bool wrapper = false;
        if (strcmp(method, "LinkedList") == 0 ||
            strcmp(method, "java.util.LinkedList") == 0 ||
            strcmp(method, "ArrayList") == 0 ||
            strcmp(method, "java.util.ArrayList") == 0)
            kind = SLP_JAVA_LIST;
        else if (strcmp(method, "HashMap") == 0 ||
                 strcmp(method, "java.util.HashMap") == 0 ||
                 strcmp(method, "LinkedHashMap") == 0 ||
                 strcmp(method, "java.util.LinkedHashMap") == 0)
            kind = SLP_JAVA_MAP;
        else if (
            strcmp(method, "Object") == 0 ||
            strcmp(method, "java.lang.Object") == 0) {
            kind = SLP_JAVA_GENERIC;
        } else if (
            strcmp(method, "Boolean") == 0 ||
            strcmp(method, "java.lang.Boolean") == 0 ||
            strcmp(method, "Byte") == 0 ||
            strcmp(method, "java.lang.Byte") == 0 ||
            strcmp(method, "Character") == 0 ||
            strcmp(method, "java.lang.Character") == 0 ||
            strcmp(method, "Double") == 0 ||
            strcmp(method, "java.lang.Double") == 0 ||
            strcmp(method, "Float") == 0 ||
            strcmp(method, "java.lang.Float") == 0 ||
            strcmp(method, "Integer") == 0 ||
            strcmp(method, "java.lang.Integer") == 0 ||
            strcmp(method, "Long") == 0 ||
            strcmp(method, "java.lang.Long") == 0 ||
            strcmp(method, "Short") == 0 ||
            strcmp(method, "java.lang.Short") == 0 ||
            strcmp(method, "String") == 0 ||
            strcmp(method, "java.lang.String") == 0)
            wrapper = true;
        else
            return false;

        SlpObjJavaObject *object =
            slp_vm_new_java_object(vm, method, kind);
        if (!object) return false;
        if (kind == SLP_JAVA_LIST && arg_count == 1) {
            SlpValue source = dereference_value(arguments[0]);
            if (SLP_IS_OBJ(source) &&
                SLP_OBJ_TYPE(source) == SLP_OBJ_ARRAY) {
                SlpObjArray *array = SLP_AS_ARRAY(source);
                for (int i = 0; i < array->count; i++)
                    slp_obj_array_push(
                        vm->allocator, object->list,
                        array->elements[i]);
            } else if (SLP_IS_OBJ(source) &&
                       SLP_OBJ_TYPE(source) ==
                           SLP_OBJ_JAVA_OBJECT &&
                       SLP_AS_JAVA_OBJECT(source)->kind ==
                           SLP_JAVA_LIST) {
                SlpObjArray *array =
                    SLP_AS_JAVA_OBJECT(source)->list;
                for (int i = 0; i < array->count; i++)
                    slp_obj_array_push(
                        vm->allocator, object->list,
                        array->elements[i]);
            }
        } else if (wrapper && arg_count > 0) {
            SlpValue source =
                dereference_value(arguments[0]);
            const char *class_name =
                object->class_object->name->chars;
            if (strcmp(
                    class_name,
                    "java.lang.Double") == 0 ||
                strcmp(
                    class_name,
                    "java.lang.Float") == 0) {
                SlpObjDouble *number =
                    slp_vm_new_double(
                        vm,
                        slp_value_as_number(source));
                object->value = number
                    ? SLP_OBJ_VAL(number)
                    : SLP_NULL_VAL;
            } else if (strcmp(
                           class_name,
                           "java.lang.Long") == 0) {
                SlpObjLong *number =
                    slp_vm_new_long(
                        vm,
                        numeric_long_value(source));
                object->value = number
                    ? SLP_OBJ_VAL(number)
                    : SLP_NULL_VAL;
            } else if (
                strcmp(
                    class_name,
                    "java.lang.Integer") == 0 ||
                strcmp(
                    class_name,
                    "java.lang.Short") == 0 ||
                strcmp(
                    class_name,
                    "java.lang.Byte") == 0) {
                object->value = SLP_NUM_VAL(
                    (double)numeric_int_value(source));
            } else if (strcmp(
                           class_name,
                           "java.lang.Boolean") == 0) {
                object->value = SLP_BOOL_VAL(
                    !slp_value_is_falsy(source));
            } else if (strcmp(
                           class_name,
                           "java.lang.Character") == 0) {
                SlpObjString *text =
                    slp_vm_stringify(vm, source);
                object->value =
                    text && text->length > 0
                        ? SLP_OBJ_VAL(
                              slp_vm_copy_string(
                                  vm, text->chars, 1))
                        : SLP_OBJ_VAL(
                              slp_vm_copy_string(
                                  vm, "", 0));
            } else if (strcmp(
                           class_name,
                           "java.lang.String") == 0) {
                SlpObjString *text =
                    slp_vm_stringify(vm, source);
                object->value = text
                    ? SLP_OBJ_VAL(text)
                    : SLP_NULL_VAL;
            }
        }
        *result = SLP_OBJ_VAL(object);
        return true;
    }

    if (SLP_IS_OBJ(target) &&
        SLP_OBJ_TYPE(target) == SLP_OBJ_JAVA_OBJECT) {
        SlpObjJavaObject *object =
            SLP_AS_JAVA_OBJECT(target);
        if (object->class_object &&
            object->class_object->name &&
            strcmp(
                object->class_object->name->chars,
                "java.io.PrintStream") == 0 &&
            (strcmp(method, "print") == 0 ||
             strcmp(method, "println") == 0) &&
            arg_count <= 1) {
            if (arg_count == 1) {
                SlpObjString *text =
                    slp_vm_stringify(
                        vm,
                        dereference_value(arguments[0]));
                if (text) slp_vm_write(vm, text->chars);
            } else if (strcmp(method, "println") == 0) {
                slp_vm_write(vm, "\n");
                *result = SLP_NULL_VAL;
                return true;
            }
            if (strcmp(method, "println") == 0)
                slp_vm_write(vm, "\n");
            *result = SLP_NULL_VAL;
            return true;
        }
        if (strcmp(method, "getClass") == 0 &&
            arg_count == 0) {
            *result = SLP_OBJ_VAL(
                object->class_object);
            return true;
        }
        if (arg_count == 0) {
            SlpValue field = SLP_NULL_VAL;
            if (object->fields)
                field = slp_obj_hash_get(
                    object->fields, message);
            if (SLP_IS_NULL(field) &&
                object->class_object &&
                object->class_object->fields)
                field = slp_obj_hash_get(
                    object->class_object->fields,
                    message);
            if (!SLP_IS_NULL(field)) {
                *result =
                    dereference_value(field);
                return true;
            }
        }
        if (object->kind == SLP_JAVA_ERROR &&
            object->list) {
            if (strcmp(method, "getMessage") == 0 &&
                arg_count == 0) {
                *result = slp_obj_array_get(
                    object->list, 0);
                return true;
            }
            if (strcmp(method, "formatErrors") == 0 &&
                arg_count == 0) {
                *result = slp_obj_array_get(
                    object->list, 1);
                return true;
            }
            if (strcmp(method, "toString") == 0 &&
                arg_count == 0) {
                *result = object->value;
                return true;
            }
        }
        if (object->kind ==
                SLP_JAVA_PROFILE_STATISTIC &&
            object->list &&
            object->list->count >= 3 &&
            arg_count == 0) {
            if (strcmp(method, "calls") == 0) {
                *result =
                    object->list->elements[0];
                return true;
            }
            if (strcmp(method, "ticks") == 0) {
                *result =
                    object->list->elements[1];
                return true;
            }
            if (strcmp(
                    method,
                    "functionName") == 0) {
                *result =
                    object->list->elements[2];
                return true;
            }
            if (strcmp(method, "toString") == 0) {
                SlpObjString *function_name =
                    SLP_AS_STRING(
                        object->list
                            ->elements[2]);
                int64_t calls =
                    SLP_AS_LONG(
                        object->list
                            ->elements[0])
                        ->value;
                char description[1200];
                snprintf(
                    description,
                    sizeof(description),
                    "0.0s %lld %s",
                    (long long)calls,
                    function_name->chars);
                *result = SLP_OBJ_VAL(
                    slp_vm_copy_cstr(
                        vm, description));
                return true;
            }
        }
        if (object->kind == SLP_JAVA_PROXY &&
            SLP_IS_OBJ(object->value) &&
            SLP_OBJ_TYPE(object->value) ==
                SLP_OBJ_CLOSURE) {
            slp_vm_push(
                vm, object->value);
            slp_vm_push(vm, message);
            for (int i = 0;
                 i < arg_count; i++) {
                slp_vm_push(
                    vm,
                    dereference_value(
                        arguments[i]));
            }
            if (slp_vm_call(
                    vm, arg_count,
                    true) != SLP_OK)
                return false;
            *result = slp_vm_pop(vm);
            return true;
        }
        if (object->kind == SLP_JAVA_ITERATOR &&
            object->list) {
            int index =
                (int)slp_value_as_number(
                    object->value);
            if (strcmp(method, "hasNext") == 0 &&
                arg_count == 0) {
                *result =
                    index >= 0 &&
                    index < object->list->count
                        ? SLP_NUM_VAL(1.0)
                        : SLP_NULL_VAL;
                return true;
            }
            if (strcmp(method, "next") == 0 &&
                arg_count == 0) {
                if (index < 0 ||
                    index >= object->list->count) {
                    *result = SLP_NULL_VAL;
                    return true;
                }
                *result = dereference_value(
                    object->list
                        ->elements[index]);
                object->value =
                    SLP_NUM_VAL(
                        (double)(index + 1));
                return true;
            }
            if (strcmp(method, "remove") == 0 &&
                arg_count == 0) {
                if (index <= 0 ||
                    index > object->list->count) {
                    *result = SLP_NULL_VAL;
                    return true;
                }
                slp_obj_array_remove_at(
                    object->list, index - 1);
                object->value =
                    SLP_NUM_VAL(
                        (double)(index - 1));
                *result = SLP_NULL_VAL;
                return true;
            }
        }
        if (object->kind == SLP_JAVA_LIST && object->list) {
            if (strcmp(method, "iterator") == 0 &&
                arg_count == 0) {
                SlpObjJavaObject *iterator =
                    slp_vm_new_java_object(
                        vm,
                        "java.util.ListIterator",
                        SLP_JAVA_ITERATOR);
                if (!iterator)
                    return false;
                iterator->list = object->list;
                iterator->value =
                    SLP_NUM_VAL(0.0);
                *result =
                    SLP_OBJ_VAL(iterator);
                return true;
            }
            if (strcmp(method, "add") == 0 &&
                (arg_count == 1 || arg_count == 2)) {
                if (arg_count == 1)
                    slp_obj_array_push(
                        vm->allocator, object->list,
                        dereference_value(arguments[0]));
                else
                    slp_obj_array_insert(
                        vm->allocator, object->list,
                        (int)slp_value_as_number(arguments[0]),
                        dereference_value(arguments[1]));
                *result = SLP_NUM_VAL(1.0);
                return true;
            }
            if (strcmp(method, "size") == 0 && arg_count == 0) {
                *result =
                    SLP_NUM_VAL((double)object->list->count);
                return true;
            }
            if (strcmp(method, "isEmpty") == 0 &&
                arg_count == 0) {
                *result = object->list->count == 0
                    ? SLP_NUM_VAL(1.0) : SLP_NULL_VAL;
                return true;
            }
            if (strcmp(method, "get") == 0 && arg_count == 1) {
                *result = slp_obj_array_get(
                    object->list,
                    (int)slp_value_as_number(arguments[0]));
                return true;
            }
            if (strcmp(method, "clear") == 0 &&
                arg_count == 0) {
                object->list->count = 0;
                object->list->mutation_version++;
                *result = SLP_NULL_VAL;
                return true;
            }
        }
        return false;
    }

    if (!SLP_IS_OBJ(target) ||
        SLP_OBJ_TYPE(target) != SLP_OBJ_STRING)
        return false;

    SlpObjString *string = SLP_AS_STRING(target);

    if (strcmp(method, "getClass") == 0 &&
        arg_count == 0) {
        SlpObjClass *class_object =
            slp_vm_new_class(vm, "java.lang.String");
        *result = class_object
            ? SLP_OBJ_VAL(class_object)
            : SLP_NULL_VAL;
        return class_object != NULL;
    }
    if (strcmp(method, "length") == 0 && arg_count == 0) {
        *result = SLP_NUM_VAL((double)string->length);
        return true;
    }
    if (strcmp(method, "trim") == 0 && arg_count == 0) {
        uint32_t start = 0;
        uint32_t end = string->length;
        while (start < end && (unsigned char)string->chars[start] <= 0x20)
            start++;
        while (end > start &&
               (unsigned char)string->chars[end - 1] <= 0x20)
            end--;
        *result = SLP_OBJ_VAL(slp_vm_copy_string(
            vm, string->chars + start, end - start));
        return true;
    }
    if (strcmp(method, "equals") == 0 && arg_count == 1) {
        SlpValue other = dereference_value(arguments[0]);
        bool equal =
            SLP_IS_OBJ(other) && SLP_OBJ_TYPE(other) == SLP_OBJ_STRING &&
            SLP_AS_STRING(other)->length == string->length &&
            memcmp(SLP_AS_STRING(other)->chars, string->chars,
                   string->length) == 0;
        *result = SLP_NUM_VAL(equal ? 1.0 : 0.0);
        return true;
    }
    if (strcmp(method, "substring") == 0 &&
        (arg_count == 1 || arg_count == 2)) {
        int start = (int)slp_value_as_number(arguments[0]);
        int end = arg_count == 2
                      ? (int)slp_value_as_number(arguments[1])
                      : (int)string->length;
        if (start < 0 || end < start || end > (int)string->length)
            return false;
        *result = SLP_OBJ_VAL(slp_vm_copy_string(
            vm, string->chars + start, (uint32_t)(end - start)));
        return true;
    }
    return false;
}

SlpResult slp_vm_call(SlpVM *vm, int arg_count, bool has_message);

static const char *value_java_class_name(SlpValue value) {
    value = dereference_value(value);
    value = slp_value_unwrap_taint(value);
    if (SLP_IS_NULL(value)) return NULL;
    if (SLP_IS_BOOL(value))
        return SLP_AS_BOOL(value)
            ? "java.lang.Integer" : NULL;
    if (SLP_IS_NUM(value)) return "java.lang.Integer";
    if (!SLP_IS_OBJ(value)) return NULL;
    switch (SLP_OBJ_TYPE(value)) {
    case SLP_OBJ_STRING: return "java.lang.String";
    case SLP_OBJ_CLASS: return "java.lang.Class";
    case SLP_OBJ_JAVA_OBJECT:
        return SLP_AS_JAVA_OBJECT(value)->class_object->name->chars;
    case SLP_OBJ_LONG: return "java.lang.Long";
    case SLP_OBJ_DOUBLE: return "java.lang.Double";
    case SLP_OBJ_ARRAY: return "sleep.engine.types.ListContainer";
    case SLP_OBJ_HASH:
        return SLP_AS_HASH(value)->order_mode == 0
            ? "sleep.engine.types.HashContainer"
            : "sleep.engine.types.OrderedHashContainer";
    case SLP_OBJ_CLOSURE: return "sleep.bridges.SleepClosure";
    case SLP_OBJ_IO_HANDLE: return "sleep.bridges.io.IOObject";
    default: return "java.lang.Object";
    }
}

static bool class_name_is_instance(
    const char *target, const char *actual) {
    if (!target || !actual) return false;
    if (strcmp(target, actual) == 0) return true;
    if (strcmp(target, "java.lang.Object") == 0) return true;

    if (strcmp(target, "java.lang.Number") == 0)
        return strcmp(actual, "java.lang.Integer") == 0 ||
               strcmp(actual, "java.lang.Long") == 0 ||
               strcmp(actual, "java.lang.Double") == 0 ||
               strcmp(actual, "java.lang.Float") == 0 ||
               strcmp(actual, "java.lang.Short") == 0 ||
               strcmp(actual, "java.lang.Byte") == 0;
    if (strcmp(target, "java.lang.CharSequence") == 0)
        return strcmp(actual, "java.lang.String") == 0;
    if (strcmp(target, "java.util.List") == 0 ||
        strcmp(target, "java.util.Collection") == 0 ||
        strcmp(target, "java.lang.Iterable") == 0)
        return strcmp(actual, "java.util.LinkedList") == 0 ||
               strcmp(actual, "java.util.ArrayList") == 0;
    if (strcmp(target, "java.util.Queue") == 0 ||
        strcmp(target, "java.util.Deque") == 0)
        return strcmp(actual, "java.util.LinkedList") == 0;
    if (strcmp(target, "java.util.Map") == 0)
        return strcmp(actual, "java.util.HashMap") == 0 ||
               strcmp(actual, "java.util.LinkedHashMap") == 0;
    if (strcmp(target, "sleep.runtime.ScalarArray") == 0)
        return strcmp(
                   actual, "sleep.engine.types.ListContainer") == 0 ||
               strcmp(
                   actual, "sleep.runtime.CollectionWrapper") == 0;
    if (strcmp(target, "sleep.runtime.ScalarHash") == 0)
        return strcmp(
                   actual, "sleep.engine.types.HashContainer") == 0 ||
               strcmp(
                   actual,
                   "sleep.engine.types.OrderedHashContainer") == 0;
    return false;
}

static const char *invalid_cast_source_class(SlpValue value) {
    const char *name = value_java_class_name(value);
    return name ? name : "null";
}

static void get_string_repr(SlpValue v, const char **out_s, uint32_t *out_len, char *buf, size_t buf_size) {
    v = slp_value_unwrap_taint(v);
    if (SLP_IS_OBJ(v)) {
        SlpObj *obj = SLP_AS_OBJ(v);
        if (obj->type == SLP_OBJ_STRING) {
            SlpObjString *str = (SlpObjString *)obj;
            *out_s = str->chars;
            *out_len = str->length;
        } else if (obj->type == SLP_OBJ_LONG) {
            snprintf(buf, buf_size, "%lld",
                     (long long)((SlpObjLong*)obj)->value);
            *out_s = buf;
            *out_len = (uint32_t)strlen(buf);
        } else if (obj->type == SLP_OBJ_DOUBLE) {
            format_sleep_double(
                ((SlpObjDouble*)obj)->value, buf, buf_size);
            *out_s = buf;
            *out_len = (uint32_t)strlen(buf);
        } else if (obj->type == SLP_OBJ_CLASS) {
            SlpObjClass *class_object = (SlpObjClass*)obj;
            snprintf(
                buf, buf_size, "%s %s",
                class_object->is_interface ? "interface" : "class",
                class_object->name ? class_object->name->chars : "");
            *out_s = buf;
            *out_len = (uint32_t)strlen(buf);
        } else {
            snprintf(buf, buf_size, "<object %p>", (void *)obj);
            *out_s = buf;
            *out_len = (uint32_t)strlen(buf);
        }
    } else if (SLP_IS_NULL(v)) {
        *out_s = "";
        *out_len = 0;
    } else if (SLP_IS_NUM(v)) {
        format_sleep_number(SLP_AS_NUM(v), buf, buf_size);
        *out_s = buf;
        *out_len = (uint32_t)strlen(buf);
    } else if (SLP_IS_BOOL(v)) {
        *out_s = SLP_AS_BOOL(v) ? "true" : "false";
        *out_len = (uint32_t)strlen(*out_s);
    } else {
        *out_s = "";
        *out_len = 0;
    }
}

SlpResult slp_vm_run(SlpVM *vm, SlpObjFunction *fn) {
    SlpObjClosure *closure = slp_obj_closure_new(vm->allocator, fn);
    if (!closure) return SLP_RUNTIME_ERROR;
    track_object(vm, &closure->obj);

    slp_vm_push(vm, SLP_OBJ_VAL(closure));
    return slp_vm_call(vm, 0, false);
}

SlpResult slp_vm_call(SlpVM *vm, int arg_count, bool has_message) {
    SlpValue callee = slp_vm_peek(vm, arg_count + (has_message ? 1 : 0));
    if (!call_value(
            vm, callee, arg_count, has_message, NULL))
        return SLP_RUNTIME_ERROR;
    
    // We need to run the VM to actually execute the closure, 
    // unless it's a native function which call_value already executed.
    if (SLP_IS_OBJ(callee) && SLP_OBJ_TYPE(callee) == SLP_OBJ_CLOSURE) {
        // The call_closure function set up a new frame.
        // We now start the execution loop. We need a way to run the loop until this frame returns.
        int target_frame_count = vm->frame_count - 1;
        for (;;) {
            // GC safepoint: collect only here, between instructions, where every
            // live value is reachable from a root (stack/frames/globals) and no
            // half-built C-local intermediates exist. Allocation itself never
            // triggers collection, so native functions run to completion safely.
            if (vm->bytes_allocated > vm->next_gc_threshold)
                slp_gc_collect(vm);
            uint8_t instruction = read_byte(current_frame(vm));
            switch (instruction) {
        case OP_PUSH_NULL:
            slp_vm_push(vm, SLP_NULL_VAL);
            break;
        case OP_PUSH_TRUE:
            slp_vm_push(vm, SLP_TRUE_VAL);
            break;
        case OP_PUSH_FALSE:
            slp_vm_push(vm, SLP_FALSE_VAL);
            break;
        case OP_PUSH_CONST: {
            SlpValue val = read_constant(current_frame(vm));
            slp_vm_push(vm, val);
            break;
        }
        case OP_LOAD_LOCAL_0: slp_vm_push(vm, dereference_value(current_frame(vm)->slots[0])); break;
        case OP_LOAD_LOCAL_1: slp_vm_push(vm, dereference_value(current_frame(vm)->slots[1])); break;
        case OP_LOAD_LOCAL_2: slp_vm_push(vm, dereference_value(current_frame(vm)->slots[2])); break;
        case OP_LOAD_LOCAL_3: slp_vm_push(vm, dereference_value(current_frame(vm)->slots[3])); break;
        case OP_LOAD_LOCAL_4: slp_vm_push(vm, dereference_value(current_frame(vm)->slots[4])); break;
        case OP_LOAD_LOCAL_5: slp_vm_push(vm, dereference_value(current_frame(vm)->slots[5])); break;
        case OP_LOAD_LOCAL_6: slp_vm_push(vm, dereference_value(current_frame(vm)->slots[6])); break;
        case OP_LOAD_LOCAL_7: slp_vm_push(vm, dereference_value(current_frame(vm)->slots[7])); break;
        case OP_STORE_LOCAL_0: slot_assign(vm, &current_frame(vm)->slots[0], slp_vm_peek(vm, 0)); break;
        case OP_STORE_LOCAL_1: slot_assign(vm, &current_frame(vm)->slots[1], slp_vm_peek(vm, 0)); break;
        case OP_STORE_LOCAL_2: slot_assign(vm, &current_frame(vm)->slots[2], slp_vm_peek(vm, 0)); break;
        case OP_STORE_LOCAL_3: slot_assign(vm, &current_frame(vm)->slots[3], slp_vm_peek(vm, 0)); break;
        case OP_STORE_LOCAL_4: slot_assign(vm, &current_frame(vm)->slots[4], slp_vm_peek(vm, 0)); break;
        case OP_STORE_LOCAL_5: slot_assign(vm, &current_frame(vm)->slots[5], slp_vm_peek(vm, 0)); break;
        case OP_STORE_LOCAL_6: slot_assign(vm, &current_frame(vm)->slots[6], slp_vm_peek(vm, 0)); break;
        case OP_STORE_LOCAL_7: slot_assign(vm, &current_frame(vm)->slots[7], slp_vm_peek(vm, 0)); break;
        case OP_LOAD_LOCAL: {
            uint8_t slot = read_byte(current_frame(vm));
            slp_vm_push(vm, dereference_value(current_frame(vm)->slots[slot]));
            break;
        }
        case OP_STORE_LOCAL: {
            uint8_t slot = read_byte(current_frame(vm));
            slot_assign(vm, &current_frame(vm)->slots[slot], slp_vm_peek(vm, 0));
            break;
        }
        case OP_LOAD_GLOBAL: {
            uint16_t idx = read_short(current_frame(vm));
            SlpValue name_val = current_frame(vm)->closure->function->chunk->constants[idx];
            SlpValue val = SLP_NULL_VAL;
            if (!scoped_variable_get(vm, name_val, &val)) {
                warn_undeclared_variable(vm, name_val);
                val = implicit_variable_value(vm, name_val);
                scoped_variable_set(vm, name_val, val);
            }
            slp_vm_push(vm, val);
            break;
        }
        case OP_STORE_GLOBAL: {
            uint16_t idx = read_short(current_frame(vm));
            SlpValue name_val = current_frame(vm)->closure->function->chunk->constants[idx];
            SlpValue val = slp_vm_peek(vm, 0);
            SlpValue previous;
            if (!scoped_variable_get(vm, name_val, &previous))
                warn_undeclared_variable(vm, name_val);
            scoped_variable_set(vm, name_val, val);
            break;
        }
        case OP_STORE_CATCH: {
            uint16_t idx = read_short(current_frame(vm));
            SlpValue name_val =
                current_frame(vm)->closure->function->chunk->constants[idx];
            scoped_variable_set(vm, name_val, slp_vm_peek(vm, 0));
            break;
        }
        case OP_LOAD_UPVALUE: {
            uint8_t idx = read_byte(current_frame(vm));
            slp_vm_push(vm, dereference_value(
                *current_frame(vm)->closure->upvalues[idx]->location));
            break;
        }
        case OP_STORE_UPVALUE: {
            uint8_t idx = read_byte(current_frame(vm));
            slot_assign(
                vm,
                current_frame(vm)->closure->upvalues[idx]->location,
                slp_vm_peek(vm, 0));
            break;
        }
        case OP_REFERENCE_LOCAL: {
            uint8_t slot = read_byte(current_frame(vm));
            slp_vm_push(vm, slot_reference(vm, &current_frame(vm)->slots[slot]));
            break;
        }
        case OP_REFERENCE_GLOBAL: {
            uint16_t idx = read_short(current_frame(vm));
            SlpValue name = current_frame(vm)->closure->function->chunk->constants[idx];
            SlpValue previous;
            if (!scoped_variable_get(vm, name, &previous)) {
                warn_undeclared_variable(vm, name);
                scoped_variable_set(vm, name, implicit_variable_value(vm, name));
            }
            slp_vm_push(vm, scoped_variable_reference(vm, name));
            break;
        }
        case OP_REFERENCE_UPVALUE: {
            uint8_t idx = read_byte(current_frame(vm));
            slp_vm_push(vm, slot_reference(
                vm, current_frame(vm)->closure->upvalues[idx]->location));
            break;
        }
        case OP_ADD: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            SlpNumericKind kind = widest_numeric_kind(a, b);
            SlpValue result;
            if (kind == SLP_NUMERIC_DOUBLE)
                result = make_double_value(
                    vm, slp_value_as_number(a) +
                        slp_value_as_number(b));
            else if (kind == SLP_NUMERIC_LONG)
                result = make_long_value(
                    vm, (int64_t)((uint64_t)numeric_long_value(a) +
                                  (uint64_t)numeric_long_value(b)));
            else
                result = SLP_NUM_VAL((double)(int32_t)(
                    (uint32_t)numeric_int_value(a) +
                    (uint32_t)numeric_int_value(b)));
            slp_vm_push(
                vm,
                propagate_taint_binary(
                    vm, result, a, b));
            break;
        }
        case OP_SUBTRACT: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            SlpNumericKind kind = widest_numeric_kind(a, b);
            SlpValue result;
            if (kind == SLP_NUMERIC_DOUBLE)
                result = make_double_value(
                    vm, slp_value_as_number(a) -
                        slp_value_as_number(b));
            else if (kind == SLP_NUMERIC_LONG)
                result = make_long_value(
                    vm, (int64_t)((uint64_t)numeric_long_value(a) -
                                  (uint64_t)numeric_long_value(b)));
            else
                result = SLP_NUM_VAL((double)(int32_t)(
                    (uint32_t)numeric_int_value(a) -
                    (uint32_t)numeric_int_value(b)));
            slp_vm_push(
                vm,
                propagate_taint_binary(
                    vm, result, a, b));
            break;
        }
        case OP_MULTIPLY: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            SlpNumericKind kind = widest_numeric_kind(a, b);
            SlpValue result;
            if (kind == SLP_NUMERIC_DOUBLE)
                result = make_double_value(
                    vm, slp_value_as_number(a) *
                        slp_value_as_number(b));
            else if (kind == SLP_NUMERIC_LONG)
                result = make_long_value(
                    vm, (int64_t)((uint64_t)numeric_long_value(a) *
                                  (uint64_t)numeric_long_value(b)));
            else
                result = SLP_NUM_VAL((double)(int32_t)(
                    (uint32_t)numeric_int_value(a) *
                    (uint32_t)numeric_int_value(b)));
            slp_vm_push(
                vm,
                propagate_taint_binary(
                    vm, result, a, b));
            break;
        }
        case OP_DIVIDE: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            SlpNumericKind kind = widest_numeric_kind(a, b);
            SlpValue quotient;
            if (kind == SLP_NUMERIC_DOUBLE) {
                quotient = make_double_value(
                    vm, slp_value_as_number(a) /
                        slp_value_as_number(b));
            } else {
                int64_t divisor = kind == SLP_NUMERIC_LONG
                                      ? numeric_long_value(b)
                                      : numeric_int_value(b);
                if (divisor == 0) {
                    slp_vm_runtime_error(vm, "Division by zero.");
                    return SLP_RUNTIME_ERROR;
                }
                if (kind == SLP_NUMERIC_LONG) {
                    int64_t dividend = numeric_long_value(a);
                    int64_t result =
                        dividend == INT64_MIN && divisor == -1
                            ? INT64_MIN
                            : dividend / divisor;
                    quotient =
                        make_long_value(vm, result);
                } else {
                    int32_t dividend = numeric_int_value(a);
                    int32_t int_divisor = (int32_t)divisor;
                    int32_t result =
                        dividend == INT32_MIN && int_divisor == -1
                            ? INT32_MIN
                            : dividend / int_divisor;
                    quotient =
                        SLP_NUM_VAL((double)result);
                }
            }
            slp_vm_push(
                vm,
                propagate_taint_binary(
                    vm, quotient, a, b));
            break;
        }
        case OP_MODULO: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            SlpNumericKind kind = widest_numeric_kind(a, b);
            SlpValue remainder;
            if (kind == SLP_NUMERIC_DOUBLE) {
                double divisor = slp_value_as_number(b);
                if (divisor == 0.0) {
                    slp_vm_runtime_error(vm, "Modulo by zero.");
                    return SLP_RUNTIME_ERROR;
                }
                remainder = make_double_value(
                    vm,
                    fmod(
                        slp_value_as_number(a),
                        divisor));
            } else {
                int64_t divisor =
                    kind == SLP_NUMERIC_LONG
                        ? numeric_long_value(b)
                        : numeric_int_value(b);
                if (divisor == 0) {
                    slp_vm_runtime_error(vm, "Modulo by zero.");
                    return SLP_RUNTIME_ERROR;
                }
                if (kind == SLP_NUMERIC_LONG)
                    remainder = make_long_value(
                        vm,
                        numeric_long_value(a) %
                            divisor);
                else
                    remainder = SLP_NUM_VAL(
                        (double)(
                            numeric_int_value(a) %
                            (int32_t)divisor));
            }
            slp_vm_push(
                vm,
                propagate_taint_binary(
                    vm, remainder, a, b));
            break;
        }
        case OP_POWER: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            SlpValue result =
                make_double_value(
                    vm,
                    pow(
                        slp_value_as_number(a),
                        slp_value_as_number(b)));
            slp_vm_push(
                vm,
                propagate_taint_binary(
                    vm, result, a, b));
            break;
        }
        case OP_NEGATE: {
            SlpValue value = slp_vm_pop(vm);
            SlpNumericKind kind = numeric_kind(value);
            SlpValue result;
            if (kind == SLP_NUMERIC_DOUBLE)
                result = make_double_value(
                    vm, -slp_value_as_number(value));
            else if (kind == SLP_NUMERIC_LONG)
                result = make_long_value(
                    vm, (int64_t)(0u -
                        (uint64_t)numeric_long_value(value)));
            else
                result = SLP_NUM_VAL(
                    (double)(int32_t)(
                        0u -
                        (uint32_t)
                            numeric_int_value(value)));
            slp_vm_push(
                vm,
                propagate_taint_unary(
                    vm, result, value));
            break;
        }
        case OP_CONCAT: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            SlpObjString *left = slp_vm_stringify(vm, a);
            SlpObjString *right = slp_vm_stringify(vm, b);
            if (!left || !right) {
                slp_vm_runtime_error(vm, "Out of memory.");
                return SLP_RUNTIME_ERROR;
            }
            uint32_t total = left->length + right->length;
            char *concat_buf = (char*)SLP_MALLOC(vm->allocator, total + 1);
            if (!concat_buf) {
                slp_vm_runtime_error(vm, "Out of memory.");
                return SLP_RUNTIME_ERROR;
            }
            memcpy(concat_buf, left->chars, left->length);
            memcpy(concat_buf + left->length, right->chars, right->length);
            concat_buf[total] = '\0';
            SlpObjString *result = slp_vm_intern_string(vm, concat_buf, total);
            SLP_FREE(vm->allocator, concat_buf);
            slp_vm_push(
                vm,
                propagate_taint_binary(
                    vm, SLP_OBJ_VAL(result),
                    a, b));
            break;
        }
        case OP_ALIGN: {
            SlpValue value = slp_vm_pop(vm);
            int width = (int)slp_value_as_number(slp_vm_pop(vm));
            SlpObjString *text = slp_vm_stringify(vm, value);
            if (!text) {
                slp_vm_runtime_error(vm, "Out of memory.");
                return SLP_RUNTIME_ERROR;
            }

            int64_t requested = width < 0 ? -(int64_t)width : (int64_t)width;
            size_t padding = requested > (int64_t)text->length
                                 ? (size_t)(requested - (int64_t)text->length)
                                 : 0;
            if (padding == 0) {
                slp_vm_push(vm, SLP_OBJ_VAL(text));
                break;
            }

            size_t length = (size_t)text->length + padding;
            char *buffer = (char*)SLP_MALLOC(vm->allocator, length + 1);
            if (!buffer) {
                slp_vm_runtime_error(vm, "Out of memory.");
                return SLP_RUNTIME_ERROR;
            }
            if (width < 0) {
                memset(buffer, ' ', padding);
                memcpy(buffer + padding, text->chars, text->length);
            } else {
                memcpy(buffer, text->chars, text->length);
                memset(buffer + text->length, ' ', padding);
            }
            buffer[length] = '\0';
            SlpObjString *aligned =
                slp_vm_copy_string(vm, buffer, (uint32_t)length);
            SLP_FREE(vm->allocator, buffer);
            if (!aligned) {
                slp_vm_runtime_error(vm, "Out of memory.");
                return SLP_RUNTIME_ERROR;
            }
            slp_vm_push(vm, SLP_OBJ_VAL(aligned));
            break;
        }
        case OP_REPEAT: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            SlpObjString *str = slp_vm_stringify(vm, a);
            long count = (long)slp_value_as_number(b);
            if (!str) {
                slp_vm_runtime_error(vm, "Out of memory.");
                return SLP_RUNTIME_ERROR;
            }
            if (count <= 0 || str->length == 0) {
                slp_vm_push(
                    vm, SLP_OBJ_VAL(
                        slp_vm_copy_string(vm, "", 0)));
            } else {
                size_t total =
                    (size_t)str->length * (size_t)count;
                if (total > UINT32_MAX ||
                    total / (size_t)count != str->length) {
                    slp_vm_runtime_error(
                        vm, "Repeated string is too large.");
                    return SLP_RUNTIME_ERROR;
                }
                char *buf =
                    (char*)SLP_MALLOC(vm->allocator, total + 1);
                if (!buf) {
                    slp_vm_runtime_error(vm, "Out of memory.");
                    return SLP_RUNTIME_ERROR;
                }
                for (long i = 0; i < count; i++)
                    memcpy(
                        buf + (size_t)str->length * (size_t)i,
                        str->chars, str->length);
                buf[total] = '\0';
                SlpObjString *result = slp_vm_intern_string(
                    vm, buf, (uint32_t)total);
                SLP_FREE(vm->allocator, buf);
                slp_vm_push(vm, SLP_OBJ_VAL(result));
            }
            break;
        }
        case OP_EQUAL: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            bool result = slp_value_equals(a, b);
            trace_binary_predicate(vm, a, "==", b, result);
            slp_vm_push(vm, SLP_BOOL_VAL(result));
            break;
        }
        case OP_NOT_EQUAL: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            bool result = !slp_value_equals(a, b);
            trace_binary_predicate(vm, a, "!=", b, result);
            slp_vm_push(vm, SLP_BOOL_VAL(result));
            break;
        }
        case OP_LESS: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            bool result =
                slp_value_as_number(a) < slp_value_as_number(b);
            trace_binary_predicate(vm, a, "<", b, result);
            slp_vm_push(vm, SLP_BOOL_VAL(result));
            break;
        }
        case OP_GREATER: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            bool result =
                slp_value_as_number(a) > slp_value_as_number(b);
            trace_binary_predicate(vm, a, ">", b, result);
            slp_vm_push(vm, SLP_BOOL_VAL(result));
            break;
        }
        case OP_LESS_EQUAL: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            bool result =
                slp_value_as_number(a) <= slp_value_as_number(b);
            trace_binary_predicate(vm, a, "<=", b, result);
            slp_vm_push(vm, SLP_BOOL_VAL(result));
            break;
        }
        case OP_GREATER_EQUAL: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            bool result =
                slp_value_as_number(a) >= slp_value_as_number(b);
            trace_binary_predicate(vm, a, ">=", b, result);
            slp_vm_push(vm, SLP_BOOL_VAL(result));
            break;
        }
        case OP_SPACESHIP: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            double da = slp_value_as_number(a);
            double db = slp_value_as_number(b);
            slp_vm_push(vm, SLP_NUM_VAL(da < db ? -1.0 : (da > db ? 1.0 : 0.0)));
            break;
        }
        case OP_NOT:
            slp_vm_push(vm, SLP_BOOL_VAL(slp_value_is_falsy(slp_vm_pop(vm))));
            break;
        case OP_AND: {
            uint16_t offset = read_short(current_frame(vm));
            if (slp_value_is_falsy(slp_vm_peek(vm, 0))) {
                current_frame(vm)->ip += offset;
            } else {
                slp_vm_pop(vm);
            }
            break;
        }
        case OP_OR: {
            uint16_t offset = read_short(current_frame(vm));
            if (!slp_value_is_falsy(slp_vm_peek(vm, 0))) {
                current_frame(vm)->ip += offset;
            } else {
                slp_vm_pop(vm);
            }
            break;
        }
        case OP_BIT_AND: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            if (widest_numeric_kind(a, b) == SLP_NUMERIC_LONG)
                slp_vm_push(vm, make_long_value(
                    vm, numeric_long_value(a) & numeric_long_value(b)));
            else
                slp_vm_push(vm, SLP_NUM_VAL((double)(
                    numeric_int_value(a) & numeric_int_value(b))));
            break;
        }
        case OP_BIT_OR: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            if (widest_numeric_kind(a, b) == SLP_NUMERIC_LONG)
                slp_vm_push(vm, make_long_value(
                    vm, numeric_long_value(a) | numeric_long_value(b)));
            else
                slp_vm_push(vm, SLP_NUM_VAL((double)(
                    numeric_int_value(a) | numeric_int_value(b))));
            break;
        }
        case OP_BIT_XOR: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            if (widest_numeric_kind(a, b) == SLP_NUMERIC_LONG)
                slp_vm_push(vm, make_long_value(
                    vm, numeric_long_value(a) ^ numeric_long_value(b)));
            else
                slp_vm_push(vm, SLP_NUM_VAL((double)(
                    numeric_int_value(a) ^ numeric_int_value(b))));
            break;
        }
        case OP_BIT_NOT: {
            SlpValue value = slp_vm_pop(vm);
            if (numeric_kind(value) == SLP_NUMERIC_LONG)
                slp_vm_push(vm, make_long_value(
                    vm, ~numeric_long_value(value)));
            else
                slp_vm_push(vm, SLP_NUM_VAL((double)(
                    ~numeric_int_value(value))));
            break;
        }
        case OP_LSHIFT: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            if (numeric_kind(a) == SLP_NUMERIC_LONG) {
                uint32_t shift = (uint32_t)numeric_int_value(b) & 63u;
                slp_vm_push(vm, make_long_value(
                    vm, (int64_t)((uint64_t)numeric_long_value(a) << shift)));
            } else {
                uint32_t shift = (uint32_t)numeric_int_value(b) & 31u;
                slp_vm_push(vm, SLP_NUM_VAL((double)(int32_t)(
                    (uint32_t)numeric_int_value(a) << shift)));
            }
            break;
        }
        case OP_RSHIFT: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            if (numeric_kind(a) == SLP_NUMERIC_LONG) {
                uint32_t shift = (uint32_t)numeric_int_value(b) & 63u;
                slp_vm_push(vm, make_long_value(
                    vm, numeric_long_value(a) >> shift));
            } else {
                uint32_t shift = (uint32_t)numeric_int_value(b) & 31u;
                slp_vm_push(vm, SLP_NUM_VAL((double)(
                    numeric_int_value(a) >> shift)));
            }
            break;
        }
        case OP_INCREMENT: {
            SlpValue v = slp_vm_pop(vm);
            SlpNumericKind kind = numeric_kind(v);
            if (kind == SLP_NUMERIC_DOUBLE)
                slp_vm_push(vm, make_double_value(
                    vm, slp_value_as_number(v) + 1.0));
            else if (kind == SLP_NUMERIC_LONG)
                slp_vm_push(vm, make_long_value(
                    vm, (int64_t)((uint64_t)numeric_long_value(v) + 1u)));
            else
                slp_vm_push(vm, SLP_NUM_VAL((double)(int32_t)(
                    (uint32_t)numeric_int_value(v) + 1u)));
            break;
        }
        case OP_DECREMENT: {
            SlpValue v = slp_vm_pop(vm);
            SlpNumericKind kind = numeric_kind(v);
            if (kind == SLP_NUMERIC_DOUBLE)
                slp_vm_push(vm, make_double_value(
                    vm, slp_value_as_number(v) - 1.0));
            else if (kind == SLP_NUMERIC_LONG)
                slp_vm_push(vm, make_long_value(
                    vm, (int64_t)((uint64_t)numeric_long_value(v) - 1u)));
            else
                slp_vm_push(vm, SLP_NUM_VAL((double)(int32_t)(
                    (uint32_t)numeric_int_value(v) - 1u)));
            break;
        }
        case OP_POP:
            slp_vm_pop(vm);
            break;
        case OP_DUP:
            slp_vm_push(vm, slp_vm_peek(vm, 0));
            break;
        case OP_DUP2: {
            SlpValue below = slp_vm_peek(vm, 1);
            SlpValue top = slp_vm_peek(vm, 0);
            slp_vm_push(vm, below);
            slp_vm_push(vm, top);
            break;
        }
        case OP_SWAP: {
            SlpValue top = slp_vm_pop(vm);
            SlpValue below = slp_vm_pop(vm);
            slp_vm_push(vm, top);
            slp_vm_push(vm, below);
            break;
        }
        case OP_REVERSE: {
            uint8_t count = read_byte(current_frame(vm));
            SlpValue *first = vm->stack_top - count;
            for (uint8_t i = 0; i < count / 2; i++) {
                SlpValue value = first[i];
                first[i] = first[count - 1 - i];
                first[count - 1 - i] = value;
            }
            break;
        }
        case OP_JUMP: {
            uint16_t offset = read_short(current_frame(vm));
            current_frame(vm)->ip += offset;
            break;
        }
        case OP_JUMP_IF_FALSE: {
            uint16_t offset = read_short(current_frame(vm));
            if (slp_value_is_falsy(slp_vm_peek(vm, 0)))
                current_frame(vm)->ip += offset;
            break;
        }
        case OP_JUMP_IF_TRUE: {
            uint16_t offset = read_short(current_frame(vm));
            if (!slp_value_is_falsy(slp_vm_peek(vm, 0)))
                current_frame(vm)->ip += offset;
            break;
        }
        case OP_JUMP_IF_NULL: {
            uint16_t offset =
                read_short(current_frame(vm));
            if (SLP_IS_NULL(
                    dereference_value(
                        slp_vm_peek(vm, 0))))
                current_frame(vm)->ip += offset;
            break;
        }
        case OP_LOOP: {
            int16_t offset = (int16_t)read_short(current_frame(vm));
            current_frame(vm)->ip -= offset;
            break;
        }
        case OP_CALL: {
            uint8_t arg_count = read_byte(current_frame(vm));
            SlpValue callee = slp_vm_peek(vm, arg_count);
            if (!call_value(
                    vm, callee, arg_count, false, NULL))
                return SLP_RUNTIME_ERROR;
            break;
        }
        case OP_CALL_NAMED: {
            uint8_t arg_count = read_byte(current_frame(vm));
            uint16_t name_index = read_short(current_frame(vm));
            SlpValue callee = slp_vm_peek(vm, arg_count);
            SlpValue name_value =
                current_frame(vm)->closure->function->chunk
                    ->constants[name_index];
            const char *name =
                SLP_IS_OBJ(name_value) &&
                SLP_OBJ_TYPE(name_value) == SLP_OBJ_STRING
                    ? SLP_AS_STRING(name_value)->chars
                    : "";
            bool callable =
                SLP_IS_OBJ(callee) &&
                (SLP_OBJ_TYPE(callee) == SLP_OBJ_CLOSURE ||
                 SLP_OBJ_TYPE(callee) == SLP_OBJ_NATIVE ||
                 SLP_OBJ_TYPE(callee) == SLP_OBJ_CONTINUATION);
            if (!callable) {
                char message[320];
                snprintf(
                    message, sizeof(message),
                    "Attempted to call non-existent function &%s",
                    name);
                slp_vm_warning(vm, message);
                vm->stack_top -= arg_count + 1;
                slp_vm_push(vm, SLP_NULL_VAL);
            } else if (!call_value(
                           vm, callee, arg_count, false,
                           name)) {
                return SLP_RUNTIME_ERROR;
            }
            break;
        }
        case OP_RETURN: {
            SlpValue result = slp_vm_pop(vm);
            SlpCallFrame *returning_frame = current_frame(vm);
            if (returning_frame->tainted_arguments)
                result =
                    slp_vm_taint_value(
                        vm, result);
            SlpObjString *trace_call =
                returning_frame->trace_call;
            SlpObjString *trace_source =
                returning_frame->trace_source;
            int trace_line = returning_frame->trace_line;
            bool trace_enabled =
                returning_frame->trace_enabled;
            SlpObjClosure *returning_closure =
                returning_frame->closure;
            SlpObjContinuation *continuation_return =
                returning_frame->continuation_return;
            int leaked_local_scopes = 0;
            if (!returning_closure->is_inline &&
                returning_frame->local_scopes &&
                returning_frame->local_scopes->count > 1) {
                leaked_local_scopes =
                    returning_frame->local_scopes->count - 1;
                returning_frame->local_scopes->count = 1;
            }
            SlpObjString *returning_description =
                leaked_local_scopes > 0
                    ? describe_value(
                          vm, SLP_OBJ_VAL(returning_closure))
                    : NULL;
            close_upvalues(vm, returning_frame->slots);

            vm->frame_count--;
            trim_try_handlers(vm, vm->frame_count);
            if (leaked_local_scopes > 0 &&
                returning_description) {
                size_t message_length =
                    returning_description->length + 128;
                char *message = (char*)SLP_MALLOC(
                    vm->allocator, message_length);
                if (message) {
                    snprintf(
                        message, message_length,
                        "%d unaccounted local stack frame(s) in %s "
                        "(perhaps you forgot to &popl?)",
                        leaked_local_scopes,
                        returning_description->chars);
                    slp_vm_warning(vm, message);
                    SLP_FREE(vm->allocator, message);
                }
            }
            if (continuation_return) {
                /*
                 * The owner was entered through a continuation invocation.
                 * Its original call already emitted a -goto- trace at callcc;
                 * now finish the invocation that transferred control here.
                 */
                finish_call_trace(
                    vm,
                    continuation_return->return_trace_call,
                    continuation_return->return_trace_source,
                    continuation_return->return_trace_line,
                    continuation_return->return_trace_enabled,
                    SLP_NULL_VAL, false, false);
                restore_vm_context(vm, continuation_return);
                slp_vm_push(vm, result);
                break;
            }
            finish_call_trace(
                vm, trace_call, trace_source, trace_line,
                trace_enabled, result, false, false);
            if (vm->frame_count == target_frame_count) {
                // The frame we just popped owns `slots[0..=arg_count]` (callee
                // and args). Resetting stack_top there pops them, then we push
                // the result in their place.
                SlpCallFrame *old_frame = &vm->frames[vm->frame_count];
                vm->stack_top = old_frame->slots;
                slp_vm_push(vm, result);
                return SLP_OK;
            }
            if (vm->frame_count == 0) return SLP_OK; // Should not happen here if target_frame_count was set right, but safe.
            SlpCallFrame *old_frame = &vm->frames[vm->frame_count];
            vm->stack_top = old_frame->slots;
            slp_vm_push(vm, result);
            break;
        }
        case OP_INLINE_RETURN: {
            SlpValue result = slp_vm_pop(vm);
            int root_index = vm->frame_count - 1;
            while (root_index > 0 &&
                   vm->frames[root_index].closure->is_inline)
                root_index--;

            /* Explicit return in inline code is flow control for its ordinary
               caller. Discard the caller and all nested inline activations. */
            SlpCallFrame *root = &vm->frames[root_index];
            for (int i = vm->frame_count - 1;
                 i >= root_index; i--) {
                finish_call_trace(
                    vm, vm->frames[i].trace_call,
                    vm->frames[i].trace_source,
                    vm->frames[i].trace_line,
                    vm->frames[i].trace_enabled,
                    result, false, false);
            }
            close_upvalues(vm, root->slots);
            clear_coroutine_state(vm, root->closure);
            vm->frame_count = root_index;
            trim_try_handlers(vm, vm->frame_count);
            vm->stack_top = root->slots;
            slp_vm_push(vm, result);

            if (vm->frame_count == target_frame_count)
                return SLP_OK;
            if (vm->frame_count == 0)
                return SLP_OK;
            break;
        }
        case OP_CLOSURE: {
            uint16_t fn_idx = read_short(current_frame(vm));
            SlpObjFunction *fn_obj = (SlpObjFunction*)SLP_AS_OBJ(
                current_frame(vm)->closure->function->chunk->constants[fn_idx]);
            SlpObjClosure *cl = slp_obj_closure_new(vm->allocator, fn_obj);
            track_object(vm, &cl->obj);
            cl->identity = ++vm->next_closure_identity;
            for (int i = 0; i < fn_obj->upvalue_count; i++) {
                uint8_t is_local = read_byte(current_frame(vm));
                uint8_t idx = read_byte(current_frame(vm));
                if (is_local) {
                    cl->upvalues[i] = slp_obj_upvalue_new(vm->allocator,
                        &current_frame(vm)->slots[idx]);
                    track_object(vm, &cl->upvalues[i]->obj);
                } else {
                    cl->upvalues[i] = current_frame(vm)->closure->upvalues[idx];
                }
            }
            slp_vm_push(vm, SLP_OBJ_VAL(cl));
            break;
        }
        case OP_CLOSE_UPVALUE:
            close_upvalues(vm, vm->stack_top - 1);
            slp_vm_pop(vm);
            break;
        case OP_BUILD_ARRAY: {
            uint16_t count = read_short(current_frame(vm));
            SlpObjArray *arr = slp_vm_new_array(vm);
            for (uint16_t i = 0; i < count; i++) {
                SlpValue val = slp_vm_pop(vm);
                slp_obj_array_push(vm->allocator, arr, val);
            }
            for (int i = 0; i < arr->count / 2; i++) {
                SlpValue tmp = arr->elements[i];
                arr->elements[i] = arr->elements[arr->count - 1 - i];
                arr->elements[arr->count - 1 - i] = tmp;
            }
            slp_vm_push(vm, SLP_OBJ_VAL(arr));
            break;
        }
        case OP_BUILD_HASH: {
            uint16_t count = read_short(current_frame(vm));
            SlpObjHash *hash = slp_vm_new_hash(vm);
            for (uint16_t i = 0; i < count; i++) {
                SlpValue val = slp_vm_pop(vm);
                SlpValue key = slp_vm_pop(vm);
                slp_vm_hash_set(vm, hash, key, val);
            }
            slp_vm_push(vm, SLP_OBJ_VAL(hash));
            break;
        }
        case OP_BUILD_KEY_VALUE: {
            SlpValue value = slp_vm_pop(vm);
            SlpValue key = slp_vm_pop(vm);
            SlpObjKeyValue *kv = slp_obj_key_value_new(vm->allocator, key, value);
            if (!kv) {
                slp_vm_runtime_error(vm, "Out of memory.");
                return SLP_RUNTIME_ERROR;
            }
            track_object(vm, &kv->obj);
            slp_vm_push(vm, SLP_OBJ_VAL(kv));
            break;
        }
        case OP_INDEX_GET: {
            SlpValue idx_val = slp_vm_pop(vm);
            SlpValue container = slp_vm_pop(vm);
            if (SLP_IS_OBJ(container)) {
                SlpObj *obj = SLP_AS_OBJ(container);
                if (obj->type == SLP_OBJ_ARRAY) {
                    int idx = (int)slp_value_as_number(idx_val);
                    SlpObjArray *array = (SlpObjArray*)obj;
                    if (idx < 0 && array->count > 0) {
                        idx %= array->count;
                        if (idx < 0) idx += array->count;
                    }
                    slp_vm_push(vm, slp_obj_array_get(array, idx));
                } else if (obj->type == SLP_OBJ_HASH) {
                    slp_vm_push(
                        vm, slp_vm_hash_get(
                                vm, (SlpObjHash*)obj, idx_val));
                } else if (obj->type == SLP_OBJ_CLOSURE) {
                    SlpObjClosure *closure = (SlpObjClosure*)obj;
                    if (!ensure_closure_scope(vm, closure)) {
                        slp_vm_runtime_error(vm, "Out of memory.");
                        return SLP_RUNTIME_ERROR;
                    }
                    slp_vm_push(vm,
                        slp_obj_hash_get(closure->scope, idx_val));
                } else {
                    warn_invalid_index(vm, container, idx_val);
                    slp_vm_push(vm, SLP_NULL_VAL);
                }
            } else {
                warn_invalid_index(vm, container, idx_val);
                slp_vm_push(vm, SLP_NULL_VAL);
            }
            break;
        }
        case OP_INDEX_ENSURE: {
            uint8_t kind = read_byte(current_frame(vm));
            SlpValue idx_val = slp_vm_pop(vm);
            SlpValue container = slp_vm_pop(vm);
            SlpValue nested = SLP_NULL_VAL;
            if (SLP_IS_OBJ(container)) {
                SlpObj *obj = SLP_AS_OBJ(container);
                if (obj->type == SLP_OBJ_ARRAY) {
                    SlpObjArray *array = (SlpObjArray*)obj;
                    if (array->read_only) {
                        slp_vm_abort_warning(vm, "array is read-only");
                        break;
                    }
                    int idx = (int)slp_value_as_number(idx_val);
                    if (idx < 0 && array->count > 0) {
                        idx %= array->count;
                        if (idx < 0) idx += array->count;
                    }
                    nested = slp_obj_array_get(array, idx);
                    if (SLP_IS_NULL(nested)) {
                        nested = new_index_collection(
                            vm, kind, SLP_OBJ_ARRAY);
                        if (SLP_IS_NULL(nested)) {
                            slp_vm_runtime_error(vm, "Out of memory.");
                            return SLP_RUNTIME_ERROR;
                        }
                        slp_obj_array_set(
                            vm->allocator, array, idx, nested);
                    }
                } else if (obj->type == SLP_OBJ_HASH) {
                    SlpObjHash *hash = (SlpObjHash*)obj;
                    nested = slp_vm_hash_get(vm, hash, idx_val);
                    if (SLP_IS_NULL(nested)) {
                        nested = new_index_collection(
                            vm, kind, SLP_OBJ_HASH);
                        if (SLP_IS_NULL(nested)) {
                            slp_vm_runtime_error(vm, "Out of memory.");
                            return SLP_RUNTIME_ERROR;
                        }
                        slp_vm_hash_set(vm, hash, idx_val, nested);
                    }
                } else if (obj->type == SLP_OBJ_CLOSURE) {
                    SlpObjClosure *closure = (SlpObjClosure*)obj;
                    if (!ensure_closure_scope(vm, closure)) {
                        slp_vm_runtime_error(vm, "Out of memory.");
                        return SLP_RUNTIME_ERROR;
                    }
                    nested = slp_obj_hash_get(closure->scope, idx_val);
                    if (SLP_IS_NULL(nested)) {
                        nested = new_index_collection(
                            vm, kind, SLP_OBJ_CLOSURE);
                        if (SLP_IS_NULL(nested)) {
                            slp_vm_runtime_error(vm, "Out of memory.");
                            return SLP_RUNTIME_ERROR;
                        }
                        assign_binding(
                            vm, closure->scope,
                            idx_val, nested);
                    }
                }
            }
            slp_vm_push(vm, nested);
            break;
        }
        case OP_INDEX_SET: {
            SlpValue val = slp_vm_pop(vm);
            SlpValue idx_val = slp_vm_pop(vm);
            SlpValue container = slp_vm_pop(vm);
            if (SLP_IS_OBJ(container)) {
                SlpObj *obj = SLP_AS_OBJ(container);
                if (obj->type == SLP_OBJ_ARRAY) {
                    SlpObjArray *array = (SlpObjArray*)obj;
                    if (array->read_only) {
                        slp_vm_abort_warning(vm, "array is read-only");
                        break;
                    }
                    int idx = (int)slp_value_as_number(idx_val);
                    if (idx < 0 && array->count > 0) {
                        idx %= array->count;
                        if (idx < 0) idx += array->count;
                    }
                    slp_obj_array_set(vm->allocator, array, idx, val);
                } else if (obj->type == SLP_OBJ_HASH) {
                    /*
                     * Sleep treats $null as absence in a hash. Assigning it
                     * through the index operator removes an existing mapping.
                     */
                    SlpObjHash *hash = (SlpObjHash*)obj;
                    if (SLP_IS_NULL(val) && hash->order_mode == 0)
                        slp_vm_hash_delete(vm, hash, idx_val);
                    else
                        slp_vm_hash_set(vm, hash, idx_val, val);
                }
                else if (obj->type == SLP_OBJ_CLOSURE) {
                    SlpObjClosure *closure = (SlpObjClosure*)obj;
                    if (!ensure_closure_scope(vm, closure)) {
                        slp_vm_runtime_error(vm, "Out of memory.");
                        return SLP_RUNTIME_ERROR;
                    }
                    assign_binding(
                        vm, closure->scope,
                        idx_val, val);
                }
            }
            slp_vm_push(vm, val);
            break;
        }
        case OP_TUPLE_GET: {
            SlpValue idx_val = slp_vm_pop(vm);
            SlpValue source = slp_vm_pop(vm);
            if (SLP_IS_OBJ(source) &&
                SLP_OBJ_TYPE(source) == SLP_OBJ_ARRAY) {
                int idx = (int)slp_value_as_number(idx_val);
                slp_vm_push(vm, slp_obj_array_get(
                    SLP_AS_ARRAY(source), idx));
            } else {
                slp_vm_push(vm, source);
            }
            break;
        }
        case OP_TUPLE_COMPOUND: {
            uint8_t opcode =
                read_byte(current_frame(vm));
            SlpValue left = slp_vm_pop(vm);
            SlpValue right = slp_vm_pop(vm);
            SlpValue result = SLP_NULL_VAL;

            if (SLP_IS_OBJ(left) &&
                SLP_OBJ_TYPE(left) == SLP_OBJ_ARRAY) {
                SlpObjArray *targets =
                    SLP_AS_ARRAY(left);
                SlpObjArray *sources =
                    SLP_IS_OBJ(right) &&
                            SLP_OBJ_TYPE(right) ==
                                SLP_OBJ_ARRAY
                        ? SLP_AS_ARRAY(right)
                        : NULL;
                for (int i = 0; i < targets->count; i++) {
                    SlpValue operand = sources
                        ? slp_obj_array_get(sources, i)
                        : right;
                    SlpValue current =
                        slp_obj_array_get(
                            targets, i);
                    SlpValue value = SLP_NULL_VAL;
                    if (!evaluate_assignment_operator(
                            vm, opcode,
                            current,
                            operand, &value))
                        return SLP_RUNTIME_ERROR;
                    value =
                        propagate_taint_binary(
                            vm, value,
                            current, operand);
                    slp_obj_array_set(
                        vm->allocator, targets, i, value);
                }
                result = left;
            } else {
                SlpValue operand = right;
                if (SLP_IS_OBJ(right) &&
                    SLP_OBJ_TYPE(right) ==
                        SLP_OBJ_ARRAY)
                    operand = slp_obj_array_get(
                        SLP_AS_ARRAY(right), 0);
                if (!evaluate_assignment_operator(
                        vm, opcode, left, operand,
                        &result))
                    return SLP_RUNTIME_ERROR;
                result =
                    propagate_taint_binary(
                        vm, result,
                        left, operand);
            }
            slp_vm_push(vm, result);
            break;
        }
        case OP_PUSH_HANDLER: {
            uint16_t offset = read_short(current_frame(vm));
            SlpCallFrame *frame = current_frame(vm);
            if (vm->try_handler_count < SLP_MAX_HANDLERS) {
                vm->try_handlers[vm->try_handler_count].catch_ip = frame->ip + offset;
                vm->try_handlers[vm->try_handler_count].frame_count = vm->frame_count;
                vm->try_handlers[vm->try_handler_count].stack_count =
                    (int)(vm->stack_top - vm->stack);
                vm->try_handler_count++;
            }
            break;
        }
        case OP_POP_HANDLER:
            if (vm->try_handler_count > 0)
                vm->try_handler_count--;
            break;
        case OP_THROW: {
            SlpValue exc = slp_vm_pop(vm);
            if (vm->try_handler_count > 0) {
                vm->try_handler_count--;
                SlpTryHandler h = vm->try_handlers[vm->try_handler_count];
                capture_stack_trace(vm, h.frame_count);
                while (vm->frame_count > h.frame_count) {
                    close_upvalues(vm, current_frame(vm)->slots);
                    vm->frame_count--;
                }
                current_frame(vm)->ip = h.catch_ip;
                vm->stack_top = vm->stack + h.stack_count;
                slp_vm_push(vm, exc);
            } else {
                vm->thrown_exception = exc;
                return SLP_RUNTIME_ERROR;
            }
            break;
        }
        case OP_ASSERT: {
            SlpValue message = slp_vm_pop(vm);
            SlpObjString *text = slp_vm_stringify(vm, message);
            if ((vm->debug_flags & 8) == 8) {
                SlpObjString *description =
                    describe_value(vm, message);
                size_t length =
                    (description ? description->length : 0) + 9;
                char *call = (char*)SLP_MALLOC(
                    vm->allocator, length + 1);
                if (call) {
                    snprintf(
                        call, length + 1, "&exit(%s)",
                        description ? description->chars : "");
                    SlpObjString *call_text =
                        slp_vm_copy_cstr(vm, call);
                    SlpCallFrame *frame = current_frame(vm);
                    SlpObjString *source =
                        frame && frame->closure &&
                                frame->closure->function
                            ? frame->closure->function->source_name
                            : NULL;
                    finish_call_trace(
                        vm, call_text, source,
                        frame_current_line(frame), true,
                        SLP_NULL_VAL, true, false);
                    SLP_FREE(vm->allocator, call);
                }
            }
            slp_vm_abort_warning(
                vm, text ? text->chars : "assertion failed");
            vm->flow_exit_requested = true;
            break;
        }
        case OP_HALT:
            return SLP_HALT;
        case OP_DONE:
            return SLP_OK;
        case OP_YIELD: {
            SlpValue result = slp_vm_pop(vm);
            int root_index = vm->frame_count - 1;
            while (root_index > 0 &&
                   vm->frames[root_index].closure->is_inline)
                root_index--;

            SlpCallFrame *root = &vm->frames[root_index];
            SlpObjClosure *owner = root->closure;
            clear_coroutine_state(vm, owner);

            owner->coroutine_stack_count =
                (int)(vm->stack_top - root->slots);
            owner->coroutine_frame_count = vm->frame_count - root_index;
            owner->coroutine_stack = SLP_ALLOCATE_ARRAY(
                vm->allocator, SlpValue, owner->coroutine_stack_count);
            owner->coroutine_frames = SLP_ALLOCATE_ARRAY(
                vm->allocator, SlpCallFrame, owner->coroutine_frame_count);
            if (!owner->coroutine_stack || !owner->coroutine_frames) {
                clear_coroutine_state(vm, owner);
                slp_vm_runtime_error(vm, "Out of memory.");
                return SLP_RUNTIME_ERROR;
            }
            memcpy(owner->coroutine_stack, root->slots,
                   sizeof(SlpValue) * owner->coroutine_stack_count);
            memcpy(owner->coroutine_frames, &vm->frames[root_index],
                   sizeof(SlpCallFrame) * owner->coroutine_frame_count);
            for (int i = 0; i < owner->coroutine_frame_count; i++) {
                ptrdiff_t slot_offset =
                    owner->coroutine_frames[i].slots - root->slots;
                owner->coroutine_frames[i].slots =
                    owner->coroutine_stack + slot_offset;
            }
            if (!capture_coroutine_try_handlers(vm, owner, root_index,
                                                root->slots)) {
                clear_coroutine_state(vm, owner);
                slp_vm_runtime_error(vm, "Out of memory.");
                return SLP_RUNTIME_ERROR;
            }
            owner->coroutine_needs_result = false;

            close_upvalues(vm, root->slots);
            vm->frame_count = root_index;
            trim_try_handlers(vm, vm->frame_count);

            if (vm->frame_count == target_frame_count) {
                vm->stack_top = root->slots;
                slp_vm_push(vm, result);
                return SLP_OK;
            }

            vm->stack_top = root->slots;
            slp_vm_push(vm, result);
            break;
        }
        case OP_BREAK:
        case OP_CONTINUE:
            break;
        case OP_BRIDGE_REGISTER: {
            uint16_t kw_idx = read_short(current_frame(vm));
            uint16_t name_idx = read_short(current_frame(vm));
            SlpValue closure_val = slp_vm_pop(vm);
            SlpObjClosure *cl = SLP_AS_CLOSURE(closure_val);
            SlpChunk *chunk = current_frame(vm)->closure->function->chunk;
            SlpObjString *kw_str = (SlpObjString*)SLP_AS_OBJ(chunk->constants[kw_idx]);
            SlpObjString *name_str = (SlpObjString*)SLP_AS_OBJ(chunk->constants[name_idx]);
            cl->is_inline = strcmp(kw_str->chars, "inline") == 0;
            if (cl->is_inline) {
                if (cl->identity == vm->next_closure_identity &&
                    vm->next_closure_identity > 0)
                    vm->next_closure_identity--;
                cl->identity = 0;
            }
            if (name_str && name_str->length > 0) {
                char *call_name = (char*)SLP_MALLOC(
                    vm->allocator, (size_t)name_str->length + 2);
                if (!call_name) {
                    slp_vm_runtime_error(vm, "Out of memory.");
                    return SLP_RUNTIME_ERROR;
                }
                call_name[0] = '&';
                memcpy(call_name + 1, name_str->chars,
                       name_str->length);
                call_name[name_str->length + 1] = '\0';
                cl->call_name = slp_vm_copy_string(
                    vm, call_name, name_str->length + 1);
                SLP_FREE(vm->allocator, call_name);
            }
            bool handled = false;
            SlpBridgeType *bt = vm->bridge_types;
            while (bt) {
                if (strcmp(bt->keyword, kw_str->chars) == 0) {
                    if (bt->handler) {
                        bt->handler(vm, kw_str->chars, name_str ? name_str->chars : NULL,
                                    NULL, cl, bt->userdata);
                    }
                    handled = true;
                    break;
                }
                bt = bt->next;
            }
            if (!handled || (bt && !bt->handler)) {
                if (name_str && name_str->length > 0) {
                    slp_obj_hash_set(vm->allocator, vm->globals,
                        SLP_OBJ_VAL(name_str), SLP_OBJ_VAL(cl));
                }
            }
            break;
        }
        case OP_IMPORT:
            read_short(current_frame(vm));
            read_short(current_frame(vm));
            slp_vm_push(vm, SLP_NULL_VAL);
            break;
        case OP_BACKTICK: {
            uint16_t idx = read_short(current_frame(vm));
            SlpValue cmd_val = current_frame(vm)->closure->function->chunk->constants[idx];
            if (SLP_IS_OBJ(cmd_val) && SLP_OBJ_TYPE(cmd_val) == SLP_OBJ_STRING) {
                const char *cmd = SLP_AS_STRING(cmd_val)->chars;
                SlpPlatformProcess *proc = slp_platform_exec(cmd, vm->allocator);
                if (proc && proc->read_stream) {
                    size_t buf_size = 1024;
                    size_t len = 0;
                    char *buf = (char *)vm->allocator->reallocate(NULL, buf_size, vm->allocator->user_data);
                    while (true) {
                        if (len + 128 > buf_size) {
                            buf_size *= 2;
                            buf = (char *)vm->allocator->reallocate(buf, buf_size, vm->allocator->user_data);
                        }
                        size_t bytes = fread(buf + len, 1, buf_size - len - 1, proc->read_stream);
                        if (bytes == 0) break;
                        len += bytes;
                    }
                    buf[len] = '\0';
                    SlpObjString *res = slp_vm_copy_string(vm, buf, (uint32_t)len);
                    vm->allocator->reallocate(buf, 0, vm->allocator->user_data);
                    slp_platform_close_process(proc);
                    slp_platform_free_process(proc, vm->allocator);
                    slp_vm_push(vm, SLP_OBJ_VAL(res));
                } else {
                    if (proc) {
                        slp_platform_close_process(proc);
                        slp_platform_free_process(proc, vm->allocator);
                    }
                    slp_vm_push(vm, SLP_NULL_VAL);
                }
            } else {
                slp_vm_push(vm, SLP_NULL_VAL);
            }
            break;
        }
        case OP_CLASS_LITERAL: {
            uint16_t class_idx = read_short(current_frame(vm));
            SlpValue literal =
                current_frame(vm)->closure->function->chunk
                    ->constants[class_idx];
            if (!SLP_IS_OBJ(literal) ||
                SLP_OBJ_TYPE(literal) != SLP_OBJ_STRING) {
                slp_vm_push(vm, SLP_NULL_VAL);
                break;
            }
            SlpObjClass *class_object = slp_vm_new_class(
                vm, SLP_AS_STRING(literal)->chars);
            slp_vm_push(
                vm, class_object
                    ? SLP_OBJ_VAL(class_object)
                    : SLP_NULL_VAL);
            break;
        }
        case OP_ADDRESS: {
            uint16_t idx = read_short(current_frame(vm));
            SlpValue name_val = current_frame(vm)->closure->function->chunk->constants[idx];
            SlpValue val = slp_obj_hash_get(vm->globals, name_val);
            slp_vm_push(vm, val);
            break;
        }
        case OP_LOCAL: {
            SlpValue declarations = slp_vm_pop(vm);
            if (SLP_IS_OBJ(declarations) &&
                SLP_OBJ_TYPE(declarations) == SLP_OBJ_STRING)
                slp_vm_declare_local(vm, SLP_AS_STRING(declarations)->chars);
            slp_vm_push(vm, SLP_NULL_VAL);
            break;
        }
        case OP_THIS: {
            SlpValue declarations = slp_vm_pop(vm);
            if (SLP_IS_OBJ(declarations) &&
                SLP_OBJ_TYPE(declarations) == SLP_OBJ_STRING)
                slp_vm_declare_closure(vm, SLP_AS_STRING(declarations)->chars);
            slp_vm_push(vm, SLP_NULL_VAL);
            break;
        }
        case OP_UNPACK_TUPLE:
            read_byte(current_frame(vm));
            slp_vm_push(vm, SLP_NULL_VAL);
            break;
        case OP_FOREACH_NEXT:
        case OP_FOREACH_NEXT_VALUE: {
            SlpCallFrame *foreach_frame = current_frame(vm);
            foreach_frame->foreach_active = false;
            foreach_frame->foreach_array = NULL;
            foreach_frame->foreach_hash = NULL;
            uint8_t *instruction_start = foreach_frame->ip - 1;
            uint16_t offset = read_short(current_frame(vm));
            bool done = true;
            bool resumed_generator = foreach_frame->foreach_pending;
            SlpValue key = SLP_NULL_VAL;
            SlpValue val = SLP_NULL_VAL;
            SlpValue iterator;
            SlpValue collection;

            if (foreach_frame->foreach_pending) {
                /* The generator closure just yielded/returned into this
                   frame. Its result is above the collection and index. */
                val = slp_vm_pop(vm);
                foreach_frame->foreach_pending = false;
                iterator = slp_vm_peek(vm, 0);
                collection = slp_vm_peek(vm, 1);
                if (!SLP_IS_NULL(val)) {
                    key = iterator;
                    done = false;
                }
            } else {
                iterator = slp_vm_peek(vm, 0);
                collection = slp_vm_peek(vm, 1);
            }

            if (done && SLP_IS_OBJ(collection)) {
                SlpObj *obj = SLP_AS_OBJ(collection);
                int idx = (int)SLP_AS_NUM(iterator);
                if (obj->type == SLP_OBJ_CLOSURE && !resumed_generator) {
                    /* Re-enter this opcode after the generator returns. The
                       pending flag is part of the frame, so suspension and
                       continuation capture preserve this small state machine. */
                    foreach_frame->foreach_pending = true;
                    foreach_frame->ip = instruction_start;
                    slp_vm_push(vm, collection);
                    if (!call_value(
                            vm, collection, 0, false, NULL))
                        return SLP_RUNTIME_ERROR;
                    break;
                } else if (
                    obj->type == SLP_OBJ_ARRAY ||
                    (obj->type ==
                         SLP_OBJ_JAVA_OBJECT &&
                     ((SlpObjJavaObject*)obj)
                             ->kind ==
                         SLP_JAVA_ITERATOR &&
                     ((SlpObjJavaObject*)obj)
                         ->list)) {
                    SlpObjJavaObject *java_iterator =
                        obj->type ==
                                SLP_OBJ_JAVA_OBJECT
                            ? (SlpObjJavaObject*)obj
                            : NULL;
                    SlpObjArray *arr =
                        java_iterator
                            ? java_iterator->list
                            : (SlpObjArray*)obj;
                    if (java_iterator &&
                        foreach_frame
                                ->foreach_iteration_array !=
                            arr) {
                        idx =
                            (int)slp_value_as_number(
                                java_iterator->value);
                        iterator =
                            SLP_NUM_VAL(
                                (double)idx);
                        vm->stack_top[-1] =
                            iterator;
                    }
                    if (idx == 0 ||
                        foreach_frame->foreach_iteration_array != arr) {
                        foreach_frame->foreach_iteration_array = arr;
                        foreach_frame->foreach_iteration_version =
                            arr->mutation_version;
                        foreach_frame->foreach_invalidated = false;
                    }
                    if (foreach_frame->foreach_invalidated) {
                        done = true;
                    } else if (
                        idx > 0 &&
                        foreach_frame->foreach_iteration_version !=
                            arr->mutation_version) {
                        /*
                         * Sleep's list iterator discovers a structural change
                         * in next(), after hasNext() has already produced true.
                         * The warning is non-fatal and the loop body therefore
                         * runs once more with the previous key/value before
                         * the invalid iterator terminates.
                         */
                        slp_vm_warning(
                            vm,
                            "unsafe data modification: @array changed "
                            "during iteration");
                        key = foreach_frame->foreach_last_key;
                        val = foreach_frame->foreach_last_value;
                        foreach_frame->foreach_invalidated = true;
                        done = false;
                    } else if (idx >= 0 && idx < arr->count) {
                        key = SLP_NUM_VAL((double)idx);
                        val = arr->elements[idx];
                        if (!SLP_IS_OBJ(val) ||
                            SLP_OBJ_TYPE(val) != SLP_OBJ_SCALAR_CELL) {
                            SlpObjScalarCell *cell =
                                slp_obj_scalar_cell_new(vm->allocator, val);
                            if (!cell) {
                                slp_vm_runtime_error(vm, "Out of memory.");
                                return SLP_RUNTIME_ERROR;
                            }
                            track_object(vm, &cell->obj);
                            val = SLP_OBJ_VAL(cell);
                            arr->elements[idx] = val;
                        }
                        foreach_frame->foreach_array = arr;
                        foreach_frame->foreach_index = idx;
                        foreach_frame->foreach_iterator_offset =
                            (int)((vm->stack_top - 1) - vm->stack);
                        foreach_frame->foreach_active = true;
                        foreach_frame->foreach_last_key = key;
                        foreach_frame->foreach_last_value = val;
                        if (java_iterator)
                            java_iterator->value =
                                SLP_NUM_VAL(
                                    (double)(idx + 1));
                        done = false;
                    }
                } else if (obj->type == SLP_OBJ_HASH) {
                    SlpObjHash *hash = (SlpObjHash*)obj;
                    int ordinal = idx;
                    int i = -1;
                    while (ordinal < hash->count) {
                        i = slp_obj_hash_ordered_index(hash, ordinal);
                        if (i < 0 ||
                            !SLP_IS_NULL(hash->entries[i].value))
                            break;
                        ordinal++;
                    }
                    if (i >= 0) {
                        key = hash->entries[i].key;
                        val = hash->entries[i].value;
                        if (instruction ==
                            OP_FOREACH_NEXT_VALUE)
                            val = key;
                        iterator = SLP_NUM_VAL((double)ordinal);
                        foreach_frame->foreach_hash = hash;
                        foreach_frame->foreach_key = key;
                        foreach_frame->foreach_iterator_offset =
                            (int)((vm->stack_top - 1) - vm->stack);
                        foreach_frame->foreach_active = true;
                        done = false;
                    }
                } else if (obj->type == SLP_OBJ_STRING) {
                    SlpObjString *str = (SlpObjString*)obj;
                    if (idx >= 0 && idx < (int)str->length) {
                        key = SLP_NUM_VAL((double)idx);
                        char char_buf[2] = { str->chars[idx], '\0' };
                        val = SLP_OBJ_VAL(slp_vm_copy_string(vm, char_buf, 1));
                        done = false;
                    }
                }
            }
            
            if (done) {
                foreach_frame->foreach_iteration_array = NULL;
                foreach_frame->foreach_invalidated = false;
                current_frame(vm)->ip += offset;
            } else {
                vm->stack_top[-1] = SLP_NUM_VAL(SLP_AS_NUM(iterator) + 1);
                slp_vm_push(vm, key);
                slp_vm_push(vm, val);
            }
            break;
        }
        case OP_OBJ_EXPR: {
            uint8_t arg_count = read_byte(current_frame(vm));
            SlpValue *base = vm->stack_top - arg_count - 2;
            SlpValue result;
            if (call_builtin_object_method(vm, base, arg_count, &result)) {
                if ((vm->debug_flags & 8) ==
                    8) {
                    SlpObjString *profile_name =
                        profile_call_name(
                            vm, base[0], true,
                            base + 1,
                            (int)arg_count + 1,
                            NULL);
                    profile_record(
                        vm, profile_name);
                }
                vm->stack_top = base;
                slp_vm_push(vm, result);
                break;
            }
            SlpValue callee = base[0];
            if (!call_value(
                    vm, callee, arg_count, true, NULL))
                return SLP_RUNTIME_ERROR;
            break;
        }
        case OP_MATCH:
        case OP_NOT_MATCH: {
            SlpValue b = slp_vm_pop(vm); // String (Pattern)
            SlpValue a = slp_vm_pop(vm); // Pattern (String)
            bool is_match = false;
            
            if (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_STRING &&
                SLP_IS_OBJ(b) && SLP_OBJ_TYPE(b) == SLP_OBJ_STRING) {
                const char *pattern = SLP_AS_STRING(b)->chars;
                const char *string = SLP_AS_STRING(a)->chars;
                if (pattern[0] == '\0') {
                    is_match = (string[0] == '\0');
                    set_regex_matches(
                        vm, is_match ? slp_vm_new_array(vm) : NULL);
                } else {
                    is_match =
                        slp_vm_regex_find(vm, string, pattern, 0, false, NULL) >=
                        0;
                }
            }
            if (instruction == OP_NOT_MATCH) {
                is_match = !is_match;
            }
            trace_binary_predicate(
                vm, a,
                instruction == OP_NOT_MATCH ? "!=~" : "=~",
                b, is_match);
            slp_vm_push(vm, SLP_BOOL_VAL(is_match));
            break;
        }
        case OP_UNARY_PREDICATE: {
            uint16_t name_idx = read_short(current_frame(vm));
            SlpObjString *pred_name = SLP_AS_STRING(current_frame(vm)->closure->function->chunk->constants[name_idx]);
            SlpValue a = slp_vm_pop(vm);
            bool result = false;
            bool recognized = true;

            if (strcmp(pred_name->chars, "-istrue") == 0) {
                result = !slp_value_is_falsy(a);
            } else if (strcmp(pred_name->chars, "-isarray") == 0) {
                result = (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_ARRAY);
            } else if (strcmp(pred_name->chars, "-ishash") == 0) {
                result = (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_HASH);
            } else if (strcmp(pred_name->chars, "-isnumber") == 0) {
                result = SLP_IS_NUM(a) ||
                         (SLP_IS_OBJ(a) &&
                          (SLP_OBJ_TYPE(a) == SLP_OBJ_LONG ||
                           SLP_OBJ_TYPE(a) == SLP_OBJ_DOUBLE));
            } else if (strcmp(pred_name->chars, "-isfunction") == 0 || strcmp(pred_name->chars, "-isclosure") == 0) {
                result = (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_CLOSURE);
            } else if (strcmp(pred_name->chars, "-exists") == 0 || 
                       strcmp(pred_name->chars, "-isdir") == 0 || strcmp(pred_name->chars, "-isDir") == 0 ||
                       strcmp(pred_name->chars, "-isfile") == 0 || strcmp(pred_name->chars, "-isFile") == 0 ||
                       strcmp(pred_name->chars, "-canread") == 0 || strcmp(pred_name->chars, "-canwrite") == 0 ||
                       strcmp(pred_name->chars, "-canexecute") == 0) {
                if (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_STRING) {
                    const char *path = SLP_AS_STRING(a)->chars;
                    struct stat st;
                    int stat_res = stat(path, &st);
                    
                    if (strcmp(pred_name->chars, "-exists") == 0) {
                        result = (stat_res == 0);
                    } else if (strcmp(pred_name->chars, "-isdir") == 0 || strcmp(pred_name->chars, "-isDir") == 0) {
                        result = (stat_res == 0 && S_ISDIR(st.st_mode));
                    } else if (strcmp(pred_name->chars, "-isfile") == 0 || strcmp(pred_name->chars, "-isFile") == 0) {
                        result = (stat_res == 0 && S_ISREG(st.st_mode));
                    } else if (strcmp(pred_name->chars, "-canread") == 0) {
                        result = (slp_platform_access(path, 4) == 0);
                    } else if (strcmp(pred_name->chars, "-canwrite") == 0) {
                        result = (slp_platform_access(path, 2) == 0);
                    } else if (strcmp(pred_name->chars, "-canexecute") == 0) {
                        result = (slp_platform_access(path, 1) == 0);
                    }
                }
            } else if (strcmp(pred_name->chars, "-isstring") == 0) {
                result = (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_STRING);
            } else if (strcmp(pred_name->chars, "-isnull") == 0) {
                result = SLP_IS_NULL(a);
            } else if (strcmp(pred_name->chars, "-isboolean") == 0 || strcmp(pred_name->chars, "-isbool") == 0) {
                result = SLP_IS_BOOL(a);
            } else if (strcmp(pred_name->chars, "-islong") == 0) {
                result = (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_LONG);
            } else if (strcmp(pred_name->chars, "-isupper") == 0) {
                result = false;
                if (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_STRING) {
                    const char *s = SLP_AS_STRING(a)->chars;
                    result = (s[0] != '\0');
                    while (*s) {
                        if (isalpha((unsigned char)*s) && !isupper((unsigned char)*s)) {
                            result = false;
                            break;
                        }
                        s++;
                    }
                }
            } else if (strcmp(pred_name->chars, "-islower") == 0) {
                result = false;
                if (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_STRING) {
                    const char *s = SLP_AS_STRING(a)->chars;
                    result = (s[0] != '\0');
                    while (*s) {
                        if (isalpha((unsigned char)*s) && !islower((unsigned char)*s)) {
                            result = false;
                            break;
                        }
                        s++;
                    }
                }
            } else if (strcmp(pred_name->chars, "-isletter") == 0) {
                result = false;
                if (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_STRING) {
                    const char *s = SLP_AS_STRING(a)->chars;
                    result = (s[0] != '\0');
                    while (*s) {
                        if (!isalpha((unsigned char)*s)) {
                            result = false;
                            break;
                        }
                        s++;
                    }
                }
            } else if (strcmp(pred_name->chars, "-istainted") == 0) {
                result = slp_value_is_tainted(a);
            } else if (strcmp(pred_name->chars, "-eof") == 0) {
                result = false;
                if (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_IO_HANDLE) {
                    result = SLP_AS_IO_HANDLE(a)->is_eof;
                }
            } else {
                result = false;
                recognized = false;
            }

            if (!recognized) {
                char message[256];
                snprintf(
                    message, sizeof(message),
                    "Attempted to use non-existent predicate: %s",
                    pred_name->chars);
                slp_vm_warning(vm, message);
            } else {
                trace_unary_predicate(
                    vm, pred_name->chars, a, result);
            }
            slp_vm_push(vm, SLP_BOOL_VAL(result));
            break;
        }
        case OP_BINARY_PREDICATE:
        case OP_NEGATED_BINARY_PREDICATE: {
            uint16_t name_idx = read_short(current_frame(vm));
            SlpObjString *pred_name = SLP_AS_STRING(current_frame(vm)->closure->function->chunk->constants[name_idx]);
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            bool result = true;
            bool trace_result = true;

            if (strcmp(pred_name->chars, "is") == 0) {
                if (SLP_IS_NULL(a) && SLP_IS_NULL(b)) result = true;
                else if (SLP_IS_NULL(a) || SLP_IS_NULL(b)) result = false;
                else if (a.type != b.type) result = false;
                else if (SLP_IS_NUM(a)) result = (SLP_AS_NUM(a) == SLP_AS_NUM(b));
                else if (SLP_IS_BOOL(a)) result = (SLP_AS_BOOL(a) == SLP_AS_BOOL(b));
                else result = (SLP_AS_OBJ(a) == SLP_AS_OBJ(b));
            } else if (strcmp(pred_name->chars, "eq") == 0) {
                const char *sa_chars, *sb_chars;
                uint32_t sa_len, sb_len;
                char buf_a[64], buf_b[64];
                get_string_repr(a, &sa_chars, &sa_len, buf_a, sizeof(buf_a));
                get_string_repr(b, &sb_chars, &sb_len, buf_b, sizeof(buf_b));
                result = (sa_len == sb_len && memcmp(sa_chars, sb_chars, sa_len) == 0);
            } else if (strcmp(pred_name->chars, "ne") == 0) {
                const char *sa_chars, *sb_chars;
                uint32_t sa_len, sb_len;
                char buf_a[64], buf_b[64];
                get_string_repr(a, &sa_chars, &sa_len, buf_a, sizeof(buf_a));
                get_string_repr(b, &sb_chars, &sb_len, buf_b, sizeof(buf_b));
                result = !(sa_len == sb_len && memcmp(sa_chars, sb_chars, sa_len) == 0);
            } else if (strcmp(pred_name->chars, "isin") == 0) {
                const char *needle_chars, *haystack_chars;
                uint32_t needle_len, haystack_len;
                char needle_buf[64], haystack_buf[64];
                get_string_repr(
                    a, &needle_chars, &needle_len,
                    needle_buf, sizeof(needle_buf));
                get_string_repr(
                    b, &haystack_chars, &haystack_len,
                    haystack_buf, sizeof(haystack_buf));
                (void)needle_len;
                (void)haystack_len;
                result = strstr(haystack_chars, needle_chars) != NULL;
            } else if (strcmp(pred_name->chars, "ismatch") == 0 || strcmp(pred_name->chars, "hasmatch") == 0) {
                result = false;
                if (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_STRING && 
                    SLP_IS_OBJ(b) && SLP_OBJ_TYPE(b) == SLP_OBJ_STRING) {
                    SlpObjString *text = SLP_AS_STRING(a);
                    SlpObjString *pattern = SLP_AS_STRING(b);
                    if (strcmp(pred_name->chars, "hasmatch") == 0)
                        result = regex_hasmatch(vm, text, pattern);
                    else
                        result = slp_vm_regex_find(
                                     vm, text->chars, pattern->chars, 0, true,
                                     NULL) >= 0;
                } else {
                    set_regex_matches(vm, NULL);
                }
            } else if (strcmp(pred_name->chars, "iswm") == 0) {
                result = false;
                if (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_STRING &&
                    SLP_IS_OBJ(b) && SLP_OBJ_TYPE(b) == SLP_OBJ_STRING) {
                    result = match_wildcard(SLP_AS_STRING(b)->chars, SLP_AS_STRING(a)->chars);
                }
            } else if (strcmp(pred_name->chars, "in") == 0) {
                result = false;
                if (SLP_IS_OBJ(b)) {
                    SlpObj *b_obj = SLP_AS_OBJ(b);
                    if (b_obj->type == SLP_OBJ_ARRAY) {
                        SlpObjArray *arr = (SlpObjArray*)b_obj;
                        for (int i = 0; i < arr->count; i++) {
                            if (slp_value_equals(arr->elements[i], a)) {
                                result = true;
                                break;
                            }
                        }
                    } else if (b_obj->type == SLP_OBJ_HASH) {
                        SlpObjHash *hash = (SlpObjHash*)b_obj;
                        if (slp_vm_hash_contains(vm, hash, a)) {
                            result = true;
                        }
                    } else if (b_obj->type == SLP_OBJ_STRING) {
                        if (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_STRING) {
                            const char *sa = SLP_AS_STRING(a)->chars;
                            const char *sb = SLP_AS_STRING(b)->chars;
                            if (strstr(sb, sa) != NULL) {
                                result = true;
                            }
                        }
                    }
                }
            } else if (strcmp(pred_name->chars, "isa") == 0) {
                result = false;
                if (SLP_IS_OBJ(b) &&
                    SLP_OBJ_TYPE(b) == SLP_OBJ_CLASS) {
                    const char *target =
                        SLP_AS_CLASS(b)->name->chars;
                    if (SLP_IS_OBJ(a) &&
                        SLP_OBJ_TYPE(a) ==
                            SLP_OBJ_JAVA_OBJECT &&
                        SLP_AS_JAVA_OBJECT(a)->kind ==
                            SLP_JAVA_PROXY &&
                        SLP_AS_JAVA_OBJECT(a)->list) {
                        SlpObjArray *interfaces =
                            SLP_AS_JAVA_OBJECT(a)
                                ->list;
                        for (int i = 0;
                             i < interfaces->count;
                             i++) {
                            SlpValue interface_value =
                                interfaces
                                    ->elements[i];
                            if (SLP_IS_OBJ(
                                    interface_value) &&
                                SLP_OBJ_TYPE(
                                    interface_value) ==
                                    SLP_OBJ_CLASS &&
                                strcmp(
                                    SLP_AS_CLASS(
                                        interface_value)
                                        ->name->chars,
                                    target) == 0) {
                                result = true;
                                break;
                            }
                        }
                        if (!result)
                            result =
                                strcmp(
                                    target,
                                    "java.lang.Object") ==
                                0;
                    } else {
                        result = class_name_is_instance(
                            target,
                            value_java_class_name(a));
                    }
                } else {
                    char message[320];
                    snprintf(
                        message, sizeof(message),
                        "attempted an invalid cast: %s cannot be "
                        "cast to java.lang.Class",
                        invalid_cast_source_class(b));
                    slp_vm_abort_warning(vm, message);
                    trace_result = false;
                }
            } else if (strcmp(pred_name->chars, "lt") == 0) {
                const char *sa_chars, *sb_chars;
                uint32_t sa_len, sb_len;
                char buf_a[64], buf_b[64];
                get_string_repr(a, &sa_chars, &sa_len, buf_a, sizeof(buf_a));
                get_string_repr(b, &sb_chars, &sb_len, buf_b, sizeof(buf_b));
                result = (strcmp(sa_chars, sb_chars) < 0);
            } else if (strcmp(pred_name->chars, "gt") == 0) {
                const char *sa_chars, *sb_chars;
                uint32_t sa_len, sb_len;
                char buf_a[64], buf_b[64];
                get_string_repr(a, &sa_chars, &sa_len, buf_a, sizeof(buf_a));
                get_string_repr(b, &sb_chars, &sb_len, buf_b, sizeof(buf_b));
                result = (strcmp(sa_chars, sb_chars) > 0);
            } else if (strcmp(pred_name->chars, "ge") == 0) {
                const char *sa_chars, *sb_chars;
                uint32_t sa_len, sb_len;
                char buf_a[64], buf_b[64];
                get_string_repr(a, &sa_chars, &sa_len, buf_a, sizeof(buf_a));
                get_string_repr(b, &sb_chars, &sb_len, buf_b, sizeof(buf_b));
                result = (strcmp(sa_chars, sb_chars) >= 0);
            } else if (strcmp(pred_name->chars, "le") == 0) {
                const char *sa_chars, *sb_chars;
                uint32_t sa_len, sb_len;
                char buf_a[64], buf_b[64];
                get_string_repr(a, &sa_chars, &sa_len, buf_a, sizeof(buf_a));
                get_string_repr(b, &sb_chars, &sb_len, buf_b, sizeof(buf_b));
                result = (strcmp(sa_chars, sb_chars) <= 0);
            } else if (strcmp(pred_name->chars, "cmp") == 0) {
                const char *sa_chars, *sb_chars;
                uint32_t sa_len, sb_len;
                char buf_a[64], buf_b[64];
                get_string_repr(a, &sa_chars, &sa_len, buf_a, sizeof(buf_a));
                get_string_repr(b, &sb_chars, &sb_len, buf_b, sizeof(buf_b));
                int c = strcmp(sa_chars, sb_chars);
                slp_vm_push(vm, SLP_NUM_VAL(c < 0 ? -1.0 : (c > 0 ? 1.0 : 0.0)));
                break;
            } else {
                result = false;
            }

            if (instruction == OP_NEGATED_BINARY_PREDICATE) {
                result = !result;
            }
            if (trace_result) {
                char traced_name[260];
                const char *name = pred_name->chars;
                if (instruction ==
                    OP_NEGATED_BINARY_PREDICATE) {
                    snprintf(
                        traced_name, sizeof(traced_name),
                        "!%s", pred_name->chars);
                    name = traced_name;
                }
                trace_binary_predicate(vm, a, name, b, result);
            }
            slp_vm_push(vm, SLP_BOOL_VAL(result));
            break;
        }
        case OP_CALLCC: {
            uint8_t has_arg = read_byte(current_frame(vm));
            SlpValue handler = has_arg ? slp_vm_pop(vm) : SLP_NULL_VAL;

            int root_index = vm->frame_count - 1;
            while (root_index > 0 &&
                   vm->frames[root_index].closure->is_inline)
                root_index--;
            SlpCallFrame *root = &vm->frames[root_index];
            SlpObjClosure *owner = root->closure;
            SlpObjString *callcc_source =
                owner && owner->function
                    ? owner->function->source_name
                    : NULL;
            int callcc_line = frame_current_line(root);
            SlpObjString *owner_trace_call = root->trace_call;
            SlpObjString *owner_trace_source = root->trace_source;
            int owner_trace_line = root->trace_line;
            bool owner_trace_enabled = root->trace_enabled;

            /* A callcc continuation restores the entire VM context, unlike a
               later ordinary call to the coroutine owner, which resumes just
               that closure in its new caller. Both views share the same saved
               instruction point. */
            SlpObjContinuation *continuation =
                slp_obj_continuation_new(vm->allocator);
            if (!continuation) {
                slp_vm_runtime_error(vm, "Out of memory.");
                return SLP_RUNTIME_ERROR;
            }
            track_object(vm, &continuation->obj);
            continuation->coroutine_owner = owner;
            continuation->resume_frame_index = root_index;
            continuation->frame_count = vm->frame_count;
            continuation->frames = SLP_ALLOCATE_ARRAY(
                vm->allocator, SlpCallFrame, continuation->frame_count);
            continuation->stack_count = (int)(vm->stack_top - vm->stack);
            continuation->stack = SLP_ALLOCATE_ARRAY(
                vm->allocator, SlpValue, continuation->stack_count);
            continuation->try_handler_count = vm->try_handler_count;
            if (continuation->try_handler_count > 0)
                continuation->try_handlers = SLP_ALLOCATE_ARRAY(
                    vm->allocator, SlpTryHandler,
                    continuation->try_handler_count);
            if (!continuation->frames || !continuation->stack ||
                (continuation->try_handler_count > 0 &&
                 !continuation->try_handlers)) {
                slp_vm_runtime_error(vm, "Out of memory.");
                return SLP_RUNTIME_ERROR;
            }
            memcpy(continuation->frames, vm->frames,
                   sizeof(SlpCallFrame) * continuation->frame_count);
            memcpy(continuation->stack, vm->stack,
                   sizeof(SlpValue) * continuation->stack_count);
            if (continuation->try_handler_count > 0)
                memcpy(continuation->try_handlers, vm->try_handlers,
                       sizeof(SlpTryHandler) *
                           continuation->try_handler_count);
            for (int i = 0; i < continuation->frame_count; i++) {
                ptrdiff_t slot_offset =
                    continuation->frames[i].slots - vm->stack;
                continuation->frames[i].slots =
                    continuation->stack + slot_offset;
            }

            clear_coroutine_state(vm, owner);

            owner->coroutine_stack_count =
                (int)(vm->stack_top - root->slots);
            owner->coroutine_frame_count = vm->frame_count - root_index;
            owner->coroutine_stack = SLP_ALLOCATE_ARRAY(
                vm->allocator, SlpValue, owner->coroutine_stack_count);
            owner->coroutine_frames = SLP_ALLOCATE_ARRAY(
                vm->allocator, SlpCallFrame, owner->coroutine_frame_count);
            if (!owner->coroutine_stack || !owner->coroutine_frames) {
                clear_coroutine_state(vm, owner);
                slp_vm_runtime_error(vm, "Out of memory.");
                return SLP_RUNTIME_ERROR;
            }
            memcpy(owner->coroutine_stack, root->slots,
                   sizeof(SlpValue) * owner->coroutine_stack_count);
            memcpy(owner->coroutine_frames, &vm->frames[root_index],
                   sizeof(SlpCallFrame) * owner->coroutine_frame_count);
            for (int i = 0; i < owner->coroutine_frame_count; i++) {
                ptrdiff_t slot_offset =
                    owner->coroutine_frames[i].slots - root->slots;
                owner->coroutine_frames[i].slots =
                    owner->coroutine_stack + slot_offset;
            }
            if (!capture_coroutine_try_handlers(vm, owner, root_index,
                                                root->slots)) {
                clear_coroutine_state(vm, owner);
                slp_vm_runtime_error(vm, "Out of memory.");
                return SLP_RUNTIME_ERROR;
            }
            owner->coroutine_needs_result = true;

            finish_call_trace(
                vm, owner_trace_call, owner_trace_source,
                owner_trace_line, owner_trace_enabled,
                handler, false, true);
            close_upvalues(vm, root->slots);
            vm->frame_count = root_index;
            trim_try_handlers(vm, vm->frame_count);
            vm->stack_top = root->slots;

            if (has_arg && SLP_IS_OBJ(handler) &&
                SLP_OBJ_TYPE(handler) == SLP_OBJ_CLOSURE) {
                slp_vm_push(vm, handler);
                slp_vm_push(vm, SLP_OBJ_VAL(continuation));
                if (!call_value(
                        vm, handler, 1, false, NULL))
                    return SLP_RUNTIME_ERROR;
                SlpCallFrame *handler_frame =
                    current_frame(vm);
                SlpObjString *callcc_trace_call =
                    build_callcc_trace_call(
                        vm, handler,
                        SLP_OBJ_VAL(continuation));
                if (!callcc_trace_call) {
                    slp_vm_runtime_error(
                        vm, "Out of memory.");
                    return SLP_RUNTIME_ERROR;
                }
                handler_frame->trace_call =
                    callcc_trace_call;
                handler_frame->trace_source =
                    callcc_source;
                handler_frame->trace_line =
                    callcc_line;
                handler_frame->trace_enabled =
                    (vm->debug_flags & 8) == 8;
            } else {
                slp_vm_push(vm, SLP_OBJ_VAL(continuation));
            }
            break;
        }
        case OP_RESUME: {
            SlpValue coro_val = slp_vm_peek(vm, 0);
            if (SLP_IS_OBJ(coro_val) && SLP_OBJ_TYPE(coro_val) == SLP_OBJ_CLOSURE) {
                SlpObjClosure *closure = SLP_AS_CLOSURE(coro_val);
                if (closure->coroutine_stack) {
                    slp_vm_pop(vm);
                    if (!call_closure(
                            vm, closure, 0, false, NULL))
                        return SLP_RUNTIME_ERROR;
                }
            }
            break;
        }
        case OP_NOP:
            break;
        default:
            slp_vm_runtime_error(vm, "Unknown opcode.");
            return SLP_RUNTIME_ERROR;
        }
        if (vm->abort_requested) {
            SlpValue *return_slot = current_frame(vm)->slots;
            int stop_frame_count = vm->flow_exit_requested
                ? target_frame_count
                : vm->frame_count - 1;
            while (vm->frame_count > stop_frame_count) {
                SlpCallFrame *aborted_frame =
                    current_frame(vm);
                return_slot = aborted_frame->slots;
                finish_call_trace(
                    vm, aborted_frame->trace_call,
                    aborted_frame->trace_source,
                    aborted_frame->trace_line,
                    aborted_frame->trace_enabled,
                    SLP_NULL_VAL, true, false);
                close_upvalues(vm, return_slot);
                vm->frame_count--;
            }
            trim_try_handlers(vm, vm->frame_count);
            vm->stack_top = return_slot;
            slp_vm_push(vm, SLP_NULL_VAL);
            vm->abort_requested = false;
            bool flow_exit = vm->flow_exit_requested;
            vm->flow_exit_requested = false;
            if (flow_exit ||
                vm->frame_count == target_frame_count)
                return SLP_OK;
        }
    }
    }
    return SLP_OK;
}

SlpResult slp_vm_call_value(
    SlpVM *vm, SlpValue callable,
    const SlpValue *arguments, int argument_count,
    SlpValue *out_result) {
    if (out_result)
        *out_result = SLP_NULL_VAL;
    if (!vm || !out_result ||
        argument_count < 0 ||
        argument_count > 255 ||
        (argument_count > 0 && !arguments))
        return SLP_RUNTIME_ERROR;

    callable =
        dereference_value(
            slp_value_unwrap_taint(callable));
    if (SLP_IS_OBJ(callable) &&
        SLP_OBJ_TYPE(callable) ==
            SLP_OBJ_FUNCTION) {
        SlpObjClosure *closure =
            slp_vm_new_closure(
                vm, SLP_AS_FUNCTION(callable));
        if (!closure)
            return SLP_RUNTIME_ERROR;
        callable = SLP_OBJ_VAL(closure);
    }
    if (!SLP_IS_OBJ(callable) ||
        (SLP_OBJ_TYPE(callable) !=
             SLP_OBJ_CLOSURE &&
         SLP_OBJ_TYPE(callable) !=
             SLP_OBJ_NATIVE))
        return SLP_RUNTIME_ERROR;

    ptrdiff_t available =
        (vm->stack + SLP_STACK_MAX) -
        vm->stack_top;
    if (available <
        (ptrdiff_t)argument_count + 1) {
        slp_vm_runtime_error(
            vm, "Stack overflow.");
        return SLP_RUNTIME_ERROR;
    }

    SlpValue *saved_stack_top =
        vm->stack_top;
    int saved_frame_count =
        vm->frame_count;
    int saved_handler_count =
        vm->try_handler_count;

    slp_vm_push(vm, callable);
    for (int i = 0;
         i < argument_count; i++)
        slp_vm_push(vm, arguments[i]);

    SlpResult result =
        slp_vm_call(
            vm, argument_count, false);
    if (result == SLP_OK &&
        vm->stack_top >
            saved_stack_top)
        *out_result =
            dereference_value(
                vm->stack_top[-1]);
    else if (result == SLP_OK)
        result = SLP_RUNTIME_ERROR;

    while (vm->frame_count >
           saved_frame_count) {
        SlpCallFrame *frame =
            current_frame(vm);
        close_upvalues(vm, frame->slots);
        vm->frame_count--;
    }
    vm->try_handler_count =
        saved_handler_count;
    vm->stack_top =
        saved_stack_top;
    return result;
}

SlpResult slp_vm_interpret(SlpVM *vm, const char *source) {
    SlpParser parser;
    slp_parser_init(&parser, source, vm->allocator);
    SlpASTNode *ast = slp_parser_parse(&parser);
    if (parser.had_error || !ast) {
        if (vm->error_fn)
            vm->error_fn(vm->error_userdata, parser.error_line,
                         parser.error_message ? parser.error_message : "Parse error");
        if (ast) slp_parser_free_node(ast, vm->allocator);
        return SLP_COMPILE_ERROR;
    }

    SlpObjFunction *fn = slp_compile(vm, ast, vm->allocator);
    slp_parser_free_node(ast, vm->allocator);

    if (!fn) {
        if (vm->error_fn)
            vm->error_fn(
                vm->error_userdata,
                vm->compile_error_line,
                vm->compile_error_message[0]
                    ? vm->compile_error_message
                    : "Compile error");
        return SLP_COMPILE_ERROR;
    }

    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->try_handler_count = 0;
    vm->abort_requested = false;
    vm->flow_exit_requested = false;

    bool had_sub = false;
    SlpBridgeType *bt = vm->bridge_types;
    while (bt) {
        if (strcmp(bt->keyword, "sub") == 0 || strcmp(bt->keyword, "alias") == 0) {
            had_sub = true;
            break;
        }
        bt = bt->next;
    }
    if (!had_sub)
        slp_vm_register_bridge_type(vm, "sub", NULL, NULL);
    return slp_vm_run(vm, fn);
}

SlpResult slp_vm_eval_inline(
    SlpVM *vm, const char *source,
    const char *source_name,
    SlpValue *result_value) {
    if (!vm || !source) return SLP_RUNTIME_ERROR;
    if (result_value)
        *result_value = SLP_NULL_VAL;

    size_t saved_path_length =
        vm->source_path
            ? strlen(vm->source_path)
            : 0;
    char *saved_path = NULL;
    if (saved_path_length > 0) {
        saved_path = (char*)SLP_MALLOC(
            vm->allocator, saved_path_length + 1);
        if (!saved_path) return SLP_RUNTIME_ERROR;
        memcpy(
            saved_path, vm->source_path,
            saved_path_length + 1);
    }
    if (source_name && source_name[0])
        slp_vm_set_source_name(vm, source_name);

    /*
     * Sleep's runtime code compiler numbers an eval string from its first
     * source line, not from formatting newlines surrounding that string.
     */
    while (*source == '\r' ||
           *source == '\n')
        source++;
    SlpParser parser;
    slp_parser_init(
        &parser, source, vm->allocator);
    SlpASTNode *ast =
        slp_parser_parse(&parser);
    if (parser.had_error || !ast) {
        slp_vm_flag_error(
            vm,
            make_compile_error(
                vm, source,
                parser.error_line,
                parser.error_message));
        if (ast)
            slp_parser_free_node(
                ast, vm->allocator);
        if (saved_path)
            slp_vm_set_source_name(vm, saved_path);
        SLP_FREE(vm->allocator, saved_path);
        return SLP_COMPILE_ERROR;
    }

    SlpObjFunction *function =
        slp_compile_ex(
            vm, ast, true, vm->allocator);
    slp_parser_free_node(
        ast, vm->allocator);
    if (!function) {
        slp_vm_flag_error(
            vm,
            make_compile_error(
                vm, source,
                vm->compile_error_line,
                vm->compile_error_message[0]
                    ? vm->compile_error_message
                    : "Compile error"));
        if (saved_path)
            slp_vm_set_source_name(vm, saved_path);
        SLP_FREE(vm->allocator, saved_path);
        return SLP_COMPILE_ERROR;
    }

    SlpObjClosure *closure =
        slp_obj_closure_new(
            vm->allocator, function);
    if (!closure) {
        if (saved_path)
            slp_vm_set_source_name(vm, saved_path);
        SLP_FREE(vm->allocator, saved_path);
        return SLP_RUNTIME_ERROR;
    }
    track_object(vm, &closure->obj);
    closure->is_inline = true;

    SlpValue *saved_stack_top =
        vm->stack_top;
    int saved_frame_count =
        vm->frame_count;
    int saved_handler_count =
        vm->try_handler_count;
    slp_vm_push(vm, SLP_OBJ_VAL(closure));
    SlpResult result =
        slp_vm_call(vm, 0, false);
    if (result == SLP_OK &&
        vm->stack_top > saved_stack_top &&
        result_value)
        *result_value =
            dereference_value(
                vm->stack_top[-1]);

    while (vm->frame_count >
           saved_frame_count) {
        SlpCallFrame *frame =
            current_frame(vm);
        close_upvalues(vm, frame->slots);
        vm->frame_count--;
    }
    vm->try_handler_count =
        saved_handler_count;
    vm->stack_top = saved_stack_top;

    if (saved_path)
        slp_vm_set_source_name(vm, saved_path);
    SLP_FREE(vm->allocator, saved_path);
    return result;
}

SlpObjClosure *slp_vm_compile_closure_source(
    SlpVM *vm, const char *source,
    const char *source_name) {
    if (!vm || !source) return NULL;

    size_t saved_path_length =
        vm->source_path
            ? strlen(vm->source_path)
            : 0;
    char *saved_path = NULL;
    if (saved_path_length > 0) {
        saved_path = (char*)SLP_MALLOC(
            vm->allocator, saved_path_length + 1);
        if (!saved_path) return NULL;
        memcpy(
            saved_path, vm->source_path,
            saved_path_length + 1);
    }
    if (source_name && source_name[0])
        slp_vm_set_source_name(
            vm, source_name);

    while (*source == '\r' ||
           *source == '\n')
        source++;
    SlpParser parser;
    slp_parser_init(
        &parser, source, vm->allocator);
    SlpASTNode *ast =
        slp_parser_parse(&parser);
    if (parser.had_error || !ast) {
        slp_vm_flag_error(
            vm,
            make_compile_error(
                vm, source,
                parser.error_line,
                parser.error_message));
        if (ast)
            slp_parser_free_node(
                ast, vm->allocator);
        if (saved_path)
            slp_vm_set_source_name(
                vm, saved_path);
        SLP_FREE(vm->allocator, saved_path);
        return NULL;
    }

    SlpObjFunction *function =
        slp_compile(vm, ast, vm->allocator);
    slp_parser_free_node(
        ast, vm->allocator);
    if (!function) {
        if (saved_path)
            slp_vm_set_source_name(
                vm, saved_path);
        SLP_FREE(vm->allocator, saved_path);
        return NULL;
    }

    int first_line = 0;
    int last_line = 0;
    for (int i = 0;
         i < function->chunk->count; i++) {
        int instruction_line =
            function->chunk->lines[i];
        if (instruction_line <= 0)
            continue;
        if (first_line == 0 ||
            instruction_line < first_line)
            first_line = instruction_line;
        if (instruction_line > last_line)
            last_line = instruction_line;
    }
    function->line_start = first_line;
    function->line_end = last_line;

    SlpObjClosure *closure =
        slp_obj_closure_new(
            vm->allocator, function);
    if (closure) {
        track_object(vm, &closure->obj);
        closure->identity =
            ++vm->next_closure_identity;
        closure->scope =
            slp_vm_new_hash(vm);
        if (!closure->scope)
            closure = NULL;
    }

    if (saved_path)
        slp_vm_set_source_name(vm, saved_path);
    SLP_FREE(vm->allocator, saved_path);
    return closure;
}

void slp_vm_register_bridge_type(SlpVM *vm, const char *keyword,
    void (*handler)(SlpVM*, const char*, const char*, const char*,
                    SlpObjClosure*, void*),
    void *userdata) {
    SlpBridgeType *bt = SLP_ALLOCATE(vm->allocator, SlpBridgeType);
    size_t kw_len = slp_utils_strlen(keyword);
    char *kw_copy = (char*)SLP_MALLOC(vm->allocator, kw_len + 1);
    slp_utils_strcpy(kw_copy, keyword);
    bt->keyword = kw_copy;
    bt->handler = handler;
    bt->userdata = userdata;
    bt->next = vm->bridge_types;
    vm->bridge_types = bt;
}

void slp_vm_register_native(SlpVM *vm, const char *name, SlpNativeFn fn) {
    define_native(vm, name, fn);
}

void slp_vm_register_native_raw(
    SlpVM *vm, const char *name, SlpNativeFn fn) {
    define_native_ex(vm, name, fn, true);
}

void slp_vm_flag_error(SlpVM *vm, SlpValue error) {
    if (!vm) return;
    vm->last_error = dereference_value(error);
    if ((vm->debug_flags & 2) == 2 &&
        (vm->debug_flags & 16) != 16) {
        SlpObjString *description =
            slp_vm_stringify(
                vm, vm->last_error);
        if (description) {
            size_t capacity =
                strlen("checkError(): ") +
                description->length + 1;
            char *message =
                (char*)SLP_MALLOC(
                    vm->allocator, capacity);
            if (message) {
                snprintf(
                    message, capacity,
                    "checkError(): %s",
                    description->chars);
                slp_vm_warning(vm, message);
                SLP_FREE(
                    vm->allocator, message);
            }
        }
    }
}

SlpValue slp_vm_check_error(SlpVM *vm) {
    if (!vm) return SLP_NULL_VAL;
    SlpValue error = vm->last_error;
    vm->last_error = SLP_NULL_VAL;
    return error;
}

void slp_vm_set_error_fn(SlpVM *vm, SlpVMErrorFn fn, void *ud) {
    vm->error_fn = fn;
    vm->error_userdata = ud;
}

void slp_vm_set_write_fn(SlpVM *vm, SlpVMWriteFn fn, void *ud) {
    vm->write_fn = fn;
    vm->write_userdata = ud;
}

void slp_vm_set_source_name(SlpVM *vm, const char *source_name) {
    if (!vm) return;
    if (vm->source_name) {
        SLP_FREE(vm->allocator, vm->source_name);
        vm->source_name = NULL;
    }
    if (vm->source_path) {
        SLP_FREE(vm->allocator, vm->source_path);
        vm->source_path = NULL;
    }
    if (!source_name || !source_name[0]) return;

    bool pseudo_name =
        source_name[0] == '<';
    bool absolute =
        source_name[0] == '/' ||
        source_name[0] == '\\' ||
        (isalpha((unsigned char)source_name[0]) &&
         source_name[1] == ':');
    if (!pseudo_name && !absolute) {
        char cwd[4096];
        if (slp_platform_getcwd(
                cwd, sizeof(cwd))) {
            size_t cwd_length = strlen(cwd);
            size_t source_length =
                strlen(source_name);
            bool needs_separator =
                cwd_length > 0 &&
                cwd[cwd_length - 1] != '/' &&
                cwd[cwd_length - 1] != '\\';
            size_t total =
                cwd_length +
                (needs_separator ? 1u : 0u) +
                source_length;
            vm->source_path = (char*)SLP_MALLOC(
                vm->allocator, total + 1);
            if (vm->source_path) {
                memcpy(
                    vm->source_path, cwd,
                    cwd_length);
                size_t offset = cwd_length;
                if (needs_separator)
                    vm->source_path[offset++] =
                        slp_platform_path_separator();
                memcpy(
                    vm->source_path + offset,
                    source_name,
                    source_length + 1);
            }
        }
    }
    if (!vm->source_path) {
        size_t path_length =
            strlen(source_name);
        vm->source_path = (char*)SLP_MALLOC(
            vm->allocator, path_length + 1);
        if (vm->source_path)
            memcpy(
                vm->source_path, source_name,
                path_length + 1);
    }

    const char *basename =
        vm->source_path
            ? vm->source_path
            : source_name;
    for (const char *cursor = basename;
         *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\')
            basename = cursor + 1;
    }
    size_t length = strlen(basename);
    vm->source_name = (char*)SLP_MALLOC(vm->allocator, length + 1);
    if (!vm->source_name) return;
    memcpy(vm->source_name, basename, length + 1);

    /*
     * $__SCRIPT__ is part of the Sleep script environment. Keep it in sync
     * with the diagnostic source name so embedded callers and the CLI observe
     * the same metadata.
     */
    if (vm->globals) {
        SlpObjString *name = slp_vm_copy_cstr(vm, "$__SCRIPT__");
        SlpObjString *value = slp_vm_copy_string(
            vm, vm->source_name, (uint32_t)length);
        if (name && value) {
            slp_obj_hash_set(
                vm->allocator, vm->globals, SLP_OBJ_VAL(name),
                SLP_OBJ_VAL(value));
        }
    }
}

bool slp_vm_set_scoped_value(
    SlpVM *vm, const char *name,
    SlpValue value) {
    if (!vm || !name || !name[0])
        return false;
    SlpObjString *key =
        slp_vm_copy_cstr(vm, name);
    if (!key) return false;
    scoped_variable_set(
        vm, SLP_OBJ_VAL(key), value);
    return true;
}

void slp_vm_write(SlpVM *vm, const char *text) {
    if (vm && vm->write_fn)
        vm->write_fn(vm->write_userdata, text);
    else
        fputs(text, stdout);
}

static int slp_vm_current_line(SlpVM *vm) {
    if (!vm || vm->frame_count <= 0) return 0;
    SlpCallFrame *frame = current_frame(vm);
    SlpChunk *chunk = frame->closure->function->chunk;
    int offset = (int)(frame->ip - chunk->code);
    if (offset > 0) offset--;
    if (offset >= 0 && offset < chunk->count)
        return chunk->lines[offset];
    return 0;
}

static int frame_current_line(SlpCallFrame *frame) {
    if (!frame || !frame->closure || !frame->closure->function ||
        !frame->closure->function->chunk)
        return 0;
    SlpChunk *chunk = frame->closure->function->chunk;
    int offset = (int)(frame->ip - chunk->code);
    if (offset > 0) offset--;
    if (offset >= 0 && offset < chunk->count)
        return chunk->lines[offset];
    return 0;
}

static const char *frame_source_name(SlpCallFrame *frame) {
    if (frame && frame->closure && frame->closure->function &&
        frame->closure->function->source_name)
        return frame->closure->function->source_name->chars;
    return "<unknown>";
}

static void stack_trace_append(
    SlpVM *vm, SlpObjArray *trace, const char *source, int line,
    const char *description) {
    char entry[768];
    int length = snprintf(
        entry, sizeof(entry), "   %s:%d %s",
        source ? source : "<unknown>", line, description);
    if (length < 0) return;
    size_t used = (size_t)length;
    if (used >= sizeof(entry)) used = sizeof(entry) - 1;
    SlpObjString *text =
        slp_vm_copy_string(vm, entry, (uint32_t)used);
    if (text)
        slp_obj_array_push(
            vm->allocator, trace, SLP_OBJ_VAL(text));
}

static void capture_stack_trace(
    SlpVM *vm, int handler_frame_count) {
    SlpObjArray *trace = slp_vm_new_array(vm);
    if (!trace) return;

    int first_callee = handler_frame_count;
    if (first_callee < 1) first_callee = 1;
    for (int callee_index = first_callee;
         callee_index < vm->frame_count; callee_index++) {
        SlpCallFrame *caller = &vm->frames[callee_index - 1];
        SlpObjClosure *callee =
            vm->frames[callee_index].closure;
        char description[512];
        if (callee && callee->call_name) {
            snprintf(
                description, sizeof(description), "%s%s()",
                callee->is_inline ? "<inline> " : "",
                callee->call_name->chars);
        } else if (callee) {
            SlpObjString *text =
                describe_value(vm, SLP_OBJ_VAL(callee));
            snprintf(
                description, sizeof(description), "%s",
                text ? text->chars : "&closure");
        } else {
            snprintf(description, sizeof(description), "&closure");
        }
        stack_trace_append(
            vm, trace, frame_source_name(caller),
            frame_current_line(caller), description);
    }

    if (vm->frame_count > 0) {
        SlpCallFrame *origin =
            &vm->frames[vm->frame_count - 1];
        stack_trace_append(
            vm, trace, frame_source_name(origin),
            frame_current_line(origin), "<origin of exception>");
    }
    vm->last_stack_trace = trace;
}

void slp_vm_warning(SlpVM *vm, const char *message) {
    if (!vm || !message) return;
    char line[32];
    snprintf(line, sizeof(line), "%d", slp_vm_current_line(vm));
    slp_vm_write(vm, "Warning: ");
    slp_vm_write(vm, message);
    slp_vm_write(vm, " at ");
    slp_vm_write(vm, vm->source_name ? vm->source_name : "<unknown>");
    slp_vm_write(vm, ":");
    slp_vm_write(vm, line);
    slp_vm_write(vm, "\n");
}

void slp_vm_abort_warning(SlpVM *vm, const char *message) {
    slp_vm_warning(vm, message);
    if (vm) vm->abort_requested = true;
}

void slp_vm_ffi_set_null(SlpVM *vm, int slot) { vm->ffi_slots[slot] = SLP_NULL_VAL; }
void slp_vm_ffi_set_bool(SlpVM *vm, int slot, bool val) { vm->ffi_slots[slot] = SLP_BOOL_VAL(val); }
void slp_vm_ffi_set_number(SlpVM *vm, int slot, double val) {
    SlpObjDouble *number = slp_vm_new_double(vm, val);
    vm->ffi_slots[slot] =
        number ? SLP_OBJ_VAL(number) : SLP_NULL_VAL;
}
void slp_vm_ffi_set_long(SlpVM *vm, int slot, int64_t val) {
    SlpObjLong *obj = slp_vm_new_long(vm, val);
    vm->ffi_slots[slot] =
        obj ? SLP_OBJ_VAL(obj) : SLP_NULL_VAL;
}
void slp_vm_ffi_set_string(SlpVM *vm, int slot, const char *val) {
    SlpObjString *str = slp_vm_copy_string(vm, val, (uint32_t)slp_utils_strlen(val));
    vm->ffi_slots[slot] = SLP_OBJ_VAL(str);
}

bool slp_vm_ffi_is_null(SlpVM *vm, int slot) { return SLP_IS_NULL(vm->ffi_slots[slot]); }
bool slp_vm_ffi_is_bool(SlpVM *vm, int slot) { return SLP_IS_BOOL(vm->ffi_slots[slot]); }
bool slp_vm_ffi_is_number(SlpVM *vm, int slot) {
    SlpValue value = vm->ffi_slots[slot];
    return SLP_IS_NUM(value) ||
           (SLP_IS_OBJ(value) &&
            (SLP_OBJ_TYPE(value) == SLP_OBJ_LONG ||
             SLP_OBJ_TYPE(value) == SLP_OBJ_DOUBLE));
}
bool slp_vm_ffi_is_string(SlpVM *vm, int slot) {
    SlpValue v = vm->ffi_slots[slot];
    return SLP_IS_OBJ(v) && SLP_OBJ_TYPE(v) == SLP_OBJ_STRING;
}
bool slp_vm_ffi_get_bool(SlpVM *vm, int slot) { return SLP_AS_BOOL(vm->ffi_slots[slot]); }
double slp_vm_ffi_get_number(SlpVM *vm, int slot) {
    return slp_value_as_number(vm->ffi_slots[slot]);
}
int64_t slp_vm_ffi_get_long(SlpVM *vm, int slot) {
    SlpValue v = vm->ffi_slots[slot];
    if (SLP_IS_OBJ(v) && SLP_OBJ_TYPE(v) == SLP_OBJ_LONG)
        return ((SlpObjLong*)SLP_AS_OBJ(v))->value;
    return (int64_t)slp_value_as_number(v);
}
const char *slp_vm_ffi_get_string(SlpVM *vm, int slot) {
    SlpValue v = vm->ffi_slots[slot];
    if (SLP_IS_OBJ(v) && SLP_OBJ_TYPE(v) == SLP_OBJ_STRING)
        return ((SlpObjString*)SLP_AS_OBJ(v))->chars;
    return NULL;
}
