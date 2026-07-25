#include "doctest.h"
#include <string>
#include <string.h>

extern "C" {
#include "slp_common.h"
#include "slp_core.h"
#include "slp_value.h"
#include "slp_vm.h"
#include "slp_stdlib.h"
#include "slp_compiler.h"
#include "slp_parser.h"
#include <stdlib.h>

static void *test_vm_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static SlpAllocator vm_allocator = {test_vm_alloc, NULL};

struct CapturedVMError {
    int line;
    std::string message;
};

static void capture_vm_error(
    void *user_data, int line,
    const char *message) {
    CapturedVMError *captured =
        static_cast<CapturedVMError*>(
            user_data);
    captured->line = line;
    captured->message =
        message ? message : "";
}
}

static SlpValue get_global(SlpVM *vm, const char *name);

TEST_CASE("VM: create and free") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    slp_vm_free(vm);
}

TEST_CASE("VM: source name initializes the Sleep script metadata") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);

    slp_vm_set_source_name(vm, "/tmp/scripts/cmdline.sl");
    SlpObjString *key = slp_vm_copy_cstr(vm, "$__SCRIPT__");
    SlpValue script =
        slp_obj_hash_get(vm->globals, SLP_OBJ_VAL(key));
    REQUIRE(SLP_IS_OBJ(script));
    REQUIRE(SLP_OBJ_TYPE(script) == SLP_OBJ_STRING);
    CHECK(strcmp(SLP_AS_STRING(script)->chars, "cmdline.sl") == 0);

    slp_vm_free(vm);
}

TEST_CASE("VM: interpret simple addition") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult result = slp_vm_interpret(vm, "1 + 2;");
    CHECK(result == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: unknown bareword is a compile error") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    CapturedVMError captured = {};
    slp_vm_set_error_fn(
        vm, capture_vm_error, &captured);

    CHECK(slp_vm_interpret(
              vm,
              "$value = unknown_bareword;") ==
          SLP_COMPILE_ERROR);
    CHECK(captured.line == 1);
    CHECK(captured.message ==
          "Unknown expression");

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

TEST_CASE("VM: assignment while accepts zero and empty strings until null") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);

    SlpResult result = slp_vm_interpret(
        vm,
        "$generator = {"
        "  yield 0;"
        "  yield '';"
        "  yield 'value';"
        "  return $null;"
        "};"
        "$count = 0;"
        "while $item ([$generator]) {"
        "  $count++;"
        "  $last = $item;"
        "}");

    REQUIRE(result == SLP_OK);
    CHECK(slp_value_as_number(
              get_global(vm, "$count")) == 3.0);
    REQUIRE(SLP_IS_OBJ(
        get_global(vm, "$last")));
    CHECK(strcmp(
        SLP_AS_STRING(
            get_global(vm, "$last"))->chars,
        "value") == 0);

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

TEST_CASE("VM: value-only foreach yields hash keys and array values") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);

    SlpResult result = slp_vm_interpret(
        vm,
        "%items = %('key' => 'value');"
        "foreach $item (%items) { $hash_item = $item; }"
        "@items = @('value');"
        "foreach $item (@items) {"
        "  $array_item = $item;"
        "  $remove_source = remove();"
        "}");

    REQUIRE(result == SLP_OK);
    REQUIRE(SLP_IS_OBJ(
        get_global(vm, "$hash_item")));
    CHECK(strcmp(
        SLP_AS_STRING(
            get_global(vm, "$hash_item"))->chars,
        "key") == 0);
    REQUIRE(SLP_IS_OBJ(
        get_global(vm, "$array_item")));
    CHECK(strcmp(
        SLP_AS_STRING(
            get_global(vm, "$array_item"))->chars,
        "value") == 0);
    CHECK(SLP_AS_ARRAY(
              get_global(vm, "$remove_source")) ==
          SLP_AS_ARRAY(
              get_global(vm, "@items")));
    CHECK(SLP_AS_ARRAY(
              get_global(vm, "@items"))->count == 0);

    slp_vm_free(vm);
}

