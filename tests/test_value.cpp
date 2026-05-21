#include "doctest.h"
#include <string.h>
#include <math.h>

extern "C" {
#include "sleepy_common.h"
#include "sleepy_core.h"
#include "sleepy_value.h"
#include "sleepy_chunk.h"
#include "sleepy_gc.h"
#include "sleepy_vm.h"
#include <stdlib.h>

static void *test_val_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static SleepyAllocator val_allocator = {test_val_alloc, NULL};
}

TEST_CASE("NaN-boxing: null value") {
    SleepyValue v = SLEEPY_NULL_VAL;
    CHECK(SLEEPY_IS_NULL(v));
    CHECK(!SLEEPY_IS_BOOL(v));
    CHECK(!SLEEPY_IS_NUM(v));
    CHECK(!SLEEPY_IS_OBJ(v));
    CHECK(sleepy_value_is_falsy(v));
}

TEST_CASE("NaN-boxing: boolean values") {
    SleepyValue t = SLEEPY_TRUE_VAL;
    SleepyValue f = SLEEPY_FALSE_VAL;
    CHECK(SLEEPY_IS_BOOL(t));
    CHECK(SLEEPY_IS_BOOL(f));
    CHECK(SLEEPY_AS_BOOL(t) == true);
    CHECK(SLEEPY_AS_BOOL(f) == false);
    CHECK(!sleepy_value_is_falsy(t));
    CHECK(sleepy_value_is_falsy(f));
}

TEST_CASE("NaN-boxing: number values") {
    SleepyValue v = SLEEPY_NUM_VAL(3.14);
    CHECK(SLEEPY_IS_NUM(v));
    CHECK(!SLEEPY_IS_NULL(v));
    CHECK(!SLEEPY_IS_BOOL(v));
    CHECK(SLEEPY_AS_NUM(v) == doctest::Approx(3.14));
}

TEST_CASE("NaN-boxing: zero is falsy") {
    SleepyValue z = SLEEPY_NUM_VAL(0.0);
    CHECK(sleepy_value_is_falsy(z));
}

TEST_CASE("NaN-boxing: negative zero is falsy") {
    SleepyValue nz = SLEEPY_NUM_VAL(-0.0);
    CHECK(sleepy_value_is_falsy(nz));
}

TEST_CASE("NaN-boxing: value equality") {
    CHECK(sleepy_value_equals(SLEEPY_NULL_VAL, SLEEPY_NULL_VAL));
    CHECK(sleepy_value_equals(SLEEPY_TRUE_VAL, SLEEPY_TRUE_VAL));
    CHECK(sleepy_value_equals(SLEEPY_FALSE_VAL, SLEEPY_FALSE_VAL));
    CHECK(!sleepy_value_equals(SLEEPY_TRUE_VAL, SLEEPY_FALSE_VAL));
    CHECK(sleepy_value_equals(SLEEPY_NUM_VAL(42.0), SLEEPY_NUM_VAL(42.0)));
    CHECK(!sleepy_value_equals(SLEEPY_NUM_VAL(1.0), SLEEPY_NUM_VAL(2.0)));
    CHECK(!sleepy_value_equals(SLEEPY_NULL_VAL, SLEEPY_TRUE_VAL));
}

TEST_CASE("Object string creation") {
    SleepyObjString *s = sleepy_obj_string_new(&val_allocator, "hello", 5);
    REQUIRE(s != nullptr);
    CHECK(s->obj.type == SLEEPY_OBJ_STRING);
    CHECK(s->length == 5);
    CHECK(strncmp(s->chars, "hello", 5) == 0);
    CHECK(s->hash != 0);
    SLEEPY_FREE(&val_allocator, s);
}

TEST_CASE("Object long creation") {
    SleepyObjLong *l = sleepy_obj_long_new(&val_allocator, 123456789012LL);
    REQUIRE(l != nullptr);
    CHECK(l->obj.type == SLEEPY_OBJ_LONG);
    CHECK(l->value == 123456789012LL);
    SLEEPY_FREE(&val_allocator, l);
}

