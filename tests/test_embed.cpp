#include "doctest.h"

#include <cstring>
#include <cstdlib>
#include <limits>

extern "C" {
#include "slp_common.h"
#include "slp_core.h"
#include "slp_value.h"
#include "slp_vm.h"
#include "slp_embed.h"
#include "slp_gc.h"

static void *embed_alloc(
    void *pointer, size_t size, void *user_data) {
    (void)user_data;
    if (size == 0) {
        free(pointer);
        return NULL;
    }
    return realloc(pointer, size);
}

static SlpAllocator embed_allocator = {
    embed_alloc, NULL
};
}

static SlpValue embed_global(
    SlpVM *vm, const char *name) {
    SlpObjString *key =
        slp_vm_copy_cstr(vm, name);
    return slp_obj_hash_get(
        vm->globals, SLP_OBJ_VAL(key));
}

static bool vm_tracks_object(
    SlpVM *vm, const SlpObj *target) {
    for (SlpObj *object = vm->objects;
         object; object = object->next) {
        if (object == target)
            return true;
    }
    return false;
}

TEST_CASE("Embed: value inspection is strict and wrapper-transparent") {
    SlpVM *vm =
        slp_vm_new(&embed_allocator);
    REQUIRE(vm != nullptr);

    int integer = 0;
    int64_t long_integer = 0;
    double number = 0.0;
    bool boolean = false;
    const char *chars = nullptr;
    uint32_t length = 0;

    CHECK(slp_value_is_number(
        SLP_NUM_VAL(42.75)));
    CHECK(slp_value_get_int(
        SLP_NUM_VAL(42.75), &integer));
    CHECK(integer == 42);
    CHECK(slp_value_get_number(
        SLP_NUM_VAL(42.75), &number));
    CHECK(number == doctest::Approx(42.75));

    SlpObjLong *long_object =
        slp_vm_new_long(
            vm, INT64_C(12345678901234));
    REQUIRE(long_object != nullptr);
    CHECK(slp_value_get_long(
        SLP_OBJ_VAL(long_object),
        &long_integer));
    CHECK(long_integer ==
          INT64_C(12345678901234));

    SlpObjDouble *double_object =
        slp_vm_new_double(vm, 9.5);
    REQUIRE(double_object != nullptr);
    CHECK(slp_value_get_number(
        SLP_OBJ_VAL(double_object),
        &number));
    CHECK(number == doctest::Approx(9.5));

    SlpObjString *string =
        slp_vm_copy_string(
            vm, "a\0b", 3);
    REQUIRE(string != nullptr);
    CHECK(slp_value_get_string(
        SLP_OBJ_VAL(string),
        &chars, &length));
    CHECK(length == 3);
    CHECK(std::memcmp(chars, "a\0b", 3) == 0);
    CHECK_FALSE(slp_value_get_number(
        SLP_OBJ_VAL(string), &number));

    SlpValue tainted =
        slp_vm_taint_value(
            vm, SLP_OBJ_VAL(string));
    CHECK(slp_value_is_string(tainted));
    CHECK(slp_value_get_string(
        tainted, &chars, &length));
    CHECK(length == 3);

    SlpObjScalarCell *cell_object =
        slp_obj_scalar_cell_new(
            vm->allocator,
            SLP_NUM_VAL(73));
    REQUIRE(cell_object != nullptr);
    SlpValue cell =
        SLP_OBJ_VAL(cell_object);
    CHECK(slp_value_get_int(
        cell, &integer));
    CHECK(integer == 73);
    SLP_FREE(
        vm->allocator, cell_object);

    CHECK(slp_value_get_bool(
        SLP_TRUE_VAL, &boolean));
    CHECK(boolean);
    CHECK_FALSE(slp_value_get_bool(
        SLP_NUM_VAL(1), &boolean));
    CHECK(slp_value_truthy(
        SLP_OBJ_VAL(string)));
    CHECK_FALSE(slp_value_truthy(
        SLP_NUM_VAL(0)));

    CHECK_FALSE(slp_value_get_int(
        SLP_NUM_VAL(
            std::numeric_limits<double>::infinity()),
        &integer));
    CHECK_FALSE(slp_value_get_int(
        SLP_NUM_VAL(
            (double)INT_MAX + 1.0),
        &integer));

    slp_vm_free(vm);
}