TEST_CASE("VM: for loop discards initializer and increment results") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);

    SlpResult result = slp_vm_interpret(
        vm,
        "$sum = 0;"
        "for ($i = 0, $odd = 1;"
        "     $i < 10000;"
        "     $i++, $odd += 2) {"
        "  $sum += $odd;"
        "}");

    REQUIRE(result == SLP_OK);
    SlpValue sum = get_global(vm, "$sum");
    REQUIRE(SLP_IS_NUM(sum));
    CHECK(SLP_AS_NUM(sum) == 100000000.0);
    CHECK(slp_value_as_number(get_global(vm, "$i")) == 10000.0);
    CHECK(slp_value_as_number(get_global(vm, "$odd")) == 20001.0);
    CHECK(vm->stack_top == vm->stack + 1);

    slp_vm_free(vm);
}

TEST_CASE("VM: value stack guards overflow and underflow") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);

    for (int index = 0; index < SLP_STACK_MAX; index++)
        slp_vm_push(vm, SLP_NUM_VAL((double)index));
    REQUIRE(vm->stack_top == vm->stack + SLP_STACK_MAX);

    slp_vm_push(vm, SLP_TRUE_VAL);
    CHECK(vm->abort_requested);
    CHECK(vm->stack_top == vm->stack + SLP_STACK_MAX);

    vm->stack_top = vm->stack;
    vm->abort_requested = false;
    CHECK(SLP_IS_NULL(slp_vm_pop(vm)));
    CHECK(vm->abort_requested);
    CHECK(vm->stack_top == vm->stack);

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
        "try { throw 'boom'; }"
        "catch $e { $caught = $e; }");
    CHECK(result == SLP_OK);
    SlpObjString *caught_name =
        slp_vm_copy_cstr(vm, "$caught");
    SlpValue caught = slp_obj_hash_get(
        vm->globals, SLP_OBJ_VAL(caught_name));
    REQUIRE(SLP_IS_OBJ(caught));
    REQUIRE(SLP_OBJ_TYPE(caught) == SLP_OBJ_STRING);
    CHECK(strcmp(SLP_AS_STRING(caught)->chars, "boom") == 0);
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

TEST_CASE("VM: hasmatch advances, captures, and resets after exhaustion") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    slp_stdlib_init(vm);
    vm->next_gc_threshold = 0;
    SlpResult r = slp_vm_interpret(vm,
        "$text = \"(111) 222-3333 (444) 555-6666\";"
        "$pattern = '\\\\((\\\\d\\\\d\\\\d)\\\\) (\\\\d\\\\d\\\\d-\\\\d\\\\d\\\\d\\\\d)';"
        "$r1 = ($text hasmatch $pattern); @m1 = matched();"
        "$r2 = ($text hasmatch $pattern); @m2 = matched();"
        "$r3 = ($text hasmatch $pattern);"
        "$r4 = ($text hasmatch $pattern); @m4 = matched();"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r4")) == true);

    SlpValue m1_value = get_global(vm, "@m1");
    REQUIRE(SLP_IS_OBJ(m1_value));
    REQUIRE(SLP_OBJ_TYPE(m1_value) == SLP_OBJ_ARRAY);
    SlpObjArray *m1 = SLP_AS_ARRAY(m1_value);
    REQUIRE(m1->count == 2);
    CHECK(strcmp(SLP_AS_STRING(slp_obj_array_get(m1, 0))->chars, "111") == 0);
    CHECK(strcmp(SLP_AS_STRING(slp_obj_array_get(m1, 1))->chars, "222-3333") == 0);

    SlpValue m2_value = get_global(vm, "@m2");
    REQUIRE(SLP_IS_OBJ(m2_value));
    REQUIRE(SLP_OBJ_TYPE(m2_value) == SLP_OBJ_ARRAY);
    SlpObjArray *m2 = SLP_AS_ARRAY(m2_value);
    REQUIRE(m2->count == 2);
    CHECK(strcmp(SLP_AS_STRING(slp_obj_array_get(m2, 0))->chars, "444") == 0);
    CHECK(strcmp(SLP_AS_STRING(slp_obj_array_get(m2, 1))->chars, "555-6666") == 0);

    SlpValue m4_value = get_global(vm, "@m4");
    REQUIRE(SLP_IS_OBJ(m4_value));
    REQUIRE(SLP_OBJ_TYPE(m4_value) == SLP_OBJ_ARRAY);
    SlpObjArray *m4 = SLP_AS_ARRAY(m4_value);
    REQUIRE(m4->count == 2);
    CHECK(strcmp(SLP_AS_STRING(slp_obj_array_get(m4, 0))->chars, "111") == 0);
    slp_vm_free(vm);
}

