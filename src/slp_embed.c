#include "slp_embed.h"
#include "slp_embed_internal.h"
#include "slp_gc.h"

#include <limits.h>
#include <math.h>
#include <stdarg.h>

struct SlpCallable {
    SlpVM *vm;
    SlpValue value;
    SlpCallable *next;
};

static SlpValue embedding_value(SlpValue value) {
    for (;;) {
        if (!SLP_IS_OBJ(value))
            return value;
        if (SLP_OBJ_TYPE(value) ==
            SLP_OBJ_SCALAR_CELL) {
            value =
                SLP_AS_SCALAR_CELL(value)->value;
            continue;
        }
        if (SLP_OBJ_TYPE(value) ==
            SLP_OBJ_TAINTED) {
            value =
                SLP_AS_TAINTED(value)->value;
            continue;
        }
        return value;
    }
}

bool slp_value_is_null(SlpValue value) {
    return SLP_IS_NULL(embedding_value(value));
}

bool slp_value_is_bool(SlpValue value) {
    return SLP_IS_BOOL(embedding_value(value));
}

bool slp_value_is_number(SlpValue value) {
    value = embedding_value(value);
    return SLP_IS_NUM(value) ||
           (SLP_IS_OBJ(value) &&
            (SLP_OBJ_TYPE(value) ==
                 SLP_OBJ_LONG ||
             SLP_OBJ_TYPE(value) ==
                 SLP_OBJ_DOUBLE));
}

bool slp_value_is_string(SlpValue value) {
    value = embedding_value(value);
    return SLP_IS_OBJ(value) &&
           SLP_OBJ_TYPE(value) ==
               SLP_OBJ_STRING;
}

bool slp_value_is_array(SlpValue value) {
    value = embedding_value(value);
    return SLP_IS_OBJ(value) &&
           SLP_OBJ_TYPE(value) ==
               SLP_OBJ_ARRAY;
}

bool slp_value_is_hash(SlpValue value) {
    value = embedding_value(value);
    return SLP_IS_OBJ(value) &&
           SLP_OBJ_TYPE(value) ==
               SLP_OBJ_HASH;
}

bool slp_value_is_callable(SlpValue value) {
    value = embedding_value(value);
    if (!SLP_IS_OBJ(value))
        return false;
    SlpObjType type = SLP_OBJ_TYPE(value);
    return type == SLP_OBJ_CLOSURE ||
           type == SLP_OBJ_FUNCTION ||
           type == SLP_OBJ_NATIVE;
}

bool slp_value_get_bool(
    SlpValue value, bool *out_value) {
    value = embedding_value(value);
    if (!out_value || !SLP_IS_BOOL(value))
        return false;
    *out_value = SLP_AS_BOOL(value);
    return true;
}

bool slp_value_get_number(
    SlpValue value, double *out_value) {
    value = embedding_value(value);
    if (!out_value)
        return false;
    if (SLP_IS_NUM(value)) {
        *out_value = SLP_AS_NUM(value);
        return true;
    }
    if (!SLP_IS_OBJ(value))
        return false;
    if (SLP_OBJ_TYPE(value) ==
        SLP_OBJ_LONG) {
        *out_value =
            (double)SLP_AS_LONG(value)->value;
        return true;
    }
    if (SLP_OBJ_TYPE(value) ==
        SLP_OBJ_DOUBLE) {
        *out_value =
            SLP_AS_DOUBLE(value)->value;
        return true;
    }
    return false;
}

bool slp_value_get_int(
    SlpValue value, int *out_value) {
    double number;
    if (!out_value ||
        !slp_value_get_number(
            value, &number) ||
        !isfinite(number) ||
        number < (double)INT_MIN ||
        number > (double)INT_MAX)
        return false;
    *out_value = (int)number;
    return true;
}

bool slp_value_get_long(
    SlpValue value, int64_t *out_value) {
    value = embedding_value(value);
    if (!out_value)
        return false;
    if (SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) ==
            SLP_OBJ_LONG) {
        *out_value =
            SLP_AS_LONG(value)->value;
        return true;
    }

    double number;
    if (!slp_value_get_number(
            value, &number) ||
        !isfinite(number) ||
        number <
            -9223372036854775808.0 ||
        number >=
            9223372036854775808.0)
        return false;
    *out_value = (int64_t)number;
    return true;
}

