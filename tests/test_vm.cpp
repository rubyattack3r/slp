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

TEST_CASE("VM: @_ local variable argument array") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    slp_vm_register_bridge_type(vm, "sub", nullptr, nullptr);

    // 1. Test size of @_ with 0 arguments
    {
        SlpResult result = slp_vm_interpret(vm,
            "sub test_arg0 { $result0 = size(@_); }\n"
            "test_arg0();");
        REQUIRE(result == SLP_OK);
        SlpObjString *name = slp_vm_copy_string(vm, "$result0", 8);
        SlpValue val = slp_obj_hash_get(vm->globals, SLP_OBJ_VAL(name));
        REQUIRE(SLP_IS_NUM(val));
        CHECK(SLP_AS_NUM(val) == 0.0);
    }

    // 2. Test size of @_ with 3 arguments
    {
        SlpResult result = slp_vm_interpret(vm,
            "sub test_arg3 { $result3 = size(@_); }\n"
            "test_arg3(10, 20, 30);");
        REQUIRE(result == SLP_OK);
        SlpObjString *name = slp_vm_copy_string(vm, "$result3", 8);
        SlpValue val = slp_obj_hash_get(vm->globals, SLP_OBJ_VAL(name));
        REQUIRE(SLP_IS_NUM(val));
        CHECK(SLP_AS_NUM(val) == 3.0);
    }

    slp_vm_free(vm);
}

static SlpValue get_global(SlpVM *vm, const char *name) {
    SlpObjString *key = slp_vm_copy_string(vm, name, (uint32_t)strlen(name));
    return slp_obj_hash_get(vm->globals, SLP_OBJ_VAL(key));
}

TEST_CASE("VM: unary predicate -istrue") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    slp_vm_register_bridge_type(vm, "sub", nullptr, nullptr);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = -istrue 1;"
        "$r2 = -istrue 0;"
        "$r3 = -istrue $null;"
        "$r4 = -istrue \"hello\";"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r4")) == true);
    slp_vm_free(vm);
}

TEST_CASE("VM: unary predicate -isarray") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$arr = @(1, 2, 3);"
        "$r1 = -isarray $arr;"
        "$r2 = -isarray \"string\";"
        "$r3 = -isarray 42;"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: unary predicate -ishash") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$h = %(a => 1);"
        "$r1 = -ishash $h;"
        "$r2 = -ishash @(1);"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: unary predicate -isnumber") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = -isnumber 42;"
        "$r2 = -isnumber \"hello\";"
        "$r3 = -isnumber $null;"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: unary predicate -isfunction") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    slp_vm_register_bridge_type(vm, "sub", nullptr, nullptr);
    SlpResult r = slp_vm_interpret(vm,
        "sub myfunc { return 1; }"
        "$r1 = -isfunction &myfunc;"
        "$r2 = -isfunction 42;"
        "$r3 = -isfunction \"str\";"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: unary predicate -isstring") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = -isstring \"hello\";"
        "$r2 = -isstring 42;"
        "$r3 = -isstring $null;"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: unary predicate -isnull") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = -isnull $null;"
        "$r2 = -isnull 0;"
        "$r3 = -isnull \"\";"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: unary predicate -exists and -isfile") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = -exists \"src/slp_vm.c\";"
        "$r2 = -exists \"nonexistent_file_xyz.txt\";"
        "$r3 = -isfile \"src/slp_vm.c\";"
        "$r4 = -isfile \"nonexistent_file_xyz.txt\";"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r4")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: unary predicate -isdir") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = -isdir \"src\";"
        "$r2 = -isdir \"src/slp_vm.c\";"
        "$r3 = -isdir \"nonexistent_dir\";"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: unary predicate -canread -canwrite") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = -canread \"src/slp_vm.c\";"
        "$r2 = -canwrite \"src/slp_vm.c\";"
        "$r3 = -canread \"nonexistent_xyz.txt\";"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: unary predicate unknown returns false") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r = -unknownpred 42;"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: binary predicate is") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = (\"a\" is \"a\");"
        "$r2 = (\"a\" is \"b\");"
        "$r3 = (1 is 1);"
        "$r4 = (1 is 2);"
        "$r5 = ($null is $null);"
        "$r6 = (true is true);"
        "$r7 = (true is false);"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r4")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r5")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r6")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r7")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: binary predicate eq/ne") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = (\"hello\" eq \"hello\");"
        "$r2 = (\"hello\" eq \"world\");"
        "$r3 = (\"hello\" ne \"world\");"
        "$r4 = (\"hello\" ne \"hello\");"
        "$r5 = ($null eq $null);"
        "$r6 = ($null eq \"\");"
        "$r7 = (\"\" eq $null);"
        "$r8 = (0 eq \"0\");"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r4")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r5")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r6")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r7")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r8")) == true);
    slp_vm_free(vm);
}

TEST_CASE("VM: binary predicate ismatch/hasmatch") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = (\"hello world\" ismatch \"hello world\");"
        "$r2 = (\"hello world\" ismatch \"world\");"
        "$r3 = (\"hello world\" hasmatch \"world\");"
        "$r4 = (\"hello world\" hasmatch \"xyz\");"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r4")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: binary predicate lt/gt") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = (1 lt 2);"
        "$r2 = (2 gt 1);"
        "$r3 = (2 lt 1);"
        "$r4 = (1 gt 2);"
        "$r5 = (\"abc\" lt \"abd\");"
        "$r6 = (\"abd\" gt \"abc\");"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r4")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r5")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r6")) == true);
    slp_vm_free(vm);
}

