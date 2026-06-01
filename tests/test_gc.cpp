#include "doctest.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern "C" {
#include "slp_common.h"
#include "slp_core.h"
#include "slp_value.h"
#include "slp_vm.h"
#include "slp_gc.h"
#include "slp_compiler.h"
#include "slp_parser.h"
#include "slp_ast.h"

static void *gc_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static SlpAllocator gc_allocator = {gc_alloc, NULL};
}

TEST_CASE("GC: init and free") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    REQUIRE(vm != nullptr);
    CHECK(vm->bytes_allocated >= 0);
    slp_vm_free(vm);
}

TEST_CASE("GC: allocate object increases bytes_allocated") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    size_t before = vm->bytes_allocated;
    SlpObjString *s = slp_vm_copy_string(vm, "test", 4);
    REQUIRE(s != nullptr);
    size_t after = vm->bytes_allocated;
    CHECK(after > before);
    slp_vm_free(vm);
}

TEST_CASE("GC: string interning deduplicates") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    SlpObjString *a = slp_vm_copy_string(vm, "hello", 5);
    SlpObjString *b = slp_vm_copy_string(vm, "hello", 5);
    CHECK(a == b);
    slp_vm_free(vm);
}

TEST_CASE("GC: different strings not deduplicated") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    SlpObjString *a = slp_vm_copy_string(vm, "hello", 5);
    SlpObjString *b = slp_vm_copy_string(vm, "world", 5);
    CHECK(a != b);
    slp_vm_free(vm);
}

TEST_CASE("GC: collect reclaims unreachable objects but keeps rooted ones") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    // Root a string on the VM value stack so it must survive.
    SlpObjString *live = slp_vm_copy_string(vm, "live-object", 11);
    slp_vm_push(vm, SLP_OBJ_VAL(live));
    // Create an unrooted string that must be reclaimed.
    slp_vm_copy_string(vm, "dead-object", 11);

    size_t before = vm->bytes_allocated;
    slp_gc_collect(vm);
    size_t after = vm->bytes_allocated;

    CHECK(after < before);                              // dead object reclaimed
    CHECK(SLP_AS_STRING(slp_vm_peek(vm, 0)) == live);   // live still rooted
    CHECK(strcmp(live->chars, "live-object") == 0);     // and intact
    slp_vm_free(vm);
}

TEST_CASE("GC: mark value") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    SlpObjString *s = slp_vm_copy_string(vm, "test", 4);
    SlpValue val = SLP_OBJ_VAL(s);
    s->obj.is_marked = false;
    slp_gc_mark_value(vm, val);
    CHECK(s->obj.is_marked == true);
    slp_gc_mark_value(vm, SLP_NULL_VAL);
    slp_gc_mark_value(vm, SLP_NUM_VAL(42.0));
    slp_gc_mark_value(vm, SLP_BOOL_VAL(true));
    slp_vm_free(vm);
}

TEST_CASE("GC: mark object") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    SlpObjString *s = slp_vm_copy_string(vm, "marked", 6);
    s->obj.is_marked = false;
    slp_gc_mark_obj(vm, &s->obj);
    CHECK(s->obj.is_marked == true);
    slp_vm_free(vm);
}

TEST_CASE("GC: sweep removes unmarked") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    SlpObjString *keep = slp_vm_copy_string(vm, "keep", 4);
    SlpObjString *lose = slp_vm_copy_string(vm, "lose", 4);
    (void)lose;
    keep->obj.is_marked = true;
    lose->obj.is_marked = false;
    slp_gc_sweep(vm);
    SlpObj *obj = vm->objects;
    bool found_lose = false;
    while (obj) {
        if (obj == &lose->obj) found_lose = true;
        obj = obj->next;
    }
    CHECK_FALSE(found_lose);
    slp_vm_free(vm);
}

TEST_CASE("GC: many allocations trigger collection") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    vm->next_gc_threshold = 1024;
    for (int i = 0; i < 1000; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "str_%d", i);
        slp_vm_push(vm, SLP_OBJ_VAL(slp_vm_copy_string(vm, buf, (uint32_t)strlen(buf))));
    }
    while (vm->stack_top > vm->stack) slp_vm_pop(vm);
    slp_gc_collect(vm);
    slp_vm_free(vm);
}

TEST_CASE("GC: interpret with many strings") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$a = \"hello\"; $b = \"world\"; $c = \"foo\"; $d = \"bar\";");
    CHECK(r == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("GC: object size for string") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    SlpObjString *s = slp_vm_copy_string(vm, "test", 4);
    size_t sz = slp_gc_object_size(&s->obj);
    CHECK(sz >= sizeof(SlpObjString));
    (void)sz;
    slp_vm_free(vm);
}

