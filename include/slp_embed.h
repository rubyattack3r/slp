#ifndef SLP_EMBED_H
#define SLP_EMBED_H

#include "slp_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A borrowed view over the arguments passed to a native function.
 *
 * Typed reads advance only when they succeed. Values returned by
 * slp_args_next_value() remain owned by the VM; callers that need to retain a
 * callable after the native function returns must acquire an SlpCallable.
 */
typedef struct {
    SlpVM *vm;
    const SlpValue *values;
    int count;
    int index;
} SlpArgs;

void slp_args_init(
    SlpArgs *reader, SlpVM *vm,
    const SlpValue *values, int count);
int slp_args_remaining(const SlpArgs *reader);
bool slp_args_next_value(
    SlpArgs *reader, SlpValue *out_value);
bool slp_args_next_string(
    SlpArgs *reader, const char **out_chars,
    uint32_t *out_length);
bool slp_args_next_int(
    SlpArgs *reader, int *out_value);
bool slp_args_next_long(
    SlpArgs *reader, int64_t *out_value);
bool slp_args_next_number(
    SlpArgs *reader, double *out_value);
bool slp_args_next_bool(
    SlpArgs *reader, bool *out_value);
bool slp_args_next_truthy(
    SlpArgs *reader, bool *out_value);
bool slp_args_next_array(
    SlpArgs *reader, SlpObjArray **out_array);
bool slp_args_next_hash(
    SlpArgs *reader, SlpObjHash **out_hash);
bool slp_args_next_callable(
    SlpArgs *reader, SlpCallable **out_callable);

/*
 * Taint and scalar-cell wrappers are transparent to these predicates and
 * conversions. Numeric getters accept immediate numbers, Long values, and
 * Double values. Integer getters truncate toward zero and reject non-finite
 * or out-of-range values.
 */
bool slp_value_is_null(SlpValue value);
bool slp_value_is_bool(SlpValue value);
bool slp_value_is_number(SlpValue value);
bool slp_value_is_string(SlpValue value);
bool slp_value_is_array(SlpValue value);
bool slp_value_is_hash(SlpValue value);
bool slp_value_is_callable(SlpValue value);
bool slp_value_get_bool(
    SlpValue value, bool *out_value);
bool slp_value_get_number(
    SlpValue value, double *out_value);
bool slp_value_get_int(
    SlpValue value, int *out_value);
bool slp_value_get_long(
    SlpValue value, int64_t *out_value);
bool slp_value_get_string(
    SlpValue value, const char **out_chars,
    uint32_t *out_length);
bool slp_value_get_array(
    SlpValue value, SlpObjArray **out_array);
bool slp_value_get_hash(
    SlpValue value, SlpObjHash **out_hash);
bool slp_value_truthy(SlpValue value);

/*
 * A callable handle roots its closure in the VM until it is released. Handles
 * are invalid after their VM is freed. slp_callable_call() distinguishes an
 * interpreter failure from a successful Sleep null return through SlpResult.
 */
SlpResult slp_callable_acquire(
    SlpVM *vm, SlpValue value,
    SlpCallable **out_callable);
void slp_callable_release(SlpCallable *callable);
SlpResult slp_callable_call(
    SlpCallable *callable,
    const SlpValue *arguments, int argument_count,
    SlpValue *out_result);

/* VM-owned collection helpers for embedding code. */
SlpObjArray *slp_array_new(SlpVM *vm);
void slp_array_push(
    SlpVM *vm, SlpObjArray *array,
    SlpValue value);
SlpValue slp_array_pop(SlpObjArray *array);
SlpValue slp_array_get(
    SlpObjArray *array, int index);
int slp_array_count(const SlpObjArray *array);

SlpObjHash *slp_hash_new(SlpVM *vm);
bool slp_hash_set(
    SlpVM *vm, SlpObjHash *hash,
    SlpValue key, SlpValue value);
SlpValue slp_hash_get(
    SlpVM *vm, SlpObjHash *hash,
    SlpValue key);
bool slp_hash_contains(
    SlpVM *vm, SlpObjHash *hash,
    SlpValue key);
bool slp_hash_delete(
    SlpVM *vm, SlpObjHash *hash,
    SlpValue key);

/*
 * Compatibility convenience for small native functions.
 *
 *   s   const char ** (a Sleep null produces NULL)
 *   s#  const char **, int *
 *   i   int * (Sleep numeric coercion)
 *   l   int64_t * (numeric values only)
 *   d   double * (numeric values only)
 *   b   bool * (Sleep truthiness)
 *   O   SlpValue *
 *   O!  SlpObjType, typed destination (array and hash)
 *   |   all following values are optional
 *
 * Prefer SlpArgs for new code.
 */
bool slp_args_unpack(
    SlpVM *vm, SlpValue *arguments,
    int argument_count, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* SLP_EMBED_H */