bool slp_value_get_string(
    SlpValue value, const char **out_chars,
    uint32_t *out_length) {
    value = embedding_value(value);
    if (!out_chars || !out_length ||
        !SLP_IS_OBJ(value) ||
        SLP_OBJ_TYPE(value) !=
            SLP_OBJ_STRING)
        return false;
    SlpObjString *string =
        SLP_AS_STRING(value);
    *out_chars = string->chars;
    *out_length = string->length;
    return true;
}

bool slp_value_get_array(
    SlpValue value, SlpObjArray **out_array) {
    value = embedding_value(value);
    if (!out_array ||
        !SLP_IS_OBJ(value) ||
        SLP_OBJ_TYPE(value) !=
            SLP_OBJ_ARRAY)
        return false;
    *out_array = SLP_AS_ARRAY(value);
    return true;
}

bool slp_value_get_hash(
    SlpValue value, SlpObjHash **out_hash) {
    value = embedding_value(value);
    if (!out_hash ||
        !SLP_IS_OBJ(value) ||
        SLP_OBJ_TYPE(value) !=
            SLP_OBJ_HASH)
        return false;
    *out_hash = SLP_AS_HASH(value);
    return true;
}

bool slp_value_truthy(SlpValue value) {
    return !slp_value_is_falsy(value);
}

void slp_args_init(
    SlpArgs *reader, SlpVM *vm,
    const SlpValue *values, int count) {
    if (!reader)
        return;
    reader->vm = vm;
    reader->values =
        count > 0 ? values : NULL;
    reader->count =
        values && count > 0 ? count : 0;
    reader->index = 0;
}

int slp_args_remaining(const SlpArgs *reader) {
    if (!reader ||
        reader->index >= reader->count)
        return 0;
    return reader->count - reader->index;
}

static bool slp_args_peek(
    SlpArgs *reader, SlpValue *out_value) {
    if (!reader || !out_value ||
        slp_args_remaining(reader) == 0)
        return false;
    *out_value =
        reader->values[reader->index];
    return true;
}

bool slp_args_next_value(
    SlpArgs *reader, SlpValue *out_value) {
    if (!slp_args_peek(reader, out_value))
        return false;
    reader->index++;
    return true;
}

#define SLP_ARGS_TYPED_NEXT(name, type, getter)                     \
    bool name(SlpArgs *reader, type *out_value) {                   \
        SlpValue value;                                             \
        if (!slp_args_peek(reader, &value) ||                       \
            !getter(value, out_value))                              \
            return false;                                          \
        reader->index++;                                            \
        return true;                                                \
    }

SLP_ARGS_TYPED_NEXT(
    slp_args_next_int, int,
    slp_value_get_int)
SLP_ARGS_TYPED_NEXT(
    slp_args_next_long, int64_t,
    slp_value_get_long)
SLP_ARGS_TYPED_NEXT(
    slp_args_next_number, double,
    slp_value_get_number)
SLP_ARGS_TYPED_NEXT(
    slp_args_next_bool, bool,
    slp_value_get_bool)
SLP_ARGS_TYPED_NEXT(
    slp_args_next_array, SlpObjArray *,
    slp_value_get_array)
SLP_ARGS_TYPED_NEXT(
    slp_args_next_hash, SlpObjHash *,
    slp_value_get_hash)

#undef SLP_ARGS_TYPED_NEXT

bool slp_args_next_string(
    SlpArgs *reader, const char **out_chars,
    uint32_t *out_length) {
    SlpValue value;
    if (!slp_args_peek(reader, &value) ||
        !slp_value_get_string(
            value, out_chars, out_length))
        return false;
    reader->index++;
    return true;
}

