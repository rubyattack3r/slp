#include "doctest.h"
#include <string.h>
#include <math.h>

extern "C" {
#include "slp_common.h"
#include "slp_core.h"
#include "slp_value.h"
#include "slp_chunk.h"
#include "slp_gc.h"
#include "slp_vm.h"
#include <stdlib.h>

static void *test_val_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static SlpAllocator val_allocator = {test_val_alloc, NULL};
}

TEST_CASE("NaN-boxing: null value") {
    SlpValue v = SLP_NULL_VAL;
    CHECK(SLP_IS_NULL(v));
    CHECK(!SLP_IS_BOOL(v));
    CHECK(!SLP_IS_NUM(v));
    CHECK(!SLP_IS_OBJ(v));
    CHECK(slp_value_is_falsy(v));
}

TEST_CASE("NaN-boxing: boolean values") {
    SlpValue t = SLP_TRUE_VAL;
    SlpValue f = SLP_FALSE_VAL;
    CHECK(SLP_IS_BOOL(t));
    CHECK(SLP_IS_BOOL(f));
    CHECK(SLP_AS_BOOL(t) == true);
    CHECK(SLP_AS_BOOL(f) == false);
    CHECK(!slp_value_is_falsy(t));
    CHECK(slp_value_is_falsy(f));
}

TEST_CASE("NaN-boxing: number values") {
    SlpValue v = SLP_NUM_VAL(3.14);
    CHECK(SLP_IS_NUM(v));
    CHECK(!SLP_IS_NULL(v));
    CHECK(!SLP_IS_BOOL(v));
    CHECK(SLP_AS_NUM(v) == doctest::Approx(3.14));
}

TEST_CASE("NaN-boxing: zero is falsy") {
    SlpValue z = SLP_NUM_VAL(0.0);
    CHECK(slp_value_is_falsy(z));
}

TEST_CASE("NaN-boxing: negative zero is falsy") {
    SlpValue nz = SLP_NUM_VAL(-0.0);
    CHECK(slp_value_is_falsy(nz));
}

TEST_CASE("NaN-boxing: value equality") {
    CHECK(slp_value_equals(SLP_NULL_VAL, SLP_NULL_VAL));
    CHECK(slp_value_equals(SLP_TRUE_VAL, SLP_TRUE_VAL));
    CHECK(slp_value_equals(SLP_FALSE_VAL, SLP_FALSE_VAL));
    CHECK(!slp_value_equals(SLP_TRUE_VAL, SLP_FALSE_VAL));
    CHECK(slp_value_equals(SLP_NUM_VAL(42.0), SLP_NUM_VAL(42.0)));
    CHECK(!slp_value_equals(SLP_NUM_VAL(1.0), SLP_NUM_VAL(2.0)));
    CHECK(!slp_value_equals(SLP_NULL_VAL, SLP_TRUE_VAL));
}

TEST_CASE("Object string creation") {
    SlpObjString *s = slp_obj_string_new(&val_allocator, "hello", 5);
    REQUIRE(s != nullptr);
    CHECK(s->obj.type == SLP_OBJ_STRING);
    CHECK(s->length == 5);
    CHECK(strncmp(s->chars, "hello", 5) == 0);
    CHECK(s->hash != 0);
    SLP_FREE(&val_allocator, s);
}

TEST_CASE("Object long creation") {
    SlpObjLong *l = slp_obj_long_new(&val_allocator, 123456789012LL);
    REQUIRE(l != nullptr);
    CHECK(l->obj.type == SLP_OBJ_LONG);
    CHECK(l->value == 123456789012LL);
    SLP_FREE(&val_allocator, l);
}

TEST_CASE("Object array push/pop") {
    SlpObjArray *arr = slp_obj_array_new(&val_allocator);
    REQUIRE(arr != nullptr);
    CHECK(arr->count == 0);
    slp_obj_array_push(&val_allocator, arr, SLP_NUM_VAL(1.0));
    slp_obj_array_push(&val_allocator, arr, SLP_NUM_VAL(2.0));
    slp_obj_array_push(&val_allocator, arr, SLP_NUM_VAL(3.0));
    CHECK(arr->count == 3);
    CHECK(SLP_AS_NUM(arr->elements[0]) == 1.0);
    CHECK(SLP_AS_NUM(arr->elements[1]) == 2.0);
    CHECK(SLP_AS_NUM(arr->elements[2]) == 3.0);
    SlpValue popped = slp_obj_array_pop(arr);
    CHECK(SLP_AS_NUM(popped) == 3.0);
    CHECK(arr->count == 2);
    SLP_FREE(&val_allocator, arr->elements);
    SLP_FREE(&val_allocator, arr);
}