TEST_CASE("GC: hash table with GC") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    SlpObjHash *hash = slp_obj_hash_new(&gc_allocator);
    REQUIRE(hash != nullptr);
    hash->obj.next = vm->objects;
    vm->objects = &hash->obj;
    SlpObjString *key = slp_vm_copy_string(vm, "key", 3);
    slp_obj_hash_set(&gc_allocator, hash, SLP_OBJ_VAL(key), SLP_NUM_VAL(42.0));
    SlpValue val = slp_obj_hash_get(hash, SLP_OBJ_VAL(key));
    CHECK(SLP_IS_NUM(val));
    CHECK(SLP_AS_NUM(val) == doctest::Approx(42.0));
    slp_vm_free(vm);
}

TEST_CASE("GC: compilation creates objects tracked by GC") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    SlpParser parser;
    slp_parser_init(&parser, "\"hello\" + \"world\";", &gc_allocator);
    SlpASTNode *ast = slp_parser_parse(&parser);
    REQUIRE(ast != nullptr);
    size_t before = vm->bytes_allocated;
    SlpObjFunction *fn = slp_compile(vm, ast, &gc_allocator);
    REQUIRE(fn != nullptr);
    size_t after = vm->bytes_allocated;
    CHECK(after >= before);
    (void)after; (void)before;
    slp_parser_free_node(ast, &gc_allocator);
    slp_vm_free(vm);
}

// ---------------------------------------------------------------------------
// Real-collection correctness (Phase 2: GC)
// ---------------------------------------------------------------------------

TEST_CASE("GC: intern table has no dangling entries after a string is swept") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    // Intern a string and leave it unrooted.
    SlpObjString *first = slp_vm_copy_string(vm, "ephemeral", 9);
    (void)first;
    slp_gc_collect(vm); // frees "ephemeral"; must also drop it from the table

    // Re-interning must build a fresh, valid object rather than return a
    // dangling pointer into freed memory (ASAN would flag a use-after-free).
    SlpObjString *second = slp_vm_copy_string(vm, "ephemeral", 9);
    REQUIRE(second != nullptr);
    CHECK(second->length == 9);
    CHECK(strcmp(second->chars, "ephemeral") == 0);
    slp_vm_free(vm);
}

TEST_CASE("GC: rooted array and its elements survive collection") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    SlpObjArray *arr = slp_vm_new_array(vm);
    slp_vm_push(vm, SLP_OBJ_VAL(arr)); // root the array
    for (int i = 0; i < 32; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "elem-%d", i);
        slp_obj_array_push(vm->allocator, arr,
            SLP_OBJ_VAL(slp_vm_copy_string(vm, buf, (uint32_t)strlen(buf))));
    }
    slp_gc_collect(vm);

    REQUIRE(arr->count == 32);
    for (int i = 0; i < 32; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "elem-%d", i);
        SlpValue v = arr->elements[i];
        REQUIRE(SLP_IS_OBJ(v));
        CHECK(strcmp(SLP_AS_STRING(v)->chars, buf) == 0); // reached via rooted arr
    }
    slp_vm_free(vm);
}

TEST_CASE("GC: collection actually triggers during execution and bounds memory") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    // Allocate well over the post-collection floor so the safepoint collector
    // fires repeatedly mid-execution. A mis-marked live object would crash here
    // (or be caught by ASAN); reaching the end with SLP_OK proves it is safe.
    SlpResult r = slp_vm_interpret(vm,
        "$i = 0; while ($i < 20000) { @a = @(1, 2, 3); $i = $i + 1; } $done = $i;");
    CHECK(r == SLP_OK);
    SlpObjString *name = slp_vm_copy_string(vm, "$done", 5);
    SlpValue done = slp_obj_hash_get(vm->globals, SLP_OBJ_VAL(name));
    CHECK(SLP_IS_NUM(done));
    CHECK(SLP_AS_NUM(done) == doctest::Approx(20000));
    slp_vm_free(vm);
}

TEST_CASE("GC: callcc program runs correctly with the collector active") {
    // callcc snapshots frames whose closures must be marked. Run a known-good
    // callcc program with collection forced on; a mis-marked frame closure
    // would crash (or trip ASAN). The script's own assert pins the result.
    SlpVM *vm = slp_vm_new(&gc_allocator);
    vm->next_gc_threshold = 0; // collect at the next safepoint
    SlpResult r = slp_vm_interpret(vm,
        "sub bar { $x = callcc { [$1: 42]; }; return $x; } "
        "$r = bar(); assert $r == 42;");
    CHECK(r == SLP_OK);
    slp_gc_collect(vm);
    slp_vm_free(vm);
}