TEST_CASE("Embed: typed argument reads do not advance on failure") {
    SlpVM *vm =
        slp_vm_new(&embed_allocator);
    REQUIRE(vm != nullptr);

    SlpObjString *text =
        slp_vm_copy_cstr(vm, "hello");
    SlpObjLong *long_object =
        slp_vm_new_long(vm, 9001);
    SlpObjArray *array =
        slp_array_new(vm);
    SlpObjHash *hash =
        slp_hash_new(vm);
    REQUIRE(text != nullptr);
    REQUIRE(long_object != nullptr);
    REQUIRE(array != nullptr);
    REQUIRE(hash != nullptr);

    SlpValue values[] = {
        SLP_NUM_VAL(7.9),
        SLP_OBJ_VAL(text),
        SLP_OBJ_VAL(long_object),
        SLP_TRUE_VAL,
        SLP_OBJ_VAL(array),
        SLP_OBJ_VAL(hash)
    };
    SlpArgs reader;
    slp_args_init(
        &reader, vm, values,
        (int)(sizeof(values) /
              sizeof(values[0])));

    const char *chars = nullptr;
    uint32_t length = 0;
    CHECK_FALSE(slp_args_next_string(
        &reader, &chars, &length));
    CHECK(slp_args_remaining(&reader) == 6);

    int integer = 0;
    CHECK(slp_args_next_int(
        &reader, &integer));
    CHECK(integer == 7);
    CHECK(slp_args_remaining(&reader) == 5);

    CHECK(slp_args_next_string(
        &reader, &chars, &length));
    CHECK(length == 5);
    CHECK(std::memcmp(
        chars, "hello", 5) == 0);

    int64_t long_integer = 0;
    CHECK(slp_args_next_long(
        &reader, &long_integer));
    CHECK(long_integer == 9001);

    bool boolean = false;
    CHECK(slp_args_next_bool(
        &reader, &boolean));
    CHECK(boolean);

    SlpObjArray *read_array = nullptr;
    SlpObjHash *read_hash = nullptr;
    CHECK(slp_args_next_array(
        &reader, &read_array));
    CHECK(read_array == array);
    CHECK(slp_args_next_hash(
        &reader, &read_hash));
    CHECK(read_hash == hash);
    CHECK(slp_args_remaining(&reader) == 0);
    SlpValue ignored = SLP_NULL_VAL;
    CHECK_FALSE(slp_args_next_value(
        &reader, &ignored));

    SlpValue truth_values[] = {
        SLP_NUM_VAL(2), SLP_NULL_VAL
    };
    slp_args_init(
        &reader, vm, truth_values, 2);
    CHECK_FALSE(slp_args_next_bool(
        &reader, &boolean));
    CHECK(slp_args_remaining(&reader) == 2);
    CHECK(slp_args_next_truthy(
        &reader, &boolean));
    CHECK(boolean);
    CHECK(slp_args_next_truthy(
        &reader, &boolean));
    CHECK_FALSE(boolean);

    slp_args_init(
        &reader, vm, values, -1);
    CHECK(slp_args_remaining(&reader) == 0);
    slp_vm_free(vm);
}

