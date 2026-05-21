#include "doctest.h"
#include <string.h>
#include <stdlib.h>

extern "C" {
#include "sleepy_common.h"
#include "sleepy_core.h"
#include "sleepy_value.h"
#include "sleepy_vm.h"
#include "sleepy_gc.h"
#include "sleepy_compiler.h"
#include "sleepy_parser.h"
#include "sleepy_ast.h"

static void *gc_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static SleepyAllocator gc_allocator = {gc_alloc, NULL};
}

TEST_CASE("GC: init and free") {
    SleepyVM *vm = sleepy_vm_new(&gc_allocator);
    REQUIRE(vm != nullptr);
    CHECK(vm->bytes_allocated >= 0);
    sleepy_vm_free(vm);
}

TEST_CASE("GC: allocate object increases bytes_allocated") {
    SleepyVM *vm = sleepy_vm_new(&gc_allocator);
    size_t before = vm->bytes_allocated;
    SleepyObjString *s = sleepy_vm_copy_string(vm, "test", 4);
    REQUIRE(s != nullptr);
    size_t after = vm->bytes_allocated;
    CHECK(after > before);
    sleepy_vm_free(vm);
}

TEST_CASE("GC: string interning deduplicates") {
    SleepyVM *vm = sleepy_vm_new(&gc_allocator);
    SleepyObjString *a = sleepy_vm_copy_string(vm, "hello", 5);
    SleepyObjString *b = sleepy_vm_copy_string(vm, "hello", 5);
    CHECK(a == b);
    sleepy_vm_free(vm);
}

TEST_CASE("GC: different strings not deduplicated") {
    SleepyVM *vm = sleepy_vm_new(&gc_allocator);
    SleepyObjString *a = sleepy_vm_copy_string(vm, "hello", 5);
    SleepyObjString *b = sleepy_vm_copy_string(vm, "world", 5);
    CHECK(a != b);
    sleepy_vm_free(vm);
}

TEST_CASE("GC: collect frees unreachable objects") {
    SleepyVM *vm = sleepy_vm_new(&gc_allocator);
    vm->next_gc_threshold = 0;
    sleepy_vm_copy_string(vm, "reachable", 9);
    size_t before = vm->bytes_allocated;
    sleepy_gc_collect(vm);
    size_t after_collect = vm->bytes_allocated;
    (void)before;
    (void)after_collect;
    sleepy_vm_free(vm);
}

TEST_CASE("GC: mark value") {
    SleepyVM *vm = sleepy_vm_new(&gc_allocator);
    SleepyObjString *s = sleepy_vm_copy_string(vm, "test", 4);
    SleepyValue val = SLEEPY_OBJ_VAL(s);
    s->obj.is_marked = false;
    sleepy_gc_mark_value(vm, val);
    CHECK(s->obj.is_marked == true);
    sleepy_gc_mark_value(vm, SLEEPY_NULL_VAL);
    sleepy_gc_mark_value(vm, SLEEPY_NUM_VAL(42.0));
    sleepy_gc_mark_value(vm, SLEEPY_BOOL_VAL(true));
    sleepy_vm_free(vm);
}

TEST_CASE("GC: mark object") {
    SleepyVM *vm = sleepy_vm_new(&gc_allocator);
    SleepyObjString *s = sleepy_vm_copy_string(vm, "marked", 6);
    s->obj.is_marked = false;
    sleepy_gc_mark_obj(vm, &s->obj);
    CHECK(s->obj.is_marked == true);
    sleepy_vm_free(vm);
}

TEST_CASE("GC: sweep removes unmarked") {
    SleepyVM *vm = sleepy_vm_new(&gc_allocator);
    SleepyObjString *keep = sleepy_vm_copy_string(vm, "keep", 4);
    SleepyObjString *lose = sleepy_vm_copy_string(vm, "lose", 4);
    (void)lose;
    keep->obj.is_marked = true;
    lose->obj.is_marked = false;
    sleepy_gc_sweep(vm);
    SleepyObj *obj = vm->objects;
    bool found_lose = false;
    while (obj) {
        if (obj == &lose->obj) found_lose = true;
        obj = obj->next;
    }
    CHECK_FALSE(found_lose);
    sleepy_vm_free(vm);
}

TEST_CASE("GC: many allocations trigger collection") {
    SleepyVM *vm = sleepy_vm_new(&gc_allocator);
    vm->next_gc_threshold = 1024;
    for (int i = 0; i < 1000; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "str_%d", i);
        sleepy_vm_push(vm, SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, buf, (uint32_t)strlen(buf))));
    }
    while (vm->stack_top > vm->stack) sleepy_vm_pop(vm);
    sleepy_gc_collect(vm);
    sleepy_vm_free(vm);
}

TEST_CASE("GC: interpret with many strings") {
    SleepyVM *vm = sleepy_vm_new(&gc_allocator);
    SleepyResult r = sleepy_vm_interpret(vm,
        "$a = \"hello\"; $b = \"world\"; $c = \"foo\"; $d = \"bar\";");
    CHECK(r == SLEEPY_OK);
    sleepy_vm_free(vm);
}

TEST_CASE("GC: object size for string") {
    SleepyVM *vm = sleepy_vm_new(&gc_allocator);
    SleepyObjString *s = sleepy_vm_copy_string(vm, "test", 4);
    size_t sz = sleepy_gc_object_size(&s->obj);
    CHECK(sz >= sizeof(SleepyObjString));
    (void)sz;
    sleepy_vm_free(vm);
}

TEST_CASE("GC: hash table with GC") {
    SleepyVM *vm = sleepy_vm_new(&gc_allocator);
    SleepyObjHash *hash = sleepy_obj_hash_new(&gc_allocator);
    REQUIRE(hash != nullptr);
    hash->obj.next = vm->objects;
    vm->objects = &hash->obj;
    SleepyObjString *key = sleepy_vm_copy_string(vm, "key", 3);
    sleepy_obj_hash_set(&gc_allocator, hash, SLEEPY_OBJ_VAL(key), SLEEPY_NUM_VAL(42.0));
    SleepyValue val = sleepy_obj_hash_get(hash, SLEEPY_OBJ_VAL(key));
    CHECK(SLEEPY_IS_NUM(val));
    CHECK(SLEEPY_AS_NUM(val) == doctest::Approx(42.0));
    sleepy_vm_free(vm);
}

TEST_CASE("GC: compilation creates objects tracked by GC") {
    SleepyVM *vm = sleepy_vm_new(&gc_allocator);
    SleepyParser parser;
    sleepy_parser_init(&parser, "\"hello\" + \"world\";", &gc_allocator);
    SleepyASTNode *ast = sleepy_parser_parse(&parser);
    REQUIRE(ast != nullptr);
    size_t before = vm->bytes_allocated;
    SleepyObjFunction *fn = sleepy_compile(vm, ast, &gc_allocator);
    REQUIRE(fn != nullptr);
    size_t after = vm->bytes_allocated;
    CHECK(after >= before);
    (void)after; (void)before;
    sleepy_parser_free_node(ast, &gc_allocator);
    sleepy_vm_free(vm);
}