TEST_CASE("Object array push/pop") {
    SleepyObjArray *arr = sleepy_obj_array_new(&val_allocator);
    REQUIRE(arr != nullptr);
    CHECK(arr->count == 0);
    sleepy_obj_array_push(&val_allocator, arr, SLEEPY_NUM_VAL(1.0));
    sleepy_obj_array_push(&val_allocator, arr, SLEEPY_NUM_VAL(2.0));
    sleepy_obj_array_push(&val_allocator, arr, SLEEPY_NUM_VAL(3.0));
    CHECK(arr->count == 3);
    CHECK(SLEEPY_AS_NUM(arr->elements[0]) == 1.0);
    CHECK(SLEEPY_AS_NUM(arr->elements[1]) == 2.0);
    CHECK(SLEEPY_AS_NUM(arr->elements[2]) == 3.0);
    SleepyValue popped = sleepy_obj_array_pop(arr);
    CHECK(SLEEPY_AS_NUM(popped) == 3.0);
    CHECK(arr->count == 2);
    SLEEPY_FREE(&val_allocator, arr->elements);
    SLEEPY_FREE(&val_allocator, arr);
}

TEST_CASE("Object hash set/get") {
    SleepyObjHash *h = sleepy_obj_hash_new(&val_allocator);
    REQUIRE(h != nullptr);
    SleepyObjString *key = sleepy_obj_string_new(&val_allocator, "key", 3);
    sleepy_obj_hash_set(&val_allocator, h, SLEEPY_OBJ_VAL(key), SLEEPY_NUM_VAL(42.0));
    SleepyValue val = sleepy_obj_hash_get(h, SLEEPY_OBJ_VAL(key));
    CHECK(SLEEPY_AS_NUM(val) == 42.0);
    SleepyValue missing = sleepy_obj_hash_get(h, SLEEPY_NULL_VAL);
    CHECK(SLEEPY_IS_NULL(missing));
    SLEEPY_FREE(&val_allocator, key);
    SLEEPY_FREE(&val_allocator, h->entries);
    SLEEPY_FREE(&val_allocator, h);
}

TEST_CASE("FNV-1a hash function") {
    uint32_t h1 = sleepy_hash_string("hello", 5);
    uint32_t h2 = sleepy_hash_string("hello", 5);
    uint32_t h3 = sleepy_hash_string("world", 5);
    CHECK(h1 == h2);
    CHECK(h1 != h3);
    CHECK(h1 != 0);
}

TEST_CASE("Chunk write and constants") {
    SleepyChunk chunk;
    sleepy_chunk_init(&chunk, &val_allocator);
    int off = sleepy_chunk_write(&chunk, OP_PUSH_TRUE, 1);
    CHECK(off == 0);
    CHECK(chunk.count == 1);
    CHECK(chunk.code[0] == OP_PUSH_TRUE);
    CHECK(chunk.lines[0] == 1);
    int cidx = sleepy_chunk_add_constant(&chunk, SLEEPY_NUM_VAL(3.14));
    CHECK(cidx == 0);
    CHECK(chunk.constant_count == 1);
    int cidx2 = sleepy_chunk_add_constant(&chunk, SLEEPY_NUM_VAL(3.14));
    CHECK(cidx2 == 0);
    CHECK(chunk.constant_count == 1);
    sleepy_chunk_free(&chunk);
}

TEST_CASE("Chunk write short round-trip") {
    SleepyChunk chunk;
    sleepy_chunk_init(&chunk, &val_allocator);
    sleepy_chunk_write(&chunk, OP_PUSH_CONST, 1);
    sleepy_chunk_write_short(&chunk, 0x1234, 1);
    uint16_t val = sleepy_chunk_read_short(&chunk, 1);
    CHECK(val == 0x1234);
    sleepy_chunk_free(&chunk);
}