TEST_CASE("Embed: compatibility unpacker preserves bridge behavior") {
    SlpVM *vm =
        slp_vm_new(&embed_allocator);
    REQUIRE(vm != nullptr);

    SlpObjArray *array =
        slp_array_new(vm);
    SlpObjHash *hash =
        slp_hash_new(vm);
    REQUIRE(array != nullptr);
    REQUIRE(hash != nullptr);
    SlpValue values[] = {
        SLP_OBJ_VAL(
            slp_vm_copy_cstr(vm, "hello")),
        SLP_OBJ_VAL(
            slp_vm_copy_cstr(vm, "bytes")),
        SLP_OBJ_VAL(
            slp_vm_copy_cstr(vm, "42tail")),
        SLP_OBJ_VAL(
            slp_vm_new_long(
                vm,
                INT64_C(12345678901234))),
        SLP_OBJ_VAL(
            slp_vm_new_double(vm, 3.25)),
        SLP_NUM_VAL(1),
        SLP_OBJ_VAL(array),
        SLP_OBJ_VAL(hash)
    };

    const char *text = nullptr;
    const char *bytes = nullptr;
    int byte_count = 0;
    int integer = 0;
    int64_t long_integer = 0;
    double number = 0.0;
    bool boolean = false;
    SlpObjArray *read_array = nullptr;
    SlpObjHash *read_hash = nullptr;
    CHECK(slp_args_unpack(
        vm, values, 8,
        "s s# i l d b O! O!",
        &text, &bytes, &byte_count,
        &integer, &long_integer,
        &number, &boolean,
        SLP_OBJ_ARRAY, &read_array,
        SLP_OBJ_HASH, &read_hash));
    CHECK(std::strcmp(text, "hello") == 0);
    CHECK(byte_count == 5);
    CHECK(std::memcmp(
        bytes, "bytes", 5) == 0);
    CHECK(integer == 42);
    CHECK(long_integer ==
          INT64_C(12345678901234));
    CHECK(number == doctest::Approx(3.25));
    CHECK(boolean);
    CHECK(read_array == array);
    CHECK(read_hash == hash);

    const char *optional = "unchanged";
    CHECK(slp_args_unpack(
        vm, values, 1,
        "s | s", &text, &optional));
    CHECK(std::strcmp(
        optional, "unchanged") == 0);
    CHECK_FALSE(slp_args_unpack(
        vm, values, 1,
        "s s", &text, &optional));
    CHECK_FALSE(slp_args_unpack(
        vm, values, 1,
        "x", &text));

    SlpValue null_value[] = {
        SLP_NULL_VAL
    };
    int null_length = -1;
    text = "not-null";
    CHECK(slp_args_unpack(
        vm, null_value, 1,
        "s#", &text, &null_length));
    CHECK(text == nullptr);
    CHECK(null_length == 0);

    SlpValue tainted =
        slp_vm_taint_value(
            vm, values[0]);
    text = nullptr;
    CHECK(slp_args_unpack(
        vm, &tainted, 1,
        "s", &text));
    CHECK(std::strcmp(text, "hello") == 0);

    slp_vm_free(vm);
}

TEST_CASE("Embed: collection helpers use VM ownership and canonical hashes") {
    SlpVM *vm =
        slp_vm_new(&embed_allocator);
    REQUIRE(vm != nullptr);

    SlpObjArray *array =
        slp_array_new(vm);
    SlpObjHash *hash =
        slp_hash_new(vm);
    REQUIRE(array != nullptr);
    REQUIRE(hash != nullptr);
    slp_vm_push(vm, SLP_OBJ_VAL(array));
    slp_vm_push(vm, SLP_OBJ_VAL(hash));

    slp_array_push(
        vm, array, SLP_NUM_VAL(10));
    slp_array_push(
        vm, array, SLP_NUM_VAL(20));
    CHECK(slp_array_count(array) == 2);
    CHECK(slp_value_as_number(
        slp_array_get(array, 1)) == 20);
    CHECK(slp_value_as_number(
        slp_array_pop(array)) == 20);
    CHECK(slp_array_count(array) == 1);
    CHECK(slp_value_is_null(
        slp_array_get(array, 99)));

    SlpValue key = SLP_NUM_VAL(7);
    CHECK(slp_hash_set(
        vm, hash, key,
        SLP_OBJ_VAL(
            slp_vm_copy_cstr(vm, "value"))));
    CHECK(slp_hash_contains(
        vm, hash,
        SLP_OBJ_VAL(
            slp_vm_copy_cstr(vm, "7"))));
    const char *chars = nullptr;
    uint32_t length = 0;
    CHECK(slp_value_get_string(
        slp_hash_get(
            vm, hash,
            SLP_OBJ_VAL(
                slp_vm_copy_cstr(vm, "7"))),
        &chars, &length));
    CHECK(length == 5);
    CHECK(std::memcmp(
        chars, "value", 5) == 0);

    slp_gc_collect(vm);
    CHECK(slp_array_count(array) == 1);
    CHECK(slp_hash_delete(vm, hash, key));
    CHECK_FALSE(slp_hash_contains(
        vm, hash, key));

    slp_vm_free(vm);
}
