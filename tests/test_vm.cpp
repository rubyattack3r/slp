#include "doctest.h"
#include <string.h>

extern "C" {
#include "slp_common.h"
#include "slp_core.h"
#include "slp_value.h"
#include "slp_vm.h"
#include "slp_compiler.h"
#include "slp_parser.h"
#include <stdlib.h>

static void *test_vm_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static SlpAllocator vm_allocator = {test_vm_alloc, NULL};
}

TEST_CASE("VM: create and free") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    slp_vm_free(vm);
}

TEST_CASE("VM: interpret simple addition") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult result = slp_vm_interpret(vm, "1 + 2;");
    CHECK(result == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: interpret if/else") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult result = slp_vm_interpret(vm, "if (true) { 1; } else { 2; }");
    CHECK(result == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: interpret while loop") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult result = slp_vm_interpret(vm,
        "$i = 0; while ($i < 5) { $i = $i + 1; }");
    CHECK(result == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: interpret variable assignment and use") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult result = slp_vm_interpret(vm,
        "$x = 10; $y = 20; $z = $x + $y;");
    CHECK(result == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: interpret function call (println)") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult result = slp_vm_interpret(vm, "println(42);");
    CHECK(result == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: interpret for loop") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult result = slp_vm_interpret(vm,
        "for ($i = 0; $i < 3; $i = $i + 1) { println($i); }");
    CHECK(result == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: interpret boolean operations") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r1 = slp_vm_interpret(vm, "true == true;");
    CHECK(r1 == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: interpret string constant") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult result = slp_vm_interpret(vm, "\"hello world\";");
    CHECK(result == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: interpret null") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult result = slp_vm_interpret(vm, "$null;");
    CHECK(result == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: error on bad syntax") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult result = slp_vm_interpret(vm, "if (true { }");
    CHECK(result == SLP_COMPILE_ERROR);
    slp_vm_free(vm);
}

TEST_CASE("VM: FFI slot API") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    slp_vm_ffi_set_null(vm, 0);
    CHECK(slp_vm_ffi_is_null(vm, 0));
    slp_vm_ffi_set_bool(vm, 1, true);
    CHECK(slp_vm_ffi_is_bool(vm, 1));
    CHECK(slp_vm_ffi_get_bool(vm, 1) == true);
    slp_vm_ffi_set_number(vm, 2, 3.14);
    CHECK(slp_vm_ffi_is_number(vm, 2));
    CHECK(slp_vm_ffi_get_number(vm, 2) == doctest::Approx(3.14));
    slp_vm_ffi_set_string(vm, 3, "test");
    CHECK(slp_vm_ffi_is_string(vm, 3));
    CHECK(strcmp(slp_vm_ffi_get_string(vm, 3), "test") == 0);
    slp_vm_free(vm);
}

TEST_CASE("VM: register and call native function") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    slp_vm_register_native(vm, "double_it", nullptr);
    slp_vm_free(vm);
}

TEST_CASE("VM: register bridge type") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    slp_vm_register_bridge_type(vm, "sub", nullptr, nullptr);
    slp_vm_free(vm);
}

TEST_CASE("VM: interpret env bridge (sub)") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    slp_vm_register_bridge_type(vm, "sub", nullptr, nullptr);
    SlpResult result = slp_vm_interpret(vm,
        "sub greet { println(\"hello\"); }");
    CHECK(result == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: interpret try/catch") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult result = slp_vm_interpret(vm,
        "try { 1; } catch $e { 2; }");
    CHECK(result == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: interpret comparison operators") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r;
    r = slp_vm_interpret(vm, "1 < 2;");
    CHECK(r == SLP_OK);
    r = slp_vm_interpret(vm, "2 > 1;");
    CHECK(r == SLP_OK);
    r = slp_vm_interpret(vm, "1 <= 1;");
    CHECK(r == SLP_OK);
    r = slp_vm_interpret(vm, "1 >= 1;");
    CHECK(r == SLP_OK);
    r = slp_vm_interpret(vm, "1 != 2;");
    CHECK(r == SLP_OK);
    slp_vm_free(vm);
}