TEST_CASE("VM: nested hasmatch loops retain independent matcher positions") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    slp_stdlib_init(vm);
    SlpResult r = slp_vm_interpret(vm,
        "$outer = \"a1 b2\"; $inner = \"x9 y8\";"
        "$pattern = '([a-z])(\\\\d)';"
        "$r1 = ($outer hasmatch $pattern); @o1 = matched();"
        "$r2 = ($inner hasmatch $pattern); @i1 = matched();"
        "$r3 = ($inner hasmatch $pattern); @i2 = matched();"
        "$r4 = ($inner hasmatch $pattern);"
        "$r5 = ($outer hasmatch $pattern); @o2 = matched();"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r4")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r5")) == true);

    SlpValue o1_value = get_global(vm, "@o1");
    SlpValue i1_value = get_global(vm, "@i1");
    SlpValue i2_value = get_global(vm, "@i2");
    SlpValue o2_value = get_global(vm, "@o2");
    REQUIRE(SLP_IS_OBJ(o1_value));
    REQUIRE(SLP_IS_OBJ(i1_value));
    REQUIRE(SLP_IS_OBJ(i2_value));
    REQUIRE(SLP_IS_OBJ(o2_value));
    REQUIRE(SLP_OBJ_TYPE(o1_value) == SLP_OBJ_ARRAY);
    REQUIRE(SLP_OBJ_TYPE(i1_value) == SLP_OBJ_ARRAY);
    REQUIRE(SLP_OBJ_TYPE(i2_value) == SLP_OBJ_ARRAY);
    REQUIRE(SLP_OBJ_TYPE(o2_value) == SLP_OBJ_ARRAY);
    SlpObjArray *o1 = SLP_AS_ARRAY(o1_value);
    SlpObjArray *i1 = SLP_AS_ARRAY(i1_value);
    SlpObjArray *i2 = SLP_AS_ARRAY(i2_value);
    SlpObjArray *o2 = SLP_AS_ARRAY(o2_value);
    REQUIRE(o1->count == 2);
    REQUIRE(i1->count == 2);
    REQUIRE(i2->count == 2);
    REQUIRE(o2->count == 2);
    CHECK(strcmp(SLP_AS_STRING(slp_obj_array_get(o1, 0))->chars, "a") == 0);
    CHECK(strcmp(SLP_AS_STRING(slp_obj_array_get(i1, 0))->chars, "x") == 0);
    CHECK(strcmp(SLP_AS_STRING(slp_obj_array_get(i2, 0))->chars, "y") == 0);
    CHECK(strcmp(SLP_AS_STRING(slp_obj_array_get(o2, 0))->chars, "b") == 0);
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

TEST_CASE("VM: binary predicate ge/le") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    SlpResult r = slp_vm_interpret(vm,
        "$r1 = (2 ge 1);"
        "$r2 = (1 ge 1);"
        "$r3 = (1 ge 2);"
        "$r4 = (1 le 2);"
        "$r5 = (1 le 1);"
        "$r6 = (2 le 1);"
        "$r7 = (\"abc\" ge \"abc\");"
        "$r8 = (\"abc\" le \"abc\");"
        "$r9 = (\"abd\" ge \"abc\");"
        "$r10 = (\"abc\" le \"abd\");"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r1")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r2")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r3")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r4")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r5")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r6")) == false);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r7")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r8")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r9")) == true);
    CHECK(SLP_AS_BOOL(get_global(vm, "$r10")) == true);
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
        "$r4 = ^String x 2;"
    );
    REQUIRE(r == SLP_OK);
    CHECK(SLP_IS_OBJ(get_global(vm, "$r1")));
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$r1"))->chars, "ababab") == 0);
    CHECK(SLP_IS_OBJ(get_global(vm, "$r2")));
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$r2"))->chars, "") == 0);
    CHECK(SLP_IS_OBJ(get_global(vm, "$r3")));
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$r3"))->chars, "x") == 0);
    CHECK(strcmp(
              SLP_AS_STRING(get_global(vm, "$r4"))->chars,
              "class java.lang.Stringclass java.lang.String") == 0);
    slp_vm_free(vm);
}

