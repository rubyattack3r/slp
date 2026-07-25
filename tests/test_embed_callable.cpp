#include "doctest.h"

#include <cstdlib>
#include <cstring>

extern "C" {
#include "slp_common.h"
#include "slp_core.h"
#include "slp_value.h"
#include "slp_vm.h"
#include "slp_embed.h"
#include "slp_gc.h"

static void *callable_alloc(
    void *pointer, size_t size, void *user_data) {
    (void)user_data;
    if (size == 0) {
        free(pointer);
        return NULL;
    }
    return realloc(pointer, size);
}

static SlpAllocator callable_allocator = {
    callable_alloc, NULL
};
}

static SlpValue callable_global(
    SlpVM *vm, const char *name) {
    SlpObjString *key =
        slp_vm_copy_cstr(vm, name);
    return slp_obj_hash_get(
        vm->globals, SLP_OBJ_VAL(key));
}

static bool callable_vm_tracks(
    SlpVM *vm, const SlpObj *target) {
    for (SlpObj *object = vm->objects;
         object; object = object->next) {
        if (object == target)
            return true;
    }
    return false;
}

TEST_CASE("Embed: callable handles survive GC and release their root") {
    SlpVM *vm =
        slp_vm_new(&callable_allocator);
    REQUIRE(vm != nullptr);
    REQUIRE(slp_vm_interpret(
        vm,
        "$saved = { return $1 . ':' . $2; };") ==
        SLP_OK);

    SlpValue closure_value =
        callable_global(vm, "$saved");
    REQUIRE(slp_value_is_callable(
        closure_value));
    SlpObj *closure_object =
        SLP_AS_OBJ(closure_value);
    SlpCallable *callable = nullptr;
    REQUIRE(slp_callable_acquire(
        vm, closure_value,
        &callable) == SLP_OK);
    REQUIRE(callable != nullptr);

    REQUIRE(slp_vm_interpret(
        vm, "$saved = $null;") == SLP_OK);
    slp_gc_collect(vm);
    CHECK(callable_vm_tracks(
        vm, closure_object));

    SlpValue arguments[] = {
        SLP_OBJ_VAL(
            slp_vm_copy_cstr(vm, "left")),
        SLP_NUM_VAL(9)
    };
    vm->next_gc_threshold = 0;
    SlpValue result = SLP_NULL_VAL;
    CHECK(slp_callable_call(
        callable, arguments, 2,
        &result) == SLP_OK);
    const char *chars = nullptr;
    uint32_t length = 0;
    REQUIRE(slp_value_get_string(
        result, &chars, &length));
    CHECK(length == 6);
    CHECK(std::memcmp(
        chars, "left:9", 6) == 0);

    slp_callable_release(callable);
    slp_gc_collect(vm);
    CHECK_FALSE(callable_vm_tracks(
        vm, closure_object));
    slp_vm_free(vm);
}

TEST_CASE("Embed: null callback results differ from runtime failures") {
    SlpVM *vm =
        slp_vm_new(&callable_allocator);
    REQUIRE(vm != nullptr);
    REQUIRE(slp_vm_interpret(
        vm,
        "$empty = { $temporary = 1; }; "
        "$bad = { return 1 / 0; }; "
        "$good = { return 7; };") == SLP_OK);
    SlpValue *stack_baseline =
        vm->stack_top;

    SlpCallable *nuller = nullptr;
    SlpCallable *bad = nullptr;
    SlpCallable *good = nullptr;
    REQUIRE(slp_callable_acquire(
        vm, callable_global(vm, "$empty"),
        &nuller) == SLP_OK);
    REQUIRE(slp_callable_acquire(
        vm, callable_global(vm, "$bad"),
        &bad) == SLP_OK);
    REQUIRE(slp_callable_acquire(
        vm, callable_global(vm, "$good"),
        &good) == SLP_OK);

    SlpValue result = SLP_NUM_VAL(1);
    CHECK(slp_callable_call(
        nuller, nullptr, 0,
        &result) == SLP_OK);
    CHECK(slp_value_is_null(result));

    result = SLP_NUM_VAL(1);
    CHECK(slp_callable_call(
        bad, nullptr, 0,
        &result) == SLP_RUNTIME_ERROR);
    CHECK(slp_value_is_null(result));

    CHECK(slp_callable_call(
        good, nullptr, 0,
        &result) == SLP_OK);
    int integer = 0;
    CHECK(slp_value_get_int(
        result, &integer));
    CHECK(integer == 7);
    CHECK(vm->frame_count == 0);
    CHECK(vm->stack_top ==
          stack_baseline);

    slp_callable_release(good);
    slp_callable_release(bad);
    slp_callable_release(nuller);
    slp_vm_free(vm);
}