TEST_CASE("Object hash set/get") {
    SlpObjHash *h = slp_obj_hash_new(&val_allocator);
    REQUIRE(h != nullptr);
    SlpObjString *key = slp_obj_string_new(&val_allocator, "key", 3);
    slp_obj_hash_set(&val_allocator, h, SLP_OBJ_VAL(key), SLP_NUM_VAL(42.0));
    SlpValue val = slp_obj_hash_get(h, SLP_OBJ_VAL(key));
    CHECK(SLP_AS_NUM(val) == 42.0);
    SlpValue missing = slp_obj_hash_get(h, SLP_NULL_VAL);
    CHECK(SLP_IS_NULL(missing));
    SLP_FREE(&val_allocator, key);
    SLP_FREE(&val_allocator, h->entries);
    SLP_FREE(&val_allocator, h);
}

TEST_CASE("FNV-1a hash function") {
    uint32_t h1 = slp_hash_string("hello", 5);
    uint32_t h2 = slp_hash_string("hello", 5);
    uint32_t h3 = slp_hash_string("world", 5);
    CHECK(h1 == h2);
    CHECK(h1 != h3);
    CHECK(h1 != 0);
}

TEST_CASE("Chunk write and constants") {
    SlpChunk chunk;
    slp_chunk_init(&chunk, &val_allocator);
    int off = slp_chunk_write(&chunk, OP_PUSH_TRUE, 1);
    CHECK(off == 0);
    CHECK(chunk.count == 1);
    CHECK(chunk.code[0] == OP_PUSH_TRUE);
    CHECK(chunk.lines[0] == 1);
    int cidx = slp_chunk_add_constant(&chunk, SLP_NUM_VAL(3.14));
    CHECK(cidx == 0);
    CHECK(chunk.constant_count == 1);
    int cidx2 = slp_chunk_add_constant(&chunk, SLP_NUM_VAL(3.14));
    CHECK(cidx2 == 0);
    CHECK(chunk.constant_count == 1);
    slp_chunk_free(&chunk);
}

TEST_CASE("Chunk write short round-trip") {
    SlpChunk chunk;
    slp_chunk_init(&chunk, &val_allocator);
    slp_chunk_write(&chunk, OP_PUSH_CONST, 1);
    slp_chunk_write_short(&chunk, 0x1234, 1);
    uint16_t val = slp_chunk_read_short(&chunk, 1);
    CHECK(val == 0x1234);
    slp_chunk_free(&chunk);
}

// ---------------------------------------------------------------------------
// Floating-point edge cases (Phase 3: value coverage)
// ---------------------------------------------------------------------------

TEST_CASE("value: NaN is never equal to itself and is not falsy") {
    SlpValue nan = SLP_NUM_VAL((double)NAN);
    CHECK(SLP_IS_NUM(nan));
    CHECK_FALSE(slp_value_equals(nan, nan)); // IEEE: NaN != NaN
    CHECK_FALSE(slp_value_is_falsy(nan));    // NaN != 0, so truthy
}

TEST_CASE("value: infinities compare and are truthy") {
    SlpValue inf = SLP_NUM_VAL((double)INFINITY);
    SlpValue ninf = SLP_NUM_VAL(-(double)INFINITY);
    CHECK(slp_value_equals(inf, inf));
    CHECK_FALSE(slp_value_equals(inf, ninf));
    CHECK_FALSE(slp_value_is_falsy(inf));
    CHECK_FALSE(slp_value_is_falsy(ninf));
}

TEST_CASE("value: negative zero equals zero and is falsy") {
    SlpValue negzero = SLP_NUM_VAL(-0.0);
    SlpValue zero = SLP_NUM_VAL(0.0);
    CHECK(slp_value_equals(negzero, zero)); // -0.0 == 0.0
    CHECK(slp_value_is_falsy(negzero));
    CHECK(slp_value_is_falsy(zero));
}