TEST_CASE("VM: portable Java lists retain object identity and methods") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    slp_stdlib_init(vm);

    SlpResult result = slp_vm_interpret(
        vm,
        "$list = [new LinkedList]; "
        "$added = [$list add: 'first']; "
        "[$list add: 'second']; "
        "$size = [$list size]; "
        "$first = [$list get: 0]; "
        "$is_list = $list isa ^List; "
        "$is_collection = $list isa ^Collection; "
        "$is_map = $list isa ^Map;");
    REQUIRE(result == SLP_OK);

    SlpValue list = get_global(vm, "$list");
    REQUIRE(SLP_IS_OBJ(list));
    REQUIRE(SLP_OBJ_TYPE(list) == SLP_OBJ_JAVA_OBJECT);
    CHECK(SLP_AS_JAVA_OBJECT(list)->kind == SLP_JAVA_LIST);
    CHECK(slp_value_as_number(get_global(vm, "$added")) == 1.0);
    CHECK(slp_value_as_number(get_global(vm, "$size")) == 2.0);
    CHECK(strcmp(
              SLP_AS_STRING(get_global(vm, "$first"))->chars,
              "first") == 0);
    CHECK(!slp_value_is_falsy(get_global(vm, "$is_list")));
    CHECK(!slp_value_is_falsy(
        get_global(vm, "$is_collection")));
    CHECK(slp_value_is_falsy(get_global(vm, "$is_map")));
    CHECK(strcmp(
              slp_vm_stringify(vm, list)->chars,
              "[first, second]") == 0);

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
        "assert(2 + 3 == 5); assert(10 / 4 == 2);"
        "assert((10.0 / 4) == 2.5); assert(2 ** 8 == 256);");
    CHECK(r == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: Int Long and Double types propagate like Sleep") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);

    SlpResult result = slp_vm_interpret(
        vm,
        "$integer = 7;"
        "$long = 7L;"
        "$double = 7.0;"
        "$power = 2 ** 3;"
        "$octal = 0123;"
        "$hexlong = 0xAAAABBBBBBBL;");
    REQUIRE(result == SLP_OK);

    CHECK(SLP_IS_NUM(get_global(vm, "$integer")));
    REQUIRE(SLP_IS_OBJ(get_global(vm, "$long")));
    CHECK(SLP_OBJ_TYPE(get_global(vm, "$long")) == SLP_OBJ_LONG);
    REQUIRE(SLP_IS_OBJ(get_global(vm, "$double")));
    CHECK(SLP_OBJ_TYPE(get_global(vm, "$double")) == SLP_OBJ_DOUBLE);
    REQUIRE(SLP_IS_OBJ(get_global(vm, "$power")));
    CHECK(SLP_OBJ_TYPE(get_global(vm, "$power")) == SLP_OBJ_DOUBLE);
    CHECK(slp_value_as_number(get_global(vm, "$octal")) == 83.0);
    REQUIRE(SLP_IS_OBJ(get_global(vm, "$hexlong")));
    REQUIRE(SLP_OBJ_TYPE(get_global(vm, "$hexlong")) == SLP_OBJ_LONG);
    CHECK(SLP_AS_LONG(get_global(vm, "$hexlong"))->value ==
          11728141925307LL);

    slp_vm_free(vm);
}

TEST_CASE("VM: long values stringify without a source literal suffix") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    slp_vm_ffi_set_long(vm, 0, 4847324738247832LL);
    SlpObjString *text = slp_vm_stringify(vm, vm->ffi_slots[0]);
    REQUIRE(text != nullptr);
    CHECK(strcmp(text->chars, "4847324738247832") == 0);
    slp_vm_free(vm);
}