bool slp_args_next_truthy(
    SlpArgs *reader, bool *out_value) {
    SlpValue value;
    if (!out_value ||
        !slp_args_peek(reader, &value))
        return false;
    *out_value = slp_value_truthy(value);
    reader->index++;
    return true;
}

SlpResult slp_callable_acquire(
    SlpVM *vm, SlpValue value,
    SlpCallable **out_callable) {
    if (out_callable)
        *out_callable = NULL;
    if (!vm || !out_callable)
        return SLP_RUNTIME_ERROR;

    value = embedding_value(value);
    if (!slp_value_is_callable(value))
        return SLP_RUNTIME_ERROR;
    if (SLP_OBJ_TYPE(value) ==
        SLP_OBJ_FUNCTION) {
        SlpObjClosure *closure =
            slp_vm_new_closure(
                vm, SLP_AS_FUNCTION(value));
        if (!closure)
            return SLP_RUNTIME_ERROR;
        value = SLP_OBJ_VAL(closure);
    }

    SlpCallable *callable =
        SLP_ALLOCATE(
            vm->allocator, SlpCallable);
    if (!callable)
        return SLP_RUNTIME_ERROR;
    callable->vm = vm;
    callable->value = value;
    callable->next = vm->callables;
    vm->callables = callable;
    *out_callable = callable;
    return SLP_OK;
}

void slp_callable_release(
    SlpCallable *callable) {
    if (!callable || !callable->vm)
        return;
    SlpVM *vm = callable->vm;
    SlpCallable **link = &vm->callables;
    while (*link && *link != callable)
        link = &(*link)->next;
    if (*link != callable)
        return;
    *link = callable->next;
    callable->vm = NULL;
    SLP_FREE(vm->allocator, callable);
}

SlpResult slp_callable_call(
    SlpCallable *callable,
    const SlpValue *arguments, int argument_count,
    SlpValue *out_result) {
    if (out_result)
        *out_result = SLP_NULL_VAL;
    if (!callable || !callable->vm)
        return SLP_RUNTIME_ERROR;
    return slp_vm_call_value(
        callable->vm, callable->value,
        arguments, argument_count, out_result);
}

bool slp_args_next_callable(
    SlpArgs *reader, SlpCallable **out_callable) {
    SlpValue value;
    if (!reader || !out_callable ||
        !slp_args_peek(reader, &value) ||
        slp_callable_acquire(
            reader->vm, value,
            out_callable) != SLP_OK)
        return false;
    reader->index++;
    return true;
}

SlpObjArray *slp_array_new(SlpVM *vm) {
    return vm ? slp_vm_new_array(vm) : NULL;
}

void slp_array_push(
    SlpVM *vm, SlpObjArray *array,
    SlpValue value) {
    if (vm && array)
        slp_obj_array_push(
            vm->allocator, array, value);
}

SlpValue slp_array_pop(
    SlpObjArray *array) {
    return array
        ? slp_obj_array_pop(array)
        : SLP_NULL_VAL;
}

SlpValue slp_array_get(
    SlpObjArray *array, int index) {
    return array
        ? slp_obj_array_get(array, index)
        : SLP_NULL_VAL;
}

int slp_array_count(
    const SlpObjArray *array) {
    return array ? array->count : 0;
}

SlpObjHash *slp_hash_new(SlpVM *vm) {
    return vm ? slp_vm_new_hash(vm) : NULL;
}

bool slp_hash_set(
    SlpVM *vm, SlpObjHash *hash,
    SlpValue key, SlpValue value) {
    return vm && hash &&
           slp_vm_hash_set(
               vm, hash, key, value);
}

SlpValue slp_hash_get(
    SlpVM *vm, SlpObjHash *hash,
    SlpValue key) {
    return vm && hash
        ? slp_vm_hash_get(vm, hash, key)
        : SLP_NULL_VAL;
}

bool slp_hash_contains(
    SlpVM *vm, SlpObjHash *hash,
    SlpValue key) {
    return vm && hash &&
           slp_vm_hash_contains(
               vm, hash, key);
}

bool slp_hash_delete(
    SlpVM *vm, SlpObjHash *hash,
    SlpValue key) {
    return vm && hash &&
           slp_vm_hash_delete(
               vm, hash, key);
}

