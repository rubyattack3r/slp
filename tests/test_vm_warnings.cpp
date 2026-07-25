#include "doctest.h"

extern "C" {
#include "slp_stdlib.h"
#include "slp_vm.h"
}

#include <stdlib.h>

#include <string>

namespace {

static void *warning_alloc(void *memory, size_t size, void *user_data) {
    (void)user_data;
    if (size == 0) {
        free(memory);
        return NULL;
    }
    return realloc(memory, size);
}

static SlpAllocator warning_allocator = {warning_alloc, NULL};

static void capture_warning_output(void *user_data, const char *text) {
    std::string *output = static_cast<std::string *>(user_data);
    if (text) *output += text;
}

static SlpValue warning_global(SlpVM *vm, const char *name) {
    SlpObjString *key = slp_vm_copy_cstr(vm, name);
    return slp_obj_hash_get(vm->globals, SLP_OBJ_VAL(key));
}

} // namespace

TEST_CASE("VM warnings: strict mode warns once when an assignment declares a variable") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "/tmp/strict-assignment.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "debug(7);\n"
        "$missing = 1;\n"
        "$missing = 2;\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Warning: variable '$missing' not declared at strict-assignment.sl:2\n");
    CHECK(SLP_AS_NUM(warning_global(vm, "$missing")) == 2.0);
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: declarations and disabled strict mode suppress warnings") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "declared.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "global('$declared');\n"
        "debug(7);\n"
        "$declared = 1;\n"
        "debug(0);\n"
        "$implicit = 2;\n");

    CHECK(result == SLP_OK);
    CHECK(output.empty());
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: watch follows scalar references and describes assignments") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "watch-unit.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "global('$x $y');\n"
        "$x = 3;\n"
        "watch('$x $y');\n"
        "sub mutate { $1 = $1 * 5; return $1; }\n"
        "$y = mutate($x);\n"
        "$y = 'text';\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Warning: watch(): $x = 15 at watch-unit.sl:4\n"
          "Warning: watch(): $y = 15 at watch-unit.sl:5\n"
          "Warning: watch(): $y = 'text' at watch-unit.sl:6\n");
    CHECK(slp_value_as_number(
              warning_global(vm, "$x")) == 15.0);
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: exit unwinds recursive calls and try handlers") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "exit-unit.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "sub descend {\n"
        "  if ($1 < 0) { exit(\"leave: $1\"); }\n"
        "  return descend($1 - 1);\n"
        "}\n"
        "try { descend(2); }\n"
        "catch $problem { println('caught'); }\n"
        "println('unreachable');\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Warning: leave: -1 at exit-unit.sl:2\n");
    CHECK(vm->frame_count == 0);
    CHECK(vm->try_handler_count == 0);
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: two-argument println requires an I/O handle") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "io-argument.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "println('whatever', 'ignored');\n"
        "println('unreachable');\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Warning: expected I/O handle argument, received: "
          "'whatever' at io-argument.sl:1\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: catch bindings are declarations in strict mode") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "strict-catch.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "debug(7);\n"
        "try { throw 'caught'; }\n"
        "catch $problem { println($problem); }\n");

    CHECK(result == SLP_OK);
    CHECK(output == "caught\n");
    CHECK(slp_value_equals(
        warning_global(vm, "$problem"),
        SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "caught"))));
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: logic tracing describes class-based isa decisions") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "isa-trace.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "debug(64);\n"
        "$string = 'blah' isa ^String;\n"
        "$list = [new LinkedList] isa ^List;\n"
        "$integer = 3 isa ^Integer;\n"
        "$number = 3.0 isa ^Number;\n"
        "$not_integer = 3.0 !isa ^Integer;\n"
        "$invalid = 'blah' isa 'bleh';\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Trace: 'blah' isa class java.lang.String ? TRUE at "
          "isa-trace.sl:2\n"
          "Trace: [] isa interface java.util.List ? TRUE at "
          "isa-trace.sl:3\n"
          "Trace: 3 isa class java.lang.Integer ? TRUE at "
          "isa-trace.sl:4\n"
          "Trace: 3.0 isa class java.lang.Number ? TRUE at "
          "isa-trace.sl:5\n"
          "Trace: 3.0 !isa class java.lang.Integer ? TRUE at "
          "isa-trace.sl:6\n"
          "Warning: attempted an invalid cast: java.lang.String cannot be "
          "cast to java.lang.Class at isa-trace.sl:7\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: assertions trace truth and exit the call chain") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "assert-flow.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "debug(64);\n"
        "sub inner {\n"
        "  assert 0;\n"
        "  println('inside unreachable');\n"
        "}\n"
        "sub outer { inner(); println('outer unreachable'); }\n"
        "outer();\n"
        "println('script unreachable');\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Trace: -istrue 0 ? FALSE at assert-flow.sl:3\n"
          "Warning: assertion failed at assert-flow.sl:3\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: callcc tracing follows continuation control flow") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "callcc-trace.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "debug(8);\n"
        "sub foo {\n"
        "  callcc {\n"
        "    [$1: 'resume'];\n"
        "    return 'replacement';\n"
        "  };\n"
        "  println(\"post $1\");\n"
        "}\n"
        "$result = foo();\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Trace: &foo() -goto- &closure[callcc-trace.sl:4-5]#2 at "
          "callcc-trace.sl:9\n"
          "post resume\n"
          "Trace: &println('post resume') at callcc-trace.sl:7\n"
          "Trace: [&closure[callcc-trace.sl:3-7]#1: 'resume'] at "
          "callcc-trace.sl:4\n"
          "Trace: [&closure[callcc-trace.sl:4-5]#2 CALLCC: "
          "&closure[callcc-trace.sl:3-7]#1] = 'replacement' at "
          "callcc-trace.sl:3\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: tracing suppresses warnings and collection literals") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "trace-filter.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "debug(8);\n"
        "warn('expected warning');\n"
        "println(@('x'));\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Warning: expected warning at trace-filter.sl:2\n"
          "@('x')\n"
          "Trace: &println(@('x')) at trace-filter.sl:3\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: unreachable named parameters fail before the call") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "named-parameter.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "debug(8);\n"
        "sub target { println('unreachable'); }\n"
        "target(action => 'value');\n"
        "println('after');\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Trace: &target(action => 'value') - FAILED! at "
          "named-parameter.sl:3\n"
          "Warning: unreachable named parameter: action at "
          "named-parameter.sl:3\n"
          "after\n"
          "Trace: &println('after') at named-parameter.sl:4\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: strict reads create correctly typed implicit variables") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "implicit.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "debug(7);\n"
        "$scalar_copy = $scalar;\n"
        "$array_size = size(@items);\n"
        "$hash_size = size(%items);\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Warning: variable '$scalar' not declared at implicit.sl:2\n"
          "Warning: variable '$scalar_copy' not declared at implicit.sl:2\n"
          "Warning: variable '@items' not declared at implicit.sl:3\n"
          "Warning: variable '$array_size' not declared at implicit.sl:3\n"
          "Warning: variable '%items' not declared at implicit.sl:4\n"
          "Warning: variable '$hash_size' not declared at implicit.sl:4\n");
    SlpValue array = warning_global(vm, "@items");
    SlpValue hash = warning_global(vm, "%items");
    CHECK(SLP_IS_OBJ(array));
    CHECK(SLP_OBJ_TYPE(array) == SLP_OBJ_ARRAY);
    CHECK(SLP_IS_OBJ(hash));
    CHECK(SLP_OBJ_TYPE(hash) == SLP_OBJ_HASH);
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: popl at the base scope is nonfatal and source-aware") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "popl-warning.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "sub test {\n"
        "  popl();\n"
        "  println('continued');\n"
        "}\n"
        "test();\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Warning: &popl: no more local frames exist at popl-warning.sl:2\n"
          "continued\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: invalid nested index aborts the current frame") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "invalid-index.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "%hash['x'] = 'text';\n"
        "println('before');\n"
        "println('Value: ' . %hash['x']['z']);\n"
        "println('after');\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "before\n"
          "Warning: invalid use of index operator: 'text'['z'] at "
          "invalid-index.sl:3\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: illegal substring aborts only the current frame") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "substring-warning.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "sub fail {\n"
        "  substr('abcdef', 4, 2);\n"
        "  println('inside after');\n"
        "}\n"
        "println('before');\n"
        "fail();\n"
        "println('after');\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "before\n"
          "Warning: &substr: illegal substring('abcdef', 4 -> 4, 2 -> 2) "
          "indices at substring-warning.sl:2\n"
          "after\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: illegal sublist aborts execution successfully") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "sublist-warning.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "@items = @('a', 'b', 'c');\n"
        "sublist(@items, 2, 1);\n"
        "println('after');\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Warning: illegal subarray(@('a', 'b', 'c'), 2 -> 2, 1 -> 1) at "
          "sublist-warning.sl:2\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: nested sublist mutation invalidates its parent view") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "sublist-parent-warning.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "@parent = @('a', 'b', 'c', 'd', 'e');\n"
        "@outer = sublist(@parent, 1, 5);\n"
        "@inner = sublist(@outer, 1, 3);\n"
        "removeAt(@inner, 0);\n"
        "println(@inner);\n"
        "println(@outer);\n"
        "println('after');\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "@('d')\n"
          "Warning: unsafe data modification: parent @array changed after "
          "&sublist creation at sublist-parent-warning.sl:6\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: removeAt rejects an invalid array index") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "removeerr.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "@items = @('a', 'b', 'c');\n"
        "removeAt(@items, 4);\n"
        "println('after');\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Warning: attempted an invalid index: Index: 4, Size: 3 at "
          "removeerr.sl:2\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: filesystem arrays reject direct mutation") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "readonly-array.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "@files = ls();\n"
        "pop(@files);\n"
        "println('unreachable');\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Warning: array is read-only at readonly-array.sl:2\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: map rejects a non-iterator value") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "itererror.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "map({ println('unreachable'); }, '345');\n"
        "println('after');\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Warning: expected iterator (@array or &closure)--received: '345' at "
          "itererror.sl:1\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: unknown unary predicates warn and evaluate false") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "predicate-warning.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "if (-inumber 5) { println('unreachable'); }\n"
        "println('continued');\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Warning: Attempted to use non-existent predicate: -inumber at "
          "predicate-warning.sl:1\n"
          "continued\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: assertions use predicate grammar and lazy messages") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "assertcompare.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "assert 1 == 1 : println('message was eager');\n"
        "assert 2 + 2 : 'eh, assertions are enabled';\n"
        "println('after');\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "Warning: eh, assertions are enabled at assertcompare.sl:2\n");
    slp_vm_free(vm);
}

TEST_CASE("VM warnings: function requires an ampersand-prefixed name") {
    SlpVM *vm = slp_vm_new(&warning_allocator);
    REQUIRE(vm != NULL);
    slp_stdlib_init(vm);
    std::string output;
    slp_vm_set_write_fn(vm, capture_warning_output, &output);
    slp_vm_set_source_name(vm, "functionerr.sl");

    SlpResult result = slp_vm_interpret(
        vm,
        "sub foo { return 'ok'; }\n"
        "println(function('&foo'));\n"
        "function('foo');\n"
        "println('after');\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "&closure[functionerr.sl:1]#1\n"
          "Warning: &function: requested function name must begin with '&' at "
          "functionerr.sl:3\n");
    slp_vm_free(vm);
}