TEST_CASE("VM: closures stringify with Sleep source spans and identities") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    slp_vm_set_source_name(vm, "/tmp/closure_meta.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "$first = {\n"
        "    println(\"first\");\n"
        "};\n"
        "$second = { return 2; };\n");
    REQUIRE(result == SLP_OK);

    SlpObjString *first = slp_vm_stringify(vm, get_global(vm, "$first"));
    SlpObjString *second = slp_vm_stringify(vm, get_global(vm, "$second"));
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK_MESSAGE(
        strcmp(first->chars, "&closure[closure_meta.sl:2]#1") == 0,
        first->chars);
    CHECK_MESSAGE(
        strcmp(second->chars, "&closure[closure_meta.sl:4]#2") == 0,
        second->chars);

    slp_vm_free(vm);
}

TEST_CASE("VM: recursive collections stringify with Sleep references") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    SlpResult result = slp_vm_interpret(
        vm,
        "@array = @(\"a\"); push(@array, @array);"
        "%hash = %(); %hash[\"a\"] = %hash;");
    REQUIRE(result == SLP_OK);

    SlpObjString *array = slp_vm_stringify(vm, get_global(vm, "@array"));
    SlpObjString *hash = slp_vm_stringify(vm, get_global(vm, "%hash"));
    REQUIRE(array != nullptr);
    REQUIRE(hash != nullptr);
    CHECK(strcmp(array->chars, "@('a', @0)") == 0);
    CHECK(strcmp(hash->chars, "%(a => %0)") == 0);

    slp_vm_free(vm);
}

TEST_CASE("VM: common Java String object expressions match Sleep") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    SlpResult result = slp_vm_interpret(
        vm,
        "assert [\"test\" length] == 4;"
        "assert [\"  test \\t\" trim] eq \"test\";"
        "assert [\"same\" equals: \"same\"] == 1;"
        "assert [\"same\" equals: \"other\"] == 0;"
        "assert [\"&dangerous\" substring: 1, 7] eq \"danger\";");
    CHECK(result == SLP_OK);
    slp_vm_free(vm);
}

TEST_CASE("VM: foreach values remain references to array elements") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    SlpResult result = slp_vm_interpret(
        vm,
        "@items = @('a', 'b', 'c');"
        "foreach $item (@items) { $item = 'changed'; }"
        "@filtered = @('a', 'b', 'c');"
        "foreach $item (@filtered) {"
        "    if ($item eq 'b') { remove(); }"
        "}");
    REQUIRE(result == SLP_OK);

    SlpObjArray *items = SLP_AS_ARRAY(get_global(vm, "@items"));
    REQUIRE(items->count == 3);
    for (int i = 0; i < items->count; i++) {
        SlpValue value = slp_obj_array_get(items, i);
        REQUIRE(SLP_IS_OBJ(value));
        REQUIRE(SLP_OBJ_TYPE(value) == SLP_OBJ_STRING);
        CHECK(strcmp(SLP_AS_STRING(value)->chars, "changed") == 0);
    }
    SlpObjArray *filtered = SLP_AS_ARRAY(get_global(vm, "@filtered"));
    REQUIRE(filtered->count == 2);
    CHECK(strcmp(
        SLP_AS_STRING(slp_obj_array_get(filtered, 0))->chars, "a") == 0);
    CHECK(strcmp(
        SLP_AS_STRING(slp_obj_array_get(filtered, 1))->chars, "c") == 0);

    slp_vm_free(vm);
}

TEST_CASE("VM: floating values use Java-compatible shortest formatting") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);

    SlpObjString *sum = slp_vm_stringify(
        vm, SLP_NUM_VAL(40.2 + 9.1));
    SlpObjString *small = slp_vm_stringify(
        vm, SLP_NUM_VAL(2.3574857347858734e-22));
    REQUIRE(sum != nullptr);
    REQUIRE(small != nullptr);
    CHECK(strcmp(sum->chars, "49.300000000000004") == 0);
    CHECK(strcmp(small->chars, "2.3574857347858734E-22") == 0);

    slp_vm_free(vm);
}

TEST_CASE("VM: ordinary binary operands evaluate right to left") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    slp_stdlib_init(vm);

    SlpResult result = slp_vm_interpret(
        vm,
        "@values = @('a', 'b', 'c');"
        "$text = shift(@values) . ':' . size(@values);");
    REQUIRE(result == SLP_OK);

    SlpValue text = get_global(vm, "$text");
    REQUIRE(SLP_IS_OBJ(text));
    REQUIRE(SLP_OBJ_TYPE(text) == SLP_OBJ_STRING);
    CHECK(strcmp(SLP_AS_STRING(text)->chars, "a:3") == 0);

    slp_vm_free(vm);
}