void slp_embed_mark_roots(SlpVM *vm) {
    if (!vm)
        return;
    for (SlpCallable *callable =
             vm->callables;
         callable;
         callable = callable->next)
        slp_gc_mark_value(
            vm, callable->value);
}

void slp_embed_release_all(SlpVM *vm) {
    if (!vm)
        return;
    SlpCallable *callable =
        vm->callables;
    vm->callables = NULL;
    while (callable) {
        SlpCallable *next =
            callable->next;
        callable->vm = NULL;
        SLP_FREE(vm->allocator, callable);
        callable = next;
    }
}

static bool unpack_nullable_string(
    SlpArgs *reader, const char **out_chars,
    int *out_length, bool with_length) {
    SlpValue value;
    if (!reader || !out_chars ||
        !slp_args_peek(reader, &value))
        return false;
    if (slp_value_is_null(value)) {
        *out_chars = NULL;
        if (with_length)
            *out_length = 0;
        reader->index++;
        return true;
    }
    uint32_t length;
    if (!slp_value_get_string(
            value, out_chars, &length) ||
        (with_length &&
         length > (uint32_t)INT_MAX))
        return false;
    if (with_length)
        *out_length = (int)length;
    reader->index++;
    return true;
}

bool slp_args_unpack(
    SlpVM *vm, SlpValue *arguments,
    int argument_count, const char *format, ...) {
    if (!format)
        return true;

    SlpArgs reader;
    slp_args_init(
        &reader, vm, arguments,
        argument_count);
    bool optional = false;
    va_list output;
    va_start(output, format);

    for (const char *cursor = format;
         *cursor; cursor++) {
        if (*cursor == ' ')
            continue;
        if (*cursor == '|') {
            optional = true;
            continue;
        }
        if (slp_args_remaining(
                &reader) == 0) {
            if (optional)
                break;
            va_end(output);
            return false;
        }

        bool ok = false;
        switch (*cursor) {
        case 's': {
            const char **chars =
                va_arg(output, const char **);
            if (cursor[1] == '#') {
                int *length =
                    va_arg(output, int *);
                cursor++;
                ok = length &&
                     unpack_nullable_string(
                         &reader, chars,
                         length, true);
            } else {
                ok = unpack_nullable_string(
                    &reader, chars,
                    NULL, false);
            }
            break;
        }
        case 'i': {
            int *value =
                va_arg(output, int *);
            SlpValue argument;
            double number;
            if (value &&
                slp_args_peek(
                    &reader, &argument)) {
                number =
                    slp_value_as_number(
                        argument);
                if (isfinite(number) &&
                    number >=
                        (double)INT_MIN &&
                    number <=
                        (double)INT_MAX) {
                    *value = (int)number;
                    reader.index++;
                    ok = true;
                }
            }
            break;
        }
        case 'l':
            ok = slp_args_next_long(
                &reader,
                va_arg(output, int64_t *));
            break;
        case 'd':
            ok = slp_args_next_number(
                &reader,
                va_arg(output, double *));
            break;
        case 'b':
            ok = slp_args_next_truthy(
                &reader,
                va_arg(output, bool *));
            break;
        case 'O':
            if (cursor[1] == '!') {
                int expected =
                    va_arg(output, int);
                cursor++;
                if (expected ==
                    SLP_OBJ_ARRAY)
                    ok = slp_args_next_array(
                        &reader,
                        va_arg(
                            output,
                            SlpObjArray **));
                else if (expected ==
                         SLP_OBJ_HASH)
                    ok = slp_args_next_hash(
                        &reader,
                        va_arg(
                            output,
                            SlpObjHash **));
            } else {
                SlpValue *value =
                    va_arg(output, SlpValue *);
                ok = slp_args_next_value(
                    &reader, value);
                if (ok)
                    *value =
                        embedding_value(*value);
            }
            break;
        default:
            ok = false;
            break;
        }

        if (!ok) {
            va_end(output);
            return false;
        }
    }

    va_end(output);
    return true;
}