TEST_CASE("VM: negated binary predicate") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = !(\"hello\" eq \"world\");"
        "$r2 = !(\"hello\" eq \"hello\");"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: match operator =~") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = \"hello world\" =~ \"world\";"
        "$r2 = \"hello world\" =~ \"xyz\";"
        "$r3 = \"test123\" =~ \"test.*\";"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == true);
    slp_vm_free(vm);
}

TEST_CASE("VM: not match operator !=~") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = \"hello world\" !=~ \"xyz\";"
        "$r2 = \"hello world\" !=~ \"world\";"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: repeat operator x") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = \"ab\" x 3;"
        "$r2 = \"ha\" x 0;"
        "$r3 = \"x\" x 1;"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_IS_OBJ(get_global(vm, "$r1")));
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$r1"))->chars, "ababab") == 0);
    CHECK(SLP_IS_OBJ(get_global(vm, "$r2")));
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$r2"))->chars, "") == 0);
    CHECK(SLP_IS_OBJ(get_global(vm, "$r3")));
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$r3"))->chars, "x") == 0);
    slp_vm_free(vm);
}

TEST_CASE("VM: empty pattern regex match") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = \"\" =~ \"\";"
        "$r2 = \"hello\" =~ \"\";"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == false);
    slp_vm_free(vm);
}

TEST_CASE("VM: cmp binary predicate") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = (\"abc\" cmp \"abc\");"
        "$r2 = (\"abc\" cmp \"abd\");"
        "$r3 = (\"abd\" cmp \"abc\");"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_NUM(get_global(vm, "$r1")) == 0.0);
    CHECK(SLP_AS_NUM(get_global(vm, "$r2")) == -1.0);
    CHECK(SLP_AS_NUM(get_global(vm, "$r3")) == 1.0);
    slp_vm_free(vm);
}

TEST_CASE("VM: slp_vm_call with message ($0)") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$my_closure = {\n"
        "    $g0 = $0;\n"
        "    $g1 = $1;\n"
        "    $g2 = $2;\n"
        "    $glen = size(@_);\n"
        "};"
    );
    REQUIRE(r == SLP_OK);

    SlpValue closure_val = get_global(vm, "$my_closure");
    REQUIRE(SLP_IS_OBJ(closure_val));
    REQUIRE(SLP_OBJ_TYPE(closure_val) == SLP_OBJ_CLOSURE);

    // Call 1: Call with has_message = true
    slp_vm_push(vm, closure_val); // callee
    slp_vm_push(vm, SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "my message"))); // message ($0)
    slp_vm_push(vm, SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "arg1"))); // $1
    slp_vm_push(vm, SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "arg2"))); // $2

    REQUIRE(slp_vm_call(vm, 2, true) == SLP_OK);
    slp_vm_pop(vm); // Pop return value

    CHECK(SLP_IS_OBJ(get_global(vm, "$g0")));
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$g0"))->chars, "my message") == 0);
    CHECK(SLP_IS_OBJ(get_global(vm, "$g1")));
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$g1"))->chars, "arg1") == 0);
    CHECK(SLP_IS_OBJ(get_global(vm, "$g2")));
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$g2"))->chars, "arg2") == 0);
    CHECK(SLP_AS_NUM(get_global(vm, "$glen")) == 2.0);

    // Call 2: Call with has_message = false (standard call)
    slp_vm_push(vm, closure_val); // callee
    slp_vm_push(vm, SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "arg1"))); // $1
    slp_vm_push(vm, SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "arg2"))); // $2

    REQUIRE(slp_vm_call(vm, 2, false) == SLP_OK);
    slp_vm_pop(vm); // Pop return value

    CHECK(SLP_IS_NULL(get_global(vm, "$g0")));
    CHECK(SLP_IS_OBJ(get_global(vm, "$g1")));
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$g1"))->chars, "arg1") == 0);
    CHECK(SLP_IS_OBJ(get_global(vm, "$g2")));
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$g2"))->chars, "arg2") == 0);
    CHECK(SLP_AS_NUM(get_global(vm, "$glen")) == 2.0);

    slp_vm_free(vm);
}

// ---------------------------------------------------------------------------
// Runtime-error guards (Phase 1: Tier-1 safety)
// ---------------------------------------------------------------------------

TEST_CASE("VM: modulo by zero raises a runtime error, not a crash") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult result = slp_vm_interpret(vm, "$x = 5 % 0;");
    CHECK(result == SLP_RUNTIME_ERROR);
    slp_vm_free(vm);
}

TEST_CASE("VM: modulo by non-zero still works") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult result = slp_vm_interpret(vm, "$x = 7 % 3; assert($x == 1);");
    CHECK(result == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: unbounded recursion raises Stack overflow, not memory corruption") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult result = slp_vm_interpret(vm,
        "sub recurse { return recurse(); } recurse();");
    CHECK(result == SLP_RUNTIME_ERROR);
    slp_vm_free(vm);
}


// ---------------------------------------------------------------------------
// Arithmetic type coercion (Sleep loose typing)
// ---------------------------------------------------------------------------

TEST_CASE("VM: arithmetic coerces non-number operands") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "assert(\"5\" + 3 == 8); "      // string -> leading number
        "assert(\"5abc\" * 2 == 10); "  // numeric prefix only
        "assert(\"abc\" + 1 == 1); "    // non-numeric string -> 0
        "assert($null + 7 == 7); "      // null -> 0
        "assert(10 - \"4\" == 6);");
    CHECK(r == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: numeric arithmetic is unchanged by coercion") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "assert(2 + 3 == 5); assert(10 / 4 == 2.5); assert(2 ** 8 == 256);");
    CHECK(r == SLP_OK);
    slp_vm_free(vm);
}