TEST_CASE("VM: postfix mutation evaluates to the assigned value") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);

    SlpResult result = slp_vm_interpret(
        vm,
        "$x = 0;"
        "$incremented = $x++;"
        "$decremented = $x--;");
    REQUIRE(result == SLP_OK);
    CHECK(SLP_AS_NUM(get_global(vm, "$incremented")) == 1.0);
    CHECK(SLP_AS_NUM(get_global(vm, "$decremented")) == 0.0);
    CHECK(SLP_AS_NUM(get_global(vm, "$x")) == 0.0);

    slp_vm_free(vm);
}

TEST_CASE("VM: tuple assignment unpacks arrays and broadcasts scalars") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    slp_stdlib_init(vm);

    SlpResult result = slp_vm_interpret(
        vm,
        "($first) = @('one', 'two');"
        "($x, $y, $z) = @('a', 'b');"
        "($left, $right) = 'same';");
    REQUIRE(result == SLP_OK);
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$first"))->chars, "one") == 0);
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$x"))->chars, "a") == 0);
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$y"))->chars, "b") == 0);
    CHECK(SLP_IS_NULL(get_global(vm, "$z")));
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$left"))->chars, "same") == 0);
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$right"))->chars, "same") == 0);

    slp_vm_free(vm);
}

TEST_CASE("VM: tuple compound assignment maps scalars and arrays") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    slp_stdlib_init(vm);

    SlpResult result = slp_vm_interpret(
        vm,
        "($a, $b, $c) = @(2, 3, 4);"
        "($a, $b, $c) *= @(5, 6, 7);"
        "($a, $b, $c) += 2;"
        "@items = @(10, 12, 14);"
        "(@items) *= @(5, 4, 3);"
        "@words = @('a', 'b');"
        "(@words) .= @('1', '2');"
        "$single = 1;"
        "($single) += @(4, 99);");
    REQUIRE(result == SLP_OK);

    CHECK(slp_value_as_number(get_global(vm, "$a")) == 12.0);
    CHECK(slp_value_as_number(get_global(vm, "$b")) == 20.0);
    CHECK(slp_value_as_number(get_global(vm, "$c")) == 30.0);
    CHECK(slp_value_as_number(get_global(vm, "$single")) == 5.0);

    SlpObjArray *items = SLP_AS_ARRAY(get_global(vm, "@items"));
    REQUIRE(items->count == 3);
    CHECK(slp_value_as_number(items->elements[0]) == 50.0);
    CHECK(slp_value_as_number(items->elements[1]) == 48.0);
    CHECK(slp_value_as_number(items->elements[2]) == 42.0);

    SlpObjArray *words = SLP_AS_ARRAY(get_global(vm, "@words"));
    REQUIRE(words->count == 2);
    CHECK(strcmp(SLP_AS_STRING(words->elements[0])->chars, "a1") == 0);
    CHECK(strcmp(SLP_AS_STRING(words->elements[1])->chars, "b2") == 0);

    slp_vm_free(vm);
}

TEST_CASE("VM: negative array indices wrap until normalized") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    slp_stdlib_init(vm);

    SlpResult result = slp_vm_interpret(
        vm,
        "@values = @('a', 'b', 'c');"
        "$read = @values[-7];"
        "@values[-8] = 'changed';");
    REQUIRE(result == SLP_OK);
    CHECK(strcmp(SLP_AS_STRING(get_global(vm, "$read"))->chars, "c") == 0);
    SlpObjArray *values = SLP_AS_ARRAY(get_global(vm, "@values"));
    REQUIRE(values->count == 3);
    CHECK(strcmp(SLP_AS_STRING(values->elements[1])->chars, "changed") == 0);

    slp_vm_free(vm);
}