TEST_CASE("Embed: raw functions become tracked callable closures") {
    SlpVM *vm =
        slp_vm_new(&callable_allocator);
    REQUIRE(vm != nullptr);
    REQUIRE(slp_vm_interpret(
        vm,
        "sub forty_two { return 42; } "
        "$closure = &forty_two;") == SLP_OK);
    SlpValue closure_value =
        callable_global(vm, "$closure");
    REQUIRE(SLP_IS_OBJ(closure_value));
    REQUIRE(SLP_OBJ_TYPE(closure_value) ==
            SLP_OBJ_CLOSURE);

    SlpObjFunction *function =
        SLP_AS_CLOSURE(
            closure_value)->function;
    SlpCallable *callable = nullptr;
    REQUIRE(slp_callable_acquire(
        vm, SLP_OBJ_VAL(function),
        &callable) == SLP_OK);
    REQUIRE(callable != nullptr);

    SlpValue result = SLP_NULL_VAL;
    CHECK(slp_callable_call(
        callable, nullptr, 0,
        &result) == SLP_OK);
    int integer = 0;
    CHECK(slp_value_get_int(
        result, &integer));
    CHECK(integer == 42);
    slp_callable_release(callable);
    slp_vm_free(vm);
}

extern "C" {
static SlpValue call_from_native(
    SlpVM *vm, SlpValue *arguments,
    int argument_count) {
    SlpArgs reader;
    slp_args_init(
        &reader, vm, arguments,
        argument_count);
    SlpCallable *callable = NULL;
    int number = 0;
    if (!slp_args_next_callable(
            &reader, &callable) ||
        !slp_args_next_int(
            &reader, &number))
        return SLP_NULL_VAL;

    SlpValue callback_argument =
        SLP_NUM_VAL((double)number);
    SlpValue result =
        SLP_NULL_VAL;
    SlpResult status =
        slp_callable_call(
            callable,
            &callback_argument, 1,
            &result);
    slp_callable_release(callable);
    return status == SLP_OK
        ? result : SLP_NULL_VAL;
}
}

TEST_CASE("Embed: callable invocation is reentrant from native code") {
    SlpVM *vm =
        slp_vm_new(&callable_allocator);
    REQUIRE(vm != nullptr);
    slp_vm_register_native(
        vm, "host_call",
        call_from_native);
    REQUIRE(slp_vm_interpret(
        vm,
        "sub double_it { return $1 * 2; } "
        "$result = host_call(&double_it, 21);") ==
        SLP_OK);

    int integer = 0;
    CHECK(slp_value_get_int(
        callable_global(vm, "$result"),
        &integer));
    CHECK(integer == 42);
    CHECK(vm->frame_count == 0);
    CHECK(vm->stack_top ==
          vm->stack + 1);
    slp_vm_free(vm);
}

TEST_CASE("Embed: invalid callable and argument requests fail cleanly") {
    SlpVM *vm =
        slp_vm_new(&callable_allocator);
    REQUIRE(vm != nullptr);

    SlpCallable *callable =
        reinterpret_cast<SlpCallable*>(
            static_cast<uintptr_t>(1));
    CHECK(slp_callable_acquire(
        vm, SLP_NUM_VAL(1),
        &callable) == SLP_RUNTIME_ERROR);
    CHECK(callable == nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "sub callback { return 1; } "
        "$callback = &callback;") == SLP_OK);
    REQUIRE(slp_callable_acquire(
        vm,
        callable_global(vm, "$callback"),
        &callable) == SLP_OK);
    SlpValue result = SLP_TRUE_VAL;
    CHECK(slp_callable_call(
        callable, nullptr, 1,
        &result) == SLP_RUNTIME_ERROR);
    CHECK(slp_value_is_null(result));
    CHECK(slp_callable_call(
        callable, nullptr, 256,
        &result) == SLP_RUNTIME_ERROR);
    CHECK(slp_value_is_null(result));

    /* VM teardown owns any handles that an embedding host did not release. */
    slp_vm_free(vm);
}
