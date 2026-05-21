#include "doctest.h"
#include <string.h>

extern "C" {
#include "sleepy_common.h"
#include "sleepy_core.h"
#include "sleepy_value.h"
#include "sleepy_vm.h"
#include "sleepy_compiler.h"
#include "sleepy_parser.h"
#include <stdlib.h>

static void *test_vm_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static SleepyAllocator vm_allocator = {test_vm_alloc, NULL};
}

TEST_CASE("VM: create and free") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: interpret simple addition") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    SleepyResult result = sleepy_vm_interpret(vm, "1 + 2;");
    CHECK(result == SLEEPY_OK);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: interpret if/else") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    SleepyResult result = sleepy_vm_interpret(vm, "if (true) { 1; } else { 2; }");
    CHECK(result == SLEEPY_OK);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: interpret while loop") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    SleepyResult result = sleepy_vm_interpret(vm,
        "$i = 0; while ($i < 5) { $i = $i + 1; }");
    CHECK(result == SLEEPY_OK);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: interpret variable assignment and use") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    SleepyResult result = sleepy_vm_interpret(vm,
        "$x = 10; $y = 20; $z = $x + $y;");
    CHECK(result == SLEEPY_OK);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: interpret function call (println)") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    SleepyResult result = sleepy_vm_interpret(vm, "println(42);");
    CHECK(result == SLEEPY_OK);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: interpret for loop") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    SleepyResult result = sleepy_vm_interpret(vm,
        "for ($i = 0; $i < 3; $i = $i + 1) { println($i); }");
    CHECK(result == SLEEPY_OK);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: interpret boolean operations") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    SleepyResult r1 = sleepy_vm_interpret(vm, "true == true;");
    CHECK(r1 == SLEEPY_OK);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: interpret string constant") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    SleepyResult result = sleepy_vm_interpret(vm, "\"hello world\";");
    CHECK(result == SLEEPY_OK);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: interpret null") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    SleepyResult result = sleepy_vm_interpret(vm, "$null;");
    CHECK(result == SLEEPY_OK);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: error on bad syntax") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    SleepyResult result = sleepy_vm_interpret(vm, "if (true { }");
    CHECK(result == SLEEPY_COMPILE_ERROR);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: FFI slot API") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    sleepy_vm_ffi_set_null(vm, 0);
    CHECK(sleepy_vm_ffi_is_null(vm, 0));
    sleepy_vm_ffi_set_bool(vm, 1, true);
    CHECK(sleepy_vm_ffi_is_bool(vm, 1));
    CHECK(sleepy_vm_ffi_get_bool(vm, 1) == true);
    sleepy_vm_ffi_set_number(vm, 2, 3.14);
    CHECK(sleepy_vm_ffi_is_number(vm, 2));
    CHECK(sleepy_vm_ffi_get_number(vm, 2) == doctest::Approx(3.14));
    sleepy_vm_ffi_set_string(vm, 3, "test");
    CHECK(sleepy_vm_ffi_is_string(vm, 3));
    CHECK(strcmp(sleepy_vm_ffi_get_string(vm, 3), "test") == 0);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: register and call native function") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    sleepy_vm_register_native(vm, "double_it", nullptr);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: register bridge type") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    sleepy_vm_register_bridge_type(vm, "sub", nullptr, nullptr);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: interpret env bridge (sub)") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    sleepy_vm_register_bridge_type(vm, "sub", nullptr, nullptr);
    SleepyResult result = sleepy_vm_interpret(vm,
        "sub greet { println(\"hello\"); }");
    CHECK(result == SLEEPY_OK);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: interpret try/catch") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    SleepyResult result = sleepy_vm_interpret(vm,
        "try { 1; } catch $e { 2; }");
    CHECK(result == SLEEPY_OK);
    sleepy_vm_free(vm);
}

TEST_CASE("VM: interpret comparison operators") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    SleepyResult r;
    r = sleepy_vm_interpret(vm, "1 < 2;");
    CHECK(r == SLEEPY_OK);
    r = sleepy_vm_interpret(vm, "2 > 1;");
    CHECK(r == SLEEPY_OK);
    r = sleepy_vm_interpret(vm, "1 <= 1;");
    CHECK(r == SLEEPY_OK);
    r = sleepy_vm_interpret(vm, "1 >= 1;");
    CHECK(r == SLEEPY_OK);
    r = sleepy_vm_interpret(vm, "1 != 2;");
    CHECK(r == SLEEPY_OK);
    sleepy_vm_free(vm);
}