TEST_CASE("VM: nested index assignment auto-vivifies arrays and hashes") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    slp_stdlib_init(vm);

    SlpResult result = slp_vm_interpret(
        vm,
        "@data[2][1] = 'array value';"
        "%state['x']['y'] = 'hash value';"
        "$holder = %();"
        "$holder['nested']['key'] = 'dynamic value';");
    REQUIRE(result == SLP_OK);

    SlpValue data_value = get_global(vm, "@data");
    REQUIRE(SLP_IS_OBJ(data_value));
    REQUIRE(SLP_OBJ_TYPE(data_value) == SLP_OBJ_ARRAY);
    SlpObjArray *data = SLP_AS_ARRAY(data_value);
    REQUIRE(data->count == 3);
    SlpValue nested_array_value = slp_obj_array_get(data, 2);
    REQUIRE(SLP_IS_OBJ(nested_array_value));
    REQUIRE(SLP_OBJ_TYPE(nested_array_value) == SLP_OBJ_ARRAY);
    SlpObjArray *nested_array = SLP_AS_ARRAY(nested_array_value);
    REQUIRE(nested_array->count == 2);
    CHECK(strcmp(
        SLP_AS_STRING(slp_obj_array_get(nested_array, 1))->chars,
        "array value") == 0);

    SlpValue state_value = get_global(vm, "%state");
    REQUIRE(SLP_IS_OBJ(state_value));
    REQUIRE(SLP_OBJ_TYPE(state_value) == SLP_OBJ_HASH);
    SlpValue x_value = slp_obj_hash_get(
        SLP_AS_HASH(state_value),
        SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "x")));
    REQUIRE(SLP_IS_OBJ(x_value));
    REQUIRE(SLP_OBJ_TYPE(x_value) == SLP_OBJ_HASH);
    SlpValue y_value = slp_obj_hash_get(
        SLP_AS_HASH(x_value),
        SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "y")));
    REQUIRE(SLP_IS_OBJ(y_value));
    REQUIRE(SLP_OBJ_TYPE(y_value) == SLP_OBJ_STRING);
    CHECK(strcmp(SLP_AS_STRING(y_value)->chars, "hash value") == 0);

    SlpValue holder_value = get_global(vm, "$holder");
    REQUIRE(SLP_IS_OBJ(holder_value));
    REQUIRE(SLP_OBJ_TYPE(holder_value) == SLP_OBJ_HASH);
    SlpValue holder_nested = slp_obj_hash_get(
        SLP_AS_HASH(holder_value),
        SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "nested")));
    REQUIRE(SLP_IS_OBJ(holder_nested));
    REQUIRE(SLP_OBJ_TYPE(holder_nested) == SLP_OBJ_HASH);
    SlpValue holder_key = slp_obj_hash_get(
        SLP_AS_HASH(holder_nested),
        SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "key")));
    REQUIRE(SLP_IS_OBJ(holder_key));
    REQUIRE(SLP_OBJ_TYPE(holder_key) == SLP_OBJ_STRING);
    CHECK(strcmp(SLP_AS_STRING(holder_key)->chars, "dynamic value") == 0);

    slp_vm_free(vm);
}

TEST_CASE("VM: resumed continuations return to handlers after owner completion") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);
    slp_stdlib_init(vm);

    SlpResult result = slp_vm_interpret(
        vm,
        "sub returning_owner {"
        "  $value = callcc {"
        "    $continuation = $1;"
        "    $owner_result = [$continuation: 'resumed'];"
        "    return 'handler got ' . $owner_result;"
        "  };"
        "  return 'owner got ' . $value;"
        "}"
        "sub exception_handler { invoke($1); return 42; }"
        "sub throwing_owner {"
        "  try { callcc &exception_handler; throw 'caught'; }"
        "  catch $exception { $caught = $exception; }"
        "  return 'owner complete';"
        "}"
        "$return_result = returning_owner();"
        "$exception_result = throwing_owner();");
    REQUIRE(result == SLP_OK);

    CHECK(strcmp(
        SLP_AS_STRING(get_global(vm, "$return_result"))->chars,
        "handler got owner got resumed") == 0);
    CHECK(slp_value_as_number(
              get_global(vm, "$exception_result")) == 42.0);
    CHECK(strcmp(
        SLP_AS_STRING(get_global(vm, "$caught"))->chars,
        "caught") == 0);

    slp_vm_free(vm);
}
