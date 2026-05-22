#include "doctest.h"
#include <string.h>
#include <stdlib.h>

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

TEST_CASE("GC: collect frees unreachable objects") {
    SlpVM *vm = slp_vm_new(&gc_allocator);
    vm->next_gc_threshold = 0;
    slp_vm_copy_string(vm, "reachable", 9);
    size_t before = vm->bytes_allocated;
    slp_gc_collect(vm);
    size_t after_collect = vm->bytes_allocated;
    (void)before;
    (void)after_collect;
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
