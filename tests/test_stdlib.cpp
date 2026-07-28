#include "doctest.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

extern "C" {
#include "slp_common.h"
#include "slp_core.h"
#include "slp_platform.h"
#include "slp_stdlib.h"
#include "slp_value.h"
#include "slp_vm.h"
}

namespace {
static void *stdlib_test_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) {
        std::free(ptr);
        return nullptr;
    }
    return std::realloc(ptr, size);
}

static SlpAllocator stdlib_test_allocator = {
    stdlib_test_alloc,
    nullptr
};

static SlpVM *new_stdlib_vm() {
    SlpVM *vm = slp_vm_new(&stdlib_test_allocator);
    if (vm) slp_stdlib_init(vm);
    return vm;
}

static SlpValue get_stdlib_global(SlpVM *vm, const char *name) {
    SlpObjString *key = slp_vm_copy_cstr(vm, name);
    return slp_obj_hash_get(vm->globals, SLP_OBJ_VAL(key));
}

static void capture_stdlib_output(
    void *user_data, const char *text) {
    std::string *output =
        static_cast<std::string *>(user_data);
    if (text) *output += text;
}
}

TEST_CASE("stdlib global initializes declared collection sigils") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm, "global('  @items   %state   $scalar  ');") == SLP_OK);

    SlpValue items = get_stdlib_global(vm, "@items");
    SlpValue state = get_stdlib_global(vm, "%state");
    CHECK(SLP_IS_OBJ(items));
    CHECK(SLP_OBJ_TYPE(items) == SLP_OBJ_ARRAY);
    CHECK(SLP_AS_ARRAY(items)->count == 0);
    CHECK(SLP_IS_OBJ(state));
    CHECK(SLP_OBJ_TYPE(state) == SLP_OBJ_HASH);
    CHECK(SLP_AS_HASH(state)->count == 0);
    CHECK(SLP_IS_NULL(get_stdlib_global(vm, "$scalar")));

    slp_vm_free(vm);
}

TEST_CASE("stdlib registers every named function in the Sleep 2.1 manual") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    std::ifstream input(
        "tests/manual_reference_functions.txt");
    REQUIRE(input.good());
    std::string name;
    int checked = 0;
    while (std::getline(input, name)) {
        if (name.empty() || name[0] == '#')
            continue;
        CAPTURE(name);
        SlpValue function =
            get_stdlib_global(
                vm, name.c_str());
        REQUIRE(SLP_IS_OBJ(function));
        CHECK(SLP_OBJ_TYPE(function) ==
              SLP_OBJ_NATIVE);
        checked++;
    }
    CHECK(checked == 168);

    slp_vm_free(vm);
}

TEST_CASE("stdlib not preserves integer width") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$integer = not(15);"
        "$long = not(15L);") == SLP_OK);

    CHECK(slp_value_as_number(
              get_stdlib_global(vm, "$integer")) == -16.0);
    SlpValue long_value = get_stdlib_global(vm, "$long");
    REQUIRE(SLP_IS_OBJ(long_value));
    REQUIRE(SLP_OBJ_TYPE(long_value) == SLP_OBJ_LONG);
    CHECK(SLP_AS_LONG(long_value)->value == -16);
    slp_vm_free(vm);
}

TEST_CASE("stdlib typeOf returns Sleep implementation class objects") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$string_type = typeOf('value'); "
        "$int_type = typeOf(42); "
        "$long_type = typeOf(42L); "
        "$double_type = typeOf(42.0); "
        "$empty_type = typeOf($null); "
        "$array_type = typeOf(@()); "
        "$hash_type = typeOf(%()); "
        "$closure_type = typeOf({ return 1; }); "
        "$class_type = typeOf(^String); "
        "$resolved = ^String; "
        "$qualified = ^java.lang.String;") == SLP_OK);

    struct ExpectedType {
        const char *variable;
        const char *name;
    };
    const ExpectedType expected[] = {
        {"$string_type", "sleep.engine.types.StringValue"},
        {"$int_type", "sleep.engine.types.IntValue"},
        {"$long_type", "sleep.engine.types.LongValue"},
        {"$double_type", "sleep.engine.types.DoubleValue"},
        {"$empty_type", "sleep.engine.types.NullValue"},
        {"$array_type", "sleep.engine.types.ListContainer"},
        {"$hash_type", "sleep.engine.types.HashContainer"},
        {"$closure_type", "sleep.engine.types.ObjectValue"},
        {"$class_type", "sleep.engine.types.ObjectValue"}
    };
    for (const ExpectedType &item : expected) {
        SlpValue value = get_stdlib_global(vm, item.variable);
        REQUIRE(SLP_IS_OBJ(value));
        REQUIRE(SLP_OBJ_TYPE(value) == SLP_OBJ_CLASS);
        CHECK(std::strcmp(
                  SLP_AS_CLASS(value)->name->chars,
                  item.name) == 0);
    }

    SlpValue resolved = get_stdlib_global(vm, "$resolved");
    SlpValue qualified = get_stdlib_global(vm, "$qualified");
    REQUIRE(SLP_IS_OBJ(resolved));
    REQUIRE(SLP_OBJ_TYPE(resolved) == SLP_OBJ_CLASS);
    CHECK(std::strcmp(
              SLP_AS_CLASS(resolved)->name->chars,
              "java.lang.String") == 0);
    CHECK(slp_value_identity_equals(resolved, qualified));

    SlpObjString *description = slp_vm_stringify(vm, resolved);
    REQUIRE(description != nullptr);
    CHECK(std::strcmp(
              description->chars, "class java.lang.String") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib memory buffers round-trip independent object snapshots") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "@original = @('a', @('b'));"
        "%original = %(answer => 42);"
        "$function = { return 7; };"
        "$buffer = allocate();"
        "writeObject($buffer, @original, %original, $function);"
        "push(@original, 'changed');"
        "%original['answer'] = 0;"
        "closef($buffer);"
        "@copy = readObject($buffer);"
        "%copy = readObject($buffer);"
        "$copy_function = readObject($buffer);"
        "$copy_result = [$copy_function];") == SLP_OK);

    SlpObjArray *array = SLP_AS_ARRAY(get_stdlib_global(vm, "@copy"));
    REQUIRE(array->count == 2);
    CHECK(std::strcmp(SLP_AS_STRING(array->elements[0])->chars, "a") == 0);
    SlpObjArray *nested = SLP_AS_ARRAY(array->elements[1]);
    REQUIRE(nested->count == 1);
    CHECK(std::strcmp(SLP_AS_STRING(nested->elements[0])->chars, "b") == 0);

    SlpObjHash *hash = SLP_AS_HASH(get_stdlib_global(vm, "%copy"));
    CHECK(slp_value_as_number(slp_vm_hash_get(
              vm, hash, SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "answer")))) ==
          42.0);
    CHECK(slp_value_as_number(
              get_stdlib_global(vm, "$copy_result")) == 7.0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib object pack fields preserve snapshots and closures") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);
    std::string output;
    slp_vm_set_write_fn(
        vm, capture_stdlib_output, &output);

    REQUIRE(slp_vm_interpret(
        vm,
        "@original = @('a');"
        "$function = {"
        "  println('called');"
        "  return 9;"
        "};"
        "$packed = pack("
        "  'o3', @original, 3, $function);"
        "push(@original, 'changed');"
        "$function = $null;"
        "@original = @();") == SLP_OK);

    slp_gc_collect(vm);

    REQUIRE(slp_vm_interpret(
        vm,
        "(@copy, $number, $copy_function) ="
        "  unpack('o*', $packed);"
        "$copy_result = [$copy_function];"
        "@invalid = unpack('o', 'bad');") ==
        SLP_OK);

    SlpObjArray *copy =
        SLP_AS_ARRAY(
            get_stdlib_global(vm, "@copy"));
    REQUIRE(copy->count == 1);
    CHECK(std::strcmp(
              SLP_AS_STRING(
                  copy->elements[0])->chars,
              "a") == 0);
    CHECK(slp_value_as_number(
              get_stdlib_global(
                  vm, "$number")) == 3.0);
    CHECK(slp_value_as_number(
              get_stdlib_global(
                  vm, "$copy_result")) == 9.0);
    CHECK(
        SLP_AS_ARRAY(
            get_stdlib_global(
                vm, "@invalid"))->count == 0);
    CHECK(output == "called\n");

    slp_vm_free(vm);
}

TEST_CASE("stdlib readAsObject preserves the serialized Scalar wrapper") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$buffer = allocate();"
        "writeAsObject($buffer, 'value');"
        "closef($buffer);"
        "$wrapped = readAsObject($buffer);"
        "$class = [$wrapped getClass];"
        "$scalar = scalar($wrapped);") ==
        SLP_OK);

    SlpValue wrapped =
        get_stdlib_global(vm, "$wrapped");
    REQUIRE(SLP_IS_OBJ(wrapped));
    REQUIRE(SLP_OBJ_TYPE(wrapped) ==
            SLP_OBJ_JAVA_OBJECT);
    CHECK(std::strcmp(
              SLP_AS_JAVA_OBJECT(wrapped)
                  ->class_object->name->chars,
              "sleep.runtime.Scalar") == 0);
    CHECK(std::strcmp(
              SLP_AS_CLASS(
                  get_stdlib_global(
                      vm, "$class"))
                  ->name->chars,
              "sleep.runtime.Scalar") == 0);
    CHECK(std::strcmp(
              SLP_AS_STRING(
                  get_stdlib_global(
                      vm, "$scalar"))
                  ->chars,
              "value") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib memory buffers support binary and line IO after close") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$buffer = allocate(32);"
        "println($buffer, 'one');"
        "writeb($buffer, \"two\\n\");"
        "$before_close = available($buffer);"
        "closef($buffer);"
        "$after_close = available($buffer);"
        "mark($buffer);"
        "$first = readb($buffer, 1);"
        "reset($buffer);"
        "@lines = readAll($buffer);") == SLP_OK);

    CHECK(SLP_IS_NULL(get_stdlib_global(vm, "$before_close")));
    CHECK(slp_value_as_number(
              get_stdlib_global(vm, "$after_close")) == 8.0);
    CHECK(std::strcmp(
        SLP_AS_STRING(get_stdlib_global(vm, "$first"))->chars, "o") == 0);
    SlpObjArray *lines =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@lines"));
    REQUIRE(lines->count == 2);
    CHECK(std::strcmp(SLP_AS_STRING(lines->elements[0])->chars, "one") == 0);
    CHECK(std::strcmp(SLP_AS_STRING(lines->elements[1])->chars, "two") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib serialization snapshots suspended coroutine state") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "sub generator {"
        "  local('$state');"
        "  $state = 'first';"
        "  yield $state;"
        "  $state = 'second';"
        "  yield $state;"
        "  return 'done';"
        "}"
        "$first = generator();"
        "$buffer = allocate();"
        "writeObject($buffer, &generator);"
        "closef($buffer);"
        "$original_second = generator();"
        "$copy = readObject($buffer);"
        "$copy_second = [$copy];"
        "$original_done = generator();"
        "$copy_done = [$copy];") == SLP_OK);

    CHECK(std::strcmp(
        SLP_AS_STRING(get_stdlib_global(vm, "$first"))->chars,
        "first") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(get_stdlib_global(vm, "$original_second"))->chars,
        "second") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(get_stdlib_global(vm, "$copy_second"))->chars,
        "second") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(get_stdlib_global(vm, "$original_done"))->chars,
        "done") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(get_stdlib_global(vm, "$copy_done"))->chars,
        "done") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib hashes use Sleep ordering and add accepts key-value pairs") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);
    slp_vm_set_source_name(vm, "hash_order.sl");

    REQUIRE(slp_vm_interpret(
        vm,
        "%hash = %(a => 'apple', b => 'boy', c => 'chump');"
        "add(%hash, d => 'dog', p => 'pH34r',"
        "     j => 'jumping jack flash', f => { return 1; });") == SLP_OK);

    SlpObjString *text =
        slp_vm_stringify(vm, get_stdlib_global(vm, "%hash"));
    REQUIRE(text != nullptr);
    CHECK(std::strcmp(
        text->chars,
        "%(f => &closure[hash_order.sl:1]#1, d => 'dog', b => 'boy', "
        "c => 'chump', p => 'pH34r', a => 'apple', "
        "j => 'jumping jack flash')") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib hash accepts key-value objects and legacy key=value strings") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "%hash = hash('key=hello=world', blah => 'bleh',"
        "             count => 3);") == SLP_OK);

    SlpValue hash_value = get_stdlib_global(vm, "%hash");
    REQUIRE(SLP_IS_OBJ(hash_value));
    REQUIRE(SLP_OBJ_TYPE(hash_value) == SLP_OBJ_HASH);
    SlpObjHash *hash = SLP_AS_HASH(hash_value);
    CHECK(hash->count == 3);

    SlpValue key = slp_obj_hash_get(
        hash, SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "key")));
    REQUIRE(SLP_IS_OBJ(key));
    REQUIRE(SLP_OBJ_TYPE(key) == SLP_OBJ_STRING);
    CHECK(std::strcmp(SLP_AS_STRING(key)->chars, "hello=world") == 0);

    SlpValue blah = slp_obj_hash_get(
        hash, SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "blah")));
    REQUIRE(SLP_IS_OBJ(blah));
    REQUIRE(SLP_OBJ_TYPE(blah) == SLP_OBJ_STRING);
    CHECK(std::strcmp(SLP_AS_STRING(blah)->chars, "bleh") == 0);
    CHECK(slp_value_as_number(slp_obj_hash_get(
              hash, SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "count")))) == 3.0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib removeAt removes array indexes and hash keys") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "@items = @('a', 'b', 'c', 'd');"
        "$array_result = removeAt(@items, 1, -1);"
        "%state = hash(a => 1, b => 2, c => 3);"
        "$hash_result = removeAt(%state, 'b', 'c');") == SLP_OK);

    SlpValue items_value = get_stdlib_global(vm, "@items");
    REQUIRE(SLP_IS_OBJ(items_value));
    REQUIRE(SLP_OBJ_TYPE(items_value) == SLP_OBJ_ARRAY);
    SlpObjArray *items = SLP_AS_ARRAY(items_value);
    REQUIRE(items->count == 2);
    CHECK(std::strcmp(SLP_AS_STRING(slp_obj_array_get(items, 0))->chars,
                      "a") == 0);
    CHECK(std::strcmp(SLP_AS_STRING(slp_obj_array_get(items, 1))->chars,
                      "c") == 0);

    SlpValue state_value = get_stdlib_global(vm, "%state");
    REQUIRE(SLP_IS_OBJ(state_value));
    REQUIRE(SLP_OBJ_TYPE(state_value) == SLP_OBJ_HASH);
    SlpObjHash *state = SLP_AS_HASH(state_value);
    CHECK(state->count == 1);
    CHECK(slp_value_as_number(slp_obj_hash_get(
              state, SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "a")))) == 1.0);
    CHECK(SLP_IS_NULL(get_stdlib_global(vm, "$array_result")));
    CHECK(SLP_IS_NULL(get_stdlib_global(vm, "$hash_result")));

    slp_vm_free(vm);
}

TEST_CASE("stdlib hash index assignment of null removes the mapping") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "%state = %(a => 'apple', b => 'bat', c => 'cat');"
        "%state['b'] = $null;") == SLP_OK);

    SlpValue state_value = get_stdlib_global(vm, "%state");
    REQUIRE(SLP_IS_OBJ(state_value));
    REQUIRE(SLP_OBJ_TYPE(state_value) == SLP_OBJ_HASH);
    SlpObjHash *state = SLP_AS_HASH(state_value);
    CHECK(state->count == 2);
    CHECK_FALSE(slp_obj_hash_contains(
        state, SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "b"))));
    slp_vm_free(vm);
}

TEST_CASE("stdlib iff selects lazily and supports omitted branches") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$side = 0;"
        "$plural = iff(10 > 1, 's');"
        "$true_default = iff(1 == 1);"
        "$false_default = iff(1 != 1);"
        "$selected = iff(1 == 1, 'yes', $side++);") == SLP_OK);

    CHECK(std::strcmp(
        SLP_AS_STRING(get_stdlib_global(vm, "$plural"))->chars,
        "s") == 0);
    CHECK(slp_value_as_number(
        get_stdlib_global(vm, "$true_default")) == 1.0);
    CHECK(SLP_IS_NULL(get_stdlib_global(vm, "$false_default")));
    CHECK(std::strcmp(
        SLP_AS_STRING(get_stdlib_global(vm, "$selected"))->chars,
        "yes") == 0);
    CHECK(slp_value_as_number(get_stdlib_global(vm, "$side")) == 0.0);
    slp_vm_free(vm);
}

TEST_CASE("stdlib putAll appends a snapshot of array values") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "@destination = @('a', 'b', 'c');"
        "@source = @(1, 2, 3);"
        "putAll(@destination, @source);"
        "@source[1] = 'changed';") == SLP_OK);

    SlpObjArray *destination =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@destination"));
    REQUIRE(destination->count == 6);
    CHECK(slp_value_as_number(destination->elements[3]) == 1.0);
    CHECK(slp_value_as_number(destination->elements[4]) == 2.0);
    CHECK(slp_value_as_number(destination->elements[5]) == 3.0);
    slp_vm_free(vm);
}

TEST_CASE("stdlib putAll pairs one iterator and advances value closures lazily") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "putAll(%state, @('a', 1, 'b', 2));"
        "putAll(%state, @('x', 'y'), { return 7; });"
        "putAll(%state, {"
        "    this('$i');"
        "    for ($i = 0; $i < 4; $i++) { yield $i; }"
        "});"
        "putAll(%state, @('a', 'b'), { return $null; });") ==
        SLP_OK);

    SlpObjHash *state =
        SLP_AS_HASH(get_stdlib_global(vm, "%state"));
    CHECK_FALSE(slp_vm_hash_contains(
        vm, state, SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "a"))));
    CHECK_FALSE(slp_vm_hash_contains(
        vm, state, SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "b"))));
    CHECK(slp_value_as_number(slp_vm_hash_get(
              vm, state,
              SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "x")))) == 7.0);
    CHECK(slp_value_as_number(slp_vm_hash_get(
              vm, state,
              SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "y")))) == 7.0);
    CHECK(slp_value_as_number(slp_vm_hash_get(
              vm, state, SLP_NUM_VAL(0))) == 1.0);
    CHECK(slp_value_as_number(slp_vm_hash_get(
              vm, state, SLP_NUM_VAL(2))) == 3.0);
    slp_vm_free(vm);
}

TEST_CASE("stdlib ordered hashes preserve order and run miss policies") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "%cache = ohasha(first => 1, second => 2);"
        "setMissPolicy(%cache, { return uc($2); });"
        "$first = %cache['first'];"
        "$missing = %cache['third'];"
        "@ordered = values(%cache);"
        "@requested = values(%cache, @('fourth', 'fifth'));"
        "%cache['count'] += 4;") ==
        SLP_OK);

    CHECK(std::strcmp(
        SLP_AS_STRING(get_stdlib_global(vm, "$missing"))->chars,
        "THIRD") == 0);
    CHECK(slp_value_as_number(slp_obj_hash_get(
              SLP_AS_HASH(get_stdlib_global(vm, "%cache")),
              SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "count")))) == 4.0);
    SlpObjArray *ordered =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@ordered"));
    REQUIRE(ordered->count == 3);
    CHECK(slp_value_as_number(ordered->elements[0]) == 2.0);
    CHECK(slp_value_as_number(ordered->elements[1]) == 1.0);
    CHECK(std::strcmp(
        SLP_AS_STRING(ordered->elements[2])->chars, "THIRD") == 0);
    SlpObjArray *requested =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@requested"));
    REQUIRE(requested->count == 2);
    CHECK(std::strcmp(
        SLP_AS_STRING(requested->elements[0])->chars, "FOURTH") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(requested->elements[1])->chars, "FIFTH") == 0);
    slp_vm_free(vm);
}

TEST_CASE("stdlib hashes canonicalize collection keys like Sleep") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "%cache = ohash();"
        "$misses = 0;"
        "setMissPolicy(%cache, {"
        "    $misses++;"
        "    return $2[0] * 10;"
        "});"
        "$first = %cache[@(3L)];"
        "$second = %cache[@(3L)];"
        "$text = %cache['@(3L)'];") == SLP_OK);

    CHECK(slp_value_as_number(
              get_stdlib_global(vm, "$first")) == 30.0);
    CHECK(slp_value_as_number(
              get_stdlib_global(vm, "$second")) == 30.0);
    CHECK(slp_value_as_number(
              get_stdlib_global(vm, "$text")) == 30.0);
    CHECK(slp_value_as_number(
              get_stdlib_global(vm, "$misses")) == 1.0);
    CHECK(SLP_AS_HASH(
              get_stdlib_global(vm, "%cache"))->count == 1);
    slp_vm_free(vm);
}

TEST_CASE("stdlib ordered hashes apply removal policy and foreach removal") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "%cache = ohash();"
        "setRemovalPolicy(%cache, { return iff(size($1) >= 4); });"
        "add(%cache, a => 'apple', b => 'boy', c => 'cat');"
        "add(%cache, d => 'dog');"
        "@before = keys(%cache);"
        "add(%cache, e => 'emu');"
        "@after = keys(%cache);"
        "%iterated = ohasha(a => 'apple', b => 'bat', c => 'cat', d => 'dog');"
        "$touch = %iterated['c'];"
        "foreach $key => $value (%iterated) {"
        "  if ($key eq 'b') { remove(); }"
        "}"
        "@remaining = keys(%iterated);") == SLP_OK);

    SlpObjArray *before =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@before"));
    REQUIRE(before->count == 4);
    CHECK(std::strcmp(
        SLP_AS_STRING(before->elements[0])->chars, "a") == 0);
    SlpObjArray *after =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@after"));
    REQUIRE(after->count == 4);
    CHECK(std::strcmp(
        SLP_AS_STRING(after->elements[0])->chars, "b") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(after->elements[3])->chars, "e") == 0);
    SlpObjArray *remaining =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@remaining"));
    REQUIRE(remaining->count == 3);
    CHECK(std::strcmp(
        SLP_AS_STRING(remaining->elements[0])->chars, "a") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(remaining->elements[1])->chars, "d") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(remaining->elements[2])->chars, "c") == 0);
    slp_vm_free(vm);
}

TEST_CASE("stdlib setf replaces a named function and long keeps exact bits") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "sub original { return 'old'; }"
        "$replacement = { return 'new'; };"
        "setf('&original', $replacement);"
        "$result = original();"
        "$wide = long(2880067194370816120L);") == SLP_OK);

    CHECK(std::strcmp(
        SLP_AS_STRING(get_stdlib_global(vm, "$result"))->chars,
        "new") == 0);
    SlpValue wide = get_stdlib_global(vm, "$wide");
    REQUIRE(SLP_IS_OBJ(wide));
    REQUIRE(SLP_OBJ_TYPE(wide) == SLP_OBJ_LONG);
    CHECK(SLP_AS_LONG(wide)->value == 2880067194370816120LL);
    slp_vm_free(vm);
}

TEST_CASE("stdlib lambda snapshots bound scalar containers") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "global('$result');"
        "$x = 4;"
        "$closure = lambda({ $result = $x; }, $x => $x);"
        "$x = 5;"
        "[$closure];") == SLP_OK);

    CHECK(slp_value_as_number(
              get_stdlib_global(vm, "$result")) == 4.0);
    slp_vm_free(vm);
}

TEST_CASE("stdlib global supports computed persistent state across calls") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    const char *script =
        "global('%state', '@events');"
        "sub save_state {"
        "    %state[$1 . '.rt'] = $2;"
        "    %state[$1 . '.region'] = $3;"
        "    push(@events, $1);"
        "}"
        "save_state('beacon_0', 'baka', 'aho');";
    REQUIRE(slp_vm_interpret(vm, script) == SLP_OK);

    SlpValue state_value = get_stdlib_global(vm, "%state");
    REQUIRE(SLP_IS_OBJ(state_value));
    REQUIRE(SLP_OBJ_TYPE(state_value) == SLP_OBJ_HASH);
    SlpObjHash *state = SLP_AS_HASH(state_value);

    SlpValue refresh = slp_obj_hash_get(
        state,
        SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "beacon_0.rt")));
    SlpValue region = slp_obj_hash_get(
        state,
        SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "beacon_0.region")));
    REQUIRE(SLP_IS_OBJ(refresh));
    REQUIRE(SLP_OBJ_TYPE(refresh) == SLP_OBJ_STRING);
    REQUIRE(SLP_IS_OBJ(region));
    REQUIRE(SLP_OBJ_TYPE(region) == SLP_OBJ_STRING);
    CHECK(std::strcmp(SLP_AS_STRING(refresh)->chars, "baka") == 0);
    CHECK(std::strcmp(SLP_AS_STRING(region)->chars, "aho") == 0);

    SlpValue events_value = get_stdlib_global(vm, "@events");
    REQUIRE(SLP_IS_OBJ(events_value));
    REQUIRE(SLP_OBJ_TYPE(events_value) == SLP_OBJ_ARRAY);
    SlpObjArray *events = SLP_AS_ARRAY(events_value);
    REQUIRE(events->count == 1);
    REQUIRE(SLP_IS_OBJ(events->elements[0]));
    CHECK(std::strcmp(
        SLP_AS_STRING(events->elements[0])->chars, "beacon_0") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib global redeclaration preserves values and ignores invalid arguments") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    const char *script =
        "global('@items %state');"
        "push(@items, 'first');"
        "%state['refresh_token'] = 'baka';"
        "$scalar = 'preserved';"
        "global('@items %state $scalar');"
        "global(42, 'not-a-variable', '@extra');";
    REQUIRE(slp_vm_interpret(vm, script) == SLP_OK);

    SlpValue items_value = get_stdlib_global(vm, "@items");
    REQUIRE(SLP_IS_OBJ(items_value));
    REQUIRE(SLP_OBJ_TYPE(items_value) == SLP_OBJ_ARRAY);
    SlpObjArray *items = SLP_AS_ARRAY(items_value);
    REQUIRE(items->count == 1);
    REQUIRE(SLP_IS_OBJ(items->elements[0]));
    CHECK(std::strcmp(
        SLP_AS_STRING(items->elements[0])->chars, "first") == 0);

    SlpValue state_value = get_stdlib_global(vm, "%state");
    REQUIRE(SLP_IS_OBJ(state_value));
    REQUIRE(SLP_OBJ_TYPE(state_value) == SLP_OBJ_HASH);
    SlpValue refresh = slp_obj_hash_get(
        SLP_AS_HASH(state_value),
        SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "refresh_token")));
    REQUIRE(SLP_IS_OBJ(refresh));
    CHECK(std::strcmp(SLP_AS_STRING(refresh)->chars, "baka") == 0);

    SlpValue scalar = get_stdlib_global(vm, "$scalar");
    REQUIRE(SLP_IS_OBJ(scalar));
    CHECK(std::strcmp(SLP_AS_STRING(scalar)->chars, "preserved") == 0);
    CHECK(SLP_IS_NULL(get_stdlib_global(vm, "not-a-variable")));

    SlpValue extra = get_stdlib_global(vm, "@extra");
    REQUIRE(SLP_IS_OBJ(extra));
    CHECK(SLP_OBJ_TYPE(extra) == SLP_OBJ_ARRAY);
    CHECK(SLP_AS_ARRAY(extra)->count == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib sorters use distinct Sleep string, integer, and double order") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    const char *script =
        "@ascii = sorta(@('c', 'A', 'b'));"
        "@integers = sortn(@('10', '2', '-1'));"
        "@doubles = sortd(@(1.5, -2, 1.05));";
    REQUIRE(slp_vm_interpret(vm, script) == SLP_OK);

    SlpObjArray *ascii = SLP_AS_ARRAY(get_stdlib_global(vm, "@ascii"));
    SlpObjArray *integers =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@integers"));
    SlpObjArray *doubles =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@doubles"));
    REQUIRE(ascii->count == 3);
    REQUIRE(integers->count == 3);
    REQUIRE(doubles->count == 3);
    CHECK(std::strcmp(SLP_AS_STRING(ascii->elements[0])->chars, "A") == 0);
    CHECK(std::strcmp(SLP_AS_STRING(ascii->elements[1])->chars, "b") == 0);
    CHECK(std::strcmp(SLP_AS_STRING(ascii->elements[2])->chars, "c") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(integers->elements[0])->chars, "-1") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(integers->elements[1])->chars, "2") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(integers->elements[2])->chars, "10") == 0);
    CHECK(slp_value_as_number(doubles->elements[0]) ==
          doctest::Approx(-2.0));
    CHECK(slp_value_as_number(doubles->elements[1]) ==
          doctest::Approx(1.05));
    CHECK(slp_value_as_number(doubles->elements[2]) ==
          doctest::Approx(1.5));

    slp_vm_free(vm);
}

TEST_CASE("stdlib collection mutation and bounded split match Sleep semantics") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    const char *script =
        "@parts = split(' ', 'one two three four', 3);"
        "@base = @(1, 2, 3, 4);"
        "@spliced = splice(@base, @('x', 'y'), 1, 2);"
        "add(@base, 'front', 0);"
        "add(@base, 'back', -1);"
        "@union = @(1, 2);"
        "addAll(@union, @(2, 3, 3));"
        "$last = @base[-1];"
        "$rounded = round(4.56734, 2);";
    REQUIRE(slp_vm_interpret(vm, script) == SLP_OK);

    SlpObjArray *parts = SLP_AS_ARRAY(get_stdlib_global(vm, "@parts"));
    REQUIRE(parts->count == 3);
    CHECK(std::strcmp(
        SLP_AS_STRING(parts->elements[2])->chars, "three four") == 0);

    SlpValue base_value = get_stdlib_global(vm, "@base");
    SlpValue spliced_value = get_stdlib_global(vm, "@spliced");
    CHECK(SLP_AS_ARRAY(base_value) == SLP_AS_ARRAY(spliced_value));
    SlpObjArray *base = SLP_AS_ARRAY(base_value);
    REQUIRE(base->count == 6);
    CHECK(std::strcmp(
        SLP_AS_STRING(base->elements[0])->chars, "front") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(base->elements[2])->chars, "x") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(base->elements[3])->chars, "y") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(base->elements[5])->chars, "back") == 0);

    SlpObjArray *set_union =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@union"));
    REQUIRE(set_union->count == 3);
    CHECK(SLP_AS_NUM(set_union->elements[0]) == 1.0);
    CHECK(SLP_AS_NUM(set_union->elements[1]) == 2.0);
    CHECK(SLP_AS_NUM(set_union->elements[2]) == 3.0);
    CHECK(std::strcmp(
        SLP_AS_STRING(get_stdlib_global(vm, "$last"))->chars,
        "back") == 0);
    CHECK(SLP_AS_NUM(get_stdlib_global(vm, "$rounded")) ==
          doctest::Approx(4.57));

    slp_vm_free(vm);
}

TEST_CASE("stdlib reports invalid output handles and array insertion indexes") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);
    std::string output;
    slp_vm_set_write_fn(
        vm, capture_stdlib_output, &output);
    slp_vm_set_source_name(
        vm, "index-test.sl");

    REQUIRE(slp_vm_interpret(
        vm,
        "eval('println($null, \"value\");');"
        "@items = @('a', 'b', 'c', 'd');"
        "add(@items, 'invalid', -10);"
        "println('unreachable');") ==
        SLP_OK);

    CHECK(
        output ==
        "Warning: expected I/O handle argument, received: "
        "$null at eval:1\n"
        "Warning: attempted an invalid index: Index: -5, "
        "Size: 4 at index-test.sl:1\n");
    SlpObjArray *items =
        SLP_AS_ARRAY(
            get_stdlib_global(vm, "@items"));
    REQUIRE(items->count == 4);

    slp_vm_free(vm);
}

TEST_CASE("stdlib functional helpers transform iterator values like Sleep") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$generator = { yield 1; yield 2; };"
        "@mapped = map({ return $1 * 10; }, $generator);"
        "@filtered = filter({"
        "    if ($1 == 2) { return 'two'; }"
        "    return $null;"
        "}, @(1, 2, 3));"
        "$reduced = reduce({ return $1 + $2; }, @(1, 2, 3));") == SLP_OK);

    SlpObjArray *mapped =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@mapped"));
    SlpObjArray *filtered =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@filtered"));
    REQUIRE(mapped->count == 2);
    CHECK(slp_value_as_number(mapped->elements[0]) == 10.0);
    CHECK(slp_value_as_number(mapped->elements[1]) == 20.0);
    REQUIRE(filtered->count == 1);
    REQUIRE(SLP_IS_OBJ(filtered->elements[0]));
    CHECK(std::strcmp(
        SLP_AS_STRING(filtered->elements[0])->chars, "two") == 0);
    CHECK(slp_value_as_number(get_stdlib_global(vm, "$reduced")) == 6.0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib Java list iterators work with functional and foreach APIs") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$list = [new LinkedList];"
        "[$list add: 1];"
        "[$list add: 3];"
        "[$list add: 9];"
        "[$list add: 12];"
        "@mapped = map({ return $1 * 3; }, [$list iterator]);"
        "foreach $index => $value ([$list iterator]) {"
        "  if ($index >= 2) { remove(); }"
        "}"
        "$manual_list = [new LinkedList];"
        "[$manual_list add: 'a'];"
        "[$manual_list add: 'b'];"
        "$iterator = [$manual_list iterator];"
        "$has_first = [$iterator hasNext];"
        "$first = [$iterator next];"
        "[$iterator remove];"
        "$has_second = [$iterator hasNext];"
        "$second = [$iterator next];"
        "$done = [$iterator hasNext];") ==
        SLP_OK);

    SlpObjArray *mapped =
        SLP_AS_ARRAY(
            get_stdlib_global(vm, "@mapped"));
    REQUIRE(mapped->count == 4);
    CHECK(slp_value_as_number(
              mapped->elements[0]) == 3.0);
    CHECK(slp_value_as_number(
              mapped->elements[3]) == 36.0);

    SlpObjJavaObject *list =
        SLP_AS_JAVA_OBJECT(
            get_stdlib_global(vm, "$list"));
    REQUIRE(list->kind == SLP_JAVA_LIST);
    REQUIRE(list->list->count == 2);
    CHECK(slp_value_as_number(
              list->list->elements[0]) == 1.0);
    CHECK(slp_value_as_number(
              list->list->elements[1]) == 3.0);

    SlpValue iterator =
        get_stdlib_global(vm, "$iterator");
    REQUIRE(SLP_IS_OBJ(iterator));
    REQUIRE(SLP_OBJ_TYPE(iterator) ==
            SLP_OBJ_JAVA_OBJECT);
    CHECK(SLP_AS_JAVA_OBJECT(iterator)->kind ==
          SLP_JAVA_ITERATOR);
    CHECK(slp_value_as_number(
              get_stdlib_global(
                  vm, "$has_first")) == 1.0);
    CHECK(std::strcmp(
              SLP_AS_STRING(
                  get_stdlib_global(
                      vm, "$first"))->chars,
              "a") == 0);
    CHECK(slp_value_as_number(
              get_stdlib_global(
                  vm, "$has_second")) == 1.0);
    CHECK(std::strcmp(
              SLP_AS_STRING(
                  get_stdlib_global(
                      vm, "$second"))->chars,
              "b") == 0);
    CHECK(SLP_IS_NULL(
        get_stdlib_global(vm, "$done")));

    slp_vm_free(vm);
}

TEST_CASE("stdlib getStackTrace captures calls unwound by an exception") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);
    slp_vm_set_source_name(vm, "trace.sl");

    REQUIRE(slp_vm_interpret(
        vm,
        "sub inner { throw 'boom'; }"
        "sub outer {"
        "  try { inner(); }"
        "  catch $error { @trace = getStackTrace(); }"
        "}"
        "outer();") == SLP_OK);

    SlpObjArray *trace =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@trace"));
    REQUIRE(trace->count == 2);
    CHECK(std::strcmp(
        SLP_AS_STRING(trace->elements[0])->chars,
        "   trace.sl:1 &inner()") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(trace->elements[1])->chars,
        "   trace.sl:1 <origin of exception>") == 0);
    slp_vm_free(vm);
}

TEST_CASE("stdlib find performs regex search from an offset and sets captures") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$first = find('one   two', '(\\\\s+)', 0);"
        "@captures = matched();"
        "$missing = find('one two', '(\\\\d+)', $first + 1);") == SLP_OK);

    CHECK(slp_value_as_number(get_stdlib_global(vm, "$first")) == 3.0);
    SlpObjArray *captures =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@captures"));
    REQUIRE(captures->count == 1);
    REQUIRE(SLP_IS_OBJ(captures->elements[0]));
    CHECK(std::strcmp(
        SLP_AS_STRING(captures->elements[0])->chars, "   ") == 0);
    CHECK(SLP_IS_NULL(get_stdlib_global(vm, "$missing")));

    slp_vm_free(vm);
}

TEST_CASE("stdlib matches returns capture groups for selected matches") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "@all = matches('x12 y345 z6', '([a-z])([0-9]+)');"
        "@second = matches('x12 y345 z6', '([a-z])([0-9]+)', 1);"
        "@range = matches('x12 y345 z6', '([a-z])([0-9]+)', 1, 2);"
        "@nested = matches('abc.12', '(\\\\w++[[\\\\.]\\\\w++]*)');") ==
        SLP_OK);

    SlpObjArray *all =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@all"));
    SlpObjArray *second =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@second"));
    SlpObjArray *range =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@range"));
    SlpObjArray *nested =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@nested"));
    REQUIRE(all->count == 6);
    REQUIRE(second->count == 2);
    REQUIRE(range->count == 4);
    CHECK(std::strcmp(SLP_AS_STRING(all->elements[0])->chars, "x") == 0);
    CHECK(std::strcmp(SLP_AS_STRING(all->elements[1])->chars, "12") == 0);
    CHECK(std::strcmp(SLP_AS_STRING(all->elements[4])->chars, "z") == 0);
    CHECK(std::strcmp(SLP_AS_STRING(all->elements[5])->chars, "6") == 0);
    CHECK(std::strcmp(SLP_AS_STRING(second->elements[0])->chars, "y") == 0);
    CHECK(std::strcmp(SLP_AS_STRING(second->elements[1])->chars, "345") == 0);
    CHECK(std::strcmp(SLP_AS_STRING(range->elements[0])->chars, "y") == 0);
    CHECK(std::strcmp(SLP_AS_STRING(range->elements[3])->chars, "6") == 0);
    REQUIRE(nested->count == 1);
    CHECK(std::strcmp(
        SLP_AS_STRING(nested->elements[0])->chars, "abc.12") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib scalar unwraps portable Java wrappers and primitive arrays") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$wrapped = [new Double: 3.4567];"
        "$object_math = 3 + $wrapped;"
        "$scalar_math = 3 + scalar($wrapped);"
        "$bytes = cast('payload', 'b');"
        "$text = scalar($bytes);"
        "$byte_class = [$bytes getClass];"
        "$text_class = [$text getClass];") == SLP_OK);

    CHECK(slp_value_as_number(
        get_stdlib_global(vm, "$object_math")) == 3.0);
    CHECK(slp_value_as_number(
        get_stdlib_global(vm, "$scalar_math")) ==
        doctest::Approx(6.4567));
    CHECK(std::strcmp(
        SLP_AS_STRING(
            get_stdlib_global(vm, "$text"))->chars,
        "payload") == 0);

    SlpObjClass *byte_class = SLP_AS_CLASS(
        get_stdlib_global(vm, "$byte_class"));
    SlpObjClass *text_class = SLP_AS_CLASS(
        get_stdlib_global(vm, "$text_class"));
    CHECK(std::strcmp(
        byte_class->name->chars, "[B") == 0);
    CHECK(std::strcmp(
        text_class->name->chars,
        "java.lang.String") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib casti preserves primitive wrapper identity and values") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$true = casti(1, 'z');"
        "$false = casti(0, 'z');"
        "$byte = casti(257, 'b');"
        "$short = casti(65537, 'h');"
        "$integer = casti(63, 'i');"
        "$long = casti(2147483648L, 'l');"
        "$float = casti(100, 'f');"
        "$double = casti(1, 'd');"
        "$character = casti('b', 'c');"
        "$display = join(',', @("
        "  $true, $false, $byte, $short,"
        "  $integer, $long, $float, $double,"
        "  $character));"
        "$float_class = [$float getClass];"
        "$unwrapped = scalar($long);") == SLP_OK);

    SlpValue display =
        get_stdlib_global(vm, "$display");
    REQUIRE(SLP_IS_OBJ(display));
    REQUIRE(SLP_OBJ_TYPE(display) ==
            SLP_OBJ_STRING);
    CHECK(std::strcmp(
        SLP_AS_STRING(display)->chars,
        "true,false,1,1,63,2147483648,"
        "100.0,1.0,b") == 0);

    SlpValue float_class =
        get_stdlib_global(vm, "$float_class");
    REQUIRE(SLP_IS_OBJ(float_class));
    REQUIRE(SLP_OBJ_TYPE(float_class) ==
            SLP_OBJ_CLASS);
    CHECK(std::strcmp(
        SLP_AS_CLASS(float_class)->name->chars,
        "java.lang.Float") == 0);

    SlpValue unwrapped =
        get_stdlib_global(vm, "$unwrapped");
    REQUIRE(SLP_IS_OBJ(unwrapped));
    REQUIRE(SLP_OBJ_TYPE(unwrapped) ==
            SLP_OBJ_LONG);
    CHECK(SLP_AS_LONG(unwrapped)->value ==
          INT64_C(2147483648));

    slp_vm_free(vm);
}

TEST_CASE("stdlib inline function evaluates a closure in caller locals") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "sub run {"
        "  local('$value');"
        "  $value = 'before';"
        "  $code = { $value = 'after'; };"
        "  inline($code);"
        "  return $value;"
        "}"
        "$result = run();") == SLP_OK);

    SlpValue result =
        get_stdlib_global(vm, "$result");
    REQUIRE(SLP_IS_OBJ(result));
    REQUIRE(SLP_OBJ_TYPE(result) ==
            SLP_OBJ_STRING);
    CHECK(std::strcmp(
        SLP_AS_STRING(result)->chars,
        "after") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib systemProperties exposes portable runtime metadata") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "%properties = systemProperties();") ==
        SLP_OK);

    SlpValue properties_value =
        get_stdlib_global(vm, "%properties");
    REQUIRE(SLP_IS_OBJ(properties_value));
    REQUIRE(SLP_OBJ_TYPE(properties_value) ==
            SLP_OBJ_HASH);
    SlpObjHash *properties =
        SLP_AS_HASH(properties_value);

    SlpValue user_dir = slp_obj_hash_get(
        properties,
        SLP_OBJ_VAL(
            slp_vm_copy_cstr(vm, "user.dir")));
    SlpValue file_separator =
        slp_obj_hash_get(
            properties,
            SLP_OBJ_VAL(
                slp_vm_copy_cstr(
                    vm, "file.separator")));
    SlpValue os_name = slp_obj_hash_get(
        properties,
        SLP_OBJ_VAL(
            slp_vm_copy_cstr(vm, "os.name")));
    REQUIRE(SLP_IS_OBJ(user_dir));
    REQUIRE(SLP_OBJ_TYPE(user_dir) ==
            SLP_OBJ_STRING);
    REQUIRE(SLP_IS_OBJ(file_separator));
    REQUIRE(SLP_OBJ_TYPE(file_separator) ==
            SLP_OBJ_STRING);
    REQUIRE(SLP_IS_OBJ(os_name));
    REQUIRE(SLP_OBJ_TYPE(os_name) ==
            SLP_OBJ_STRING);

    char working_directory[2048];
    REQUIRE(slp_platform_getcwd(
                working_directory,
                sizeof(working_directory)) !=
            nullptr);
    CHECK(std::strcmp(
        SLP_AS_STRING(user_dir)->chars,
        working_directory) == 0);
    CHECK(SLP_AS_STRING(file_separator)->length ==
          1);
    CHECK(SLP_AS_STRING(file_separator)->chars[0] ==
          slp_platform_path_separator());
    CHECK(SLP_AS_STRING(os_name)->length > 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib profile reports callable names and call counts") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "sub recurse {"
        "  return iff($1 == 0, 1,"
        "    $1 * [$this : $1 - 1]);"
        "}"
        "debug(24);"
        "recurse(3);"
        "println('one');"
        "println('two');"
        "@statistics = profile();") ==
        SLP_OK);

    SlpValue statistics_value =
        get_stdlib_global(
            vm, "@statistics");
    REQUIRE(SLP_IS_OBJ(statistics_value));
    REQUIRE(SLP_OBJ_TYPE(statistics_value) ==
            SLP_OBJ_ARRAY);
    SlpObjArray *statistics =
        SLP_AS_ARRAY(statistics_value);
    CHECK(statistics->read_only);

    int64_t recurse_calls = 0;
    int64_t closure_calls = 0;
    int64_t println_calls = 0;
    for (int index = 0;
         index < statistics->count;
         index++) {
        SlpValue value =
            statistics->elements[index];
        REQUIRE(SLP_IS_OBJ(value));
        REQUIRE(SLP_OBJ_TYPE(value) ==
                SLP_OBJ_JAVA_OBJECT);
        SlpObjJavaObject *statistic =
            SLP_AS_JAVA_OBJECT(value);
        REQUIRE(statistic->kind ==
                SLP_JAVA_PROFILE_STATISTIC);
        REQUIRE(statistic->list != nullptr);
        REQUIRE(statistic->list->count == 3);
        const char *name =
            SLP_AS_STRING(
                statistic->list
                    ->elements[2])->chars;
        int64_t calls =
            SLP_AS_LONG(
                statistic->list
                    ->elements[0])->value;
        if (std::strcmp(name, "&recurse") == 0)
            recurse_calls = calls;
        else if (
            std::strncmp(
                name, "&closure[", 9) == 0)
            closure_calls = calls;
        else if (
            std::strcmp(
                name, "&println") == 0)
            println_calls = calls;
    }
    CHECK(recurse_calls == 1);
    CHECK(closure_calls == 3);
    CHECK(println_calls == 2);

    slp_vm_free(vm);
}

TEST_CASE("stdlib fork isolates globals and returns a wait token") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);
    std::string output;
    slp_vm_set_write_fn(
        vm, capture_stdlib_output, &output);

    REQUIRE(slp_vm_interpret(
        vm,
        "$global = 'parent';"
        "sub mutate {"
        "  $global = 'child';"
        "  println($global);"
        "  return 42;"
        "}"
        "$handle = fork(&mutate);"
        "$token = wait($handle);"
        "println($global);") == SLP_OK);

    CHECK(output == "child\nparent\n");
    CHECK(slp_value_as_number(
        get_stdlib_global(vm, "$token")) ==
        42.0);
    SlpValue global =
        get_stdlib_global(vm, "$global");
    REQUIRE(SLP_IS_OBJ(global));
    REQUIRE(SLP_OBJ_TYPE(global) ==
            SLP_OBJ_STRING);
    CHECK(std::strcmp(
        SLP_AS_STRING(global)->chars,
        "parent") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib fork shares explicitly passed objects and semaphores") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$counter = lambda({"
        "  this('$count');"
        "  $count++;"
        "  return $count;"
        "}, $count => 0);"
        "$mutex = semaphore();"
        "acquire($mutex);"
        "$handle = fork({"
        "  $inside = [$work];"
        "  release($lock);"
        "  return $inside;"
        "}, $work => $counter,"
        "   $lock => $mutex);"
        "$token = wait($handle);"
        "acquire($mutex);"
        "$outside = [$counter];") ==
        SLP_OK);

    CHECK(slp_value_as_number(
        get_stdlib_global(vm, "$token")) ==
        1.0);
    CHECK(slp_value_as_number(
        get_stdlib_global(vm, "$outside")) ==
        2.0);
    CHECK(SLP_IS_NULL(vm->last_error));

    slp_vm_free(vm);
}

TEST_CASE("stdlib include evaluates in the caller scope and restores source metadata") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);
    std::string output;
    slp_vm_set_write_fn(
        vm, capture_stdlib_output, &output);
    slp_vm_set_source_name(
        vm, "tests/fixtures/incit2.sl");

    REQUIRE(slp_vm_interpret(
        vm,
        "sub include_test {"
        "  local('$__INCLUDE__');"
        "  include('incit.sl');"
        "}"
        "include_test();"
        "println(\"post:$__INCLUDE__\");") ==
        SLP_OK);

    CHECK(output.find("Included with ") == 0);
    CHECK(output.find(
        "tests/fixtures/incit.sl\n") !=
        std::string::npos);
    CHECK(output.find("post:\n") !=
          std::string::npos);
    CHECK(std::strcmp(
        vm->source_name, "incit2.sl") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(
            get_stdlib_global(
                vm, "$__SCRIPT__"))->chars,
        "incit2.sl") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib eval and expr return values and share the caller scope") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);
    slp_vm_set_source_name(
        vm, "caller.sl");

    REQUIRE(slp_vm_interpret(
        vm,
        "sub calculate {"
        "  local('$value $evaluated $expressed');"
        "  $value = 4;"
        "  $evaluated = eval('$value = $value * 3; $value;');"
        "  $expressed = expr('$value + 2');"
        "  return @($value, $evaluated, $expressed);"
        "}"
        "@results = calculate();") == SLP_OK);

    SlpObjArray *results =
        SLP_AS_ARRAY(
            get_stdlib_global(vm, "@results"));
    REQUIRE(results->count == 3);
    CHECK(slp_value_as_number(
        results->elements[0]) == 12.0);
    CHECK(slp_value_as_number(
        results->elements[1]) == 12.0);
    CHECK(slp_value_as_number(
        results->elements[2]) == 14.0);
    CHECK(std::strcmp(
        vm->source_name, "caller.sl") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib pack and unpack honor postfix byte order and unsigned widths") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "@octets = unpack('B4', pack('I', 3232235777L));"
        "@little = unpack('I-', pack('B4', @(255, 255, 255, 255)));"
        "@postfix = unpack('I-', pack('B4', @(1, 2, 3, 4)));"
        "@signed = unpack('i', pack('I', 4294967295L));"
        "@long = unpack('l', pack('l', 4294967296L));") ==
        SLP_OK);

    SlpObjArray *octets =
        SLP_AS_ARRAY(
            get_stdlib_global(vm, "@octets"));
    REQUIRE(octets->count == 4);
    CHECK(slp_value_as_number(
              octets->elements[0]) == 192.0);
    CHECK(slp_value_as_number(
              octets->elements[1]) == 168.0);
    CHECK(slp_value_as_number(
              octets->elements[2]) == 1.0);
    CHECK(slp_value_as_number(
              octets->elements[3]) == 1.0);

    SlpObjArray *little =
        SLP_AS_ARRAY(
            get_stdlib_global(vm, "@little"));
    REQUIRE(little->count == 1);
    REQUIRE(SLP_IS_OBJ(little->elements[0]));
    REQUIRE(SLP_OBJ_TYPE(little->elements[0]) ==
            SLP_OBJ_LONG);
    CHECK(SLP_AS_LONG(
              little->elements[0])->value ==
          4294967295LL);

    SlpObjArray *postfix =
        SLP_AS_ARRAY(
            get_stdlib_global(vm, "@postfix"));
    REQUIRE(postfix->count == 1);
    REQUIRE(SLP_IS_OBJ(postfix->elements[0]));
    REQUIRE(SLP_OBJ_TYPE(postfix->elements[0]) ==
            SLP_OBJ_LONG);
    CHECK(SLP_AS_LONG(
              postfix->elements[0])->value ==
          67305985LL);

    SlpObjArray *signed_values =
        SLP_AS_ARRAY(
            get_stdlib_global(vm, "@signed"));
    REQUIRE(signed_values->count == 1);
    CHECK(slp_value_as_number(
              signed_values->elements[0]) ==
          -1.0);

    SlpObjArray *long_values =
        SLP_AS_ARRAY(
            get_stdlib_global(vm, "@long"));
    REQUIRE(long_values->count == 1);
    REQUIRE(SLP_IS_OBJ(long_values->elements[0]));
    REQUIRE(SLP_OBJ_TYPE(long_values->elements[0]) ==
            SLP_OBJ_LONG);
    CHECK(SLP_AS_LONG(
              long_values->elements[0])->value ==
          4294967296LL);

    slp_vm_free(vm);
}

TEST_CASE("stdlib pack rejects odd-length hexadecimal input") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);
    std::string output;
    slp_vm_set_write_fn(
        vm, capture_stdlib_output, &output);
    slp_vm_set_source_name(
        vm, "hex-test.sl");

    REQUIRE(slp_vm_interpret(
        vm,
        "$packed = pack('h*', 'abc');"
        "println('unreachable');") ==
        SLP_OK);

    CHECK(
        output ==
        "Warning: can not pack 'abc' as hex string, "
        "number of characters must be even at hex-test.sl:1\n");
    CHECK(SLP_IS_NULL(
        get_stdlib_global(vm, "$packed")));

    slp_vm_free(vm);
}

TEST_CASE("stdlib split uses Java regex and limit semantics") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "@dots = split('\\\\.', '192.168.1.1');"
        "@words = split('\\\\s+', \"red  green\\tblue\");"
        "@trimmed = split(',', 'a,b,,');"
        "@preserved = split(',', 'a,b,,', -1);"
        "@bounded = split(',', 'a,b,c', 2);") ==
        SLP_OK);

    SlpObjArray *dots =
        SLP_AS_ARRAY(
            get_stdlib_global(vm, "@dots"));
    REQUIRE(dots->count == 4);
    CHECK(std::strcmp(
              SLP_AS_STRING(
                  dots->elements[0])->chars,
              "192") == 0);
    CHECK(std::strcmp(
              SLP_AS_STRING(
                  dots->elements[3])->chars,
              "1") == 0);

    SlpObjArray *words =
        SLP_AS_ARRAY(
            get_stdlib_global(vm, "@words"));
    REQUIRE(words->count == 3);
    CHECK(std::strcmp(
              SLP_AS_STRING(
                  words->elements[1])->chars,
              "green") == 0);

    CHECK(SLP_AS_ARRAY(
              get_stdlib_global(
                  vm, "@trimmed"))->count == 2);
    CHECK(SLP_AS_ARRAY(
              get_stdlib_global(
                  vm, "@preserved"))->count == 4);
    SlpObjArray *bounded =
        SLP_AS_ARRAY(
            get_stdlib_global(vm, "@bounded"));
    REQUIRE(bounded->count == 2);
    CHECK(std::strcmp(
              SLP_AS_STRING(
                  bounded->elements[1])->chars,
              "b,c") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib checkError returns, assigns, and clears eval compile errors") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "eval(\"println('\\\\\\\\'');\");"
        "$truthy = checkError($error);"
        "$message = [$error getMessage];"
        "$formatted = [$error formatErrors];"
        "$class = [$error getClass];"
        "$second = checkError();") == SLP_OK);

    SlpValue truthy =
        get_stdlib_global(vm, "$truthy");
    SlpValue error =
        get_stdlib_global(vm, "$error");
    CHECK(!slp_value_is_falsy(truthy));
    CHECK(slp_value_identity_equals(
        truthy, error));

    SlpValue message =
        get_stdlib_global(vm, "$message");
    REQUIRE(SLP_IS_OBJ(message));
    REQUIRE(SLP_OBJ_TYPE(message) ==
            SLP_OBJ_STRING);
    CHECK(std::strcmp(
        SLP_AS_STRING(message)->chars,
        "2 error(s): Mismatched Parentheses - missing close "
        "paren at 1; Runaway string at 1") == 0);

    SlpValue formatted =
        get_stdlib_global(vm, "$formatted");
    REQUIRE(SLP_IS_OBJ(formatted));
    REQUIRE(SLP_OBJ_TYPE(formatted) ==
            SLP_OBJ_STRING);
    CHECK(std::strstr(
        SLP_AS_STRING(formatted)->chars,
        "Error: Mismatched Parentheses - missing close paren "
        "at line 1\n") != nullptr);
    CHECK(std::strstr(
        SLP_AS_STRING(formatted)->chars,
        "Error: Runaway string at line 1\n") != nullptr);

    SlpValue error_class =
        get_stdlib_global(vm, "$class");
    REQUIRE(SLP_IS_OBJ(error_class));
    REQUIRE(SLP_OBJ_TYPE(error_class) ==
            SLP_OBJ_CLASS);
    CHECK(std::strcmp(
        SLP_AS_CLASS(error_class)->name->chars,
        "sleep.error.YourCodeSucksException") == 0);
    CHECK(SLP_IS_NULL(
        get_stdlib_global(vm, "$second")));
    CHECK(SLP_IS_NULL(vm->last_error));

    slp_vm_free(vm);
}

TEST_CASE("stdlib I/O failures are soft errors available through checkError") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);
    std::string output;
    slp_vm_set_write_fn(
        vm, capture_stdlib_output, &output);
    slp_vm_set_source_name(
        vm, "io-soft-error.sl");

    REQUIRE(slp_vm_interpret(
        vm,
        "$handle = openf("
        "  'slp-definitely-missing-7d0f3a86');"
        "$caught = checkError($error);"
        "$message = [$error getMessage];"
        "$class = [$error getClass];"
        "$cleared = checkError();") == SLP_OK);

    CHECK(SLP_IS_NULL(
        get_stdlib_global(vm, "$handle")));
    CHECK(slp_value_identity_equals(
        get_stdlib_global(vm, "$caught"),
        get_stdlib_global(vm, "$error")));
    SlpValue message =
        get_stdlib_global(vm, "$message");
    REQUIRE(SLP_IS_OBJ(message));
    REQUIRE(SLP_OBJ_TYPE(message) ==
            SLP_OBJ_STRING);
    CHECK(std::strstr(
        SLP_AS_STRING(message)->chars,
        "slp-definitely-missing-7d0f3a86") !=
        nullptr);
    SlpValue error_class =
        get_stdlib_global(vm, "$class");
    REQUIRE(SLP_IS_OBJ(error_class));
    REQUIRE(SLP_OBJ_TYPE(error_class) ==
            SLP_OBJ_CLASS);
    CHECK(std::strcmp(
        SLP_AS_CLASS(error_class)->name->chars,
        "java.io.FileNotFoundException") == 0);
    CHECK(SLP_IS_NULL(
        get_stdlib_global(vm, "$cleared")));
    CHECK(output.empty());

    slp_vm_free(vm);
}

TEST_CASE("stdlib filesystem bridge covers creation roots aliases and timestamps") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);
    const char *path =
        "tests/.slp-filesystem-conformance.tmp";
    std::remove(path);

    REQUIRE(slp_vm_interpret(
        vm,
        "$path = 'tests/.slp-filesystem-conformance.tmp';"
        "$created = createNewFile($path);"
        "$duplicate = createNewFile($path);"
        "$timestamped = setLastModified($path, 1700000000000L);"
        "$modified = lastModified($path);"
        "@roots = listRoots();"
        "$cwd_alias = getCurrentDirectory();"
        "$cwd_primary = cwd();"
        "$deleted = deleteFile($path);") ==
        SLP_OK);

    CHECK(slp_value_as_number(
              get_stdlib_global(
                  vm, "$created")) == 1.0);
    CHECK(SLP_IS_NULL(
        get_stdlib_global(vm, "$duplicate")));
    CHECK(slp_value_as_number(
              get_stdlib_global(
                  vm, "$timestamped")) == 1.0);
    CHECK(std::fabs(
              slp_value_as_number(
                  get_stdlib_global(
                      vm, "$modified")) -
              1700000000000.0) < 2000.0);
    SlpObjArray *roots =
        SLP_AS_ARRAY(
            get_stdlib_global(vm, "@roots"));
    CHECK(roots->count >= 1);
    CHECK(roots->read_only);
    CHECK(slp_value_equals(
        get_stdlib_global(vm, "$cwd_alias"),
        get_stdlib_global(vm, "$cwd_primary")));
    CHECK(slp_value_as_number(
              get_stdlib_global(
                  vm, "$deleted")) == 1.0);
    CHECK(slp_platform_access(path, 0) != 0);

    std::remove(path);
    slp_vm_free(vm);
}

TEST_CASE("stdlib consume and skip discard bounded bytes from handles") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$buffer = allocate(16);"
        "writeb($buffer, 'abcdef');"
        "closef($buffer);"
        "$consumed = consume($buffer, 2);"
        "$skipped = skip($buffer, 1, 1);"
        "$remaining = readb($buffer, -1);") ==
        SLP_OK);

    CHECK(slp_value_as_number(
              get_stdlib_global(
                  vm, "$consumed")) == 2.0);
    CHECK(slp_value_as_number(
              get_stdlib_global(
                  vm, "$skipped")) == 1.0);
    SlpValue remaining =
        get_stdlib_global(vm, "$remaining");
    REQUIRE(SLP_IS_OBJ(remaining));
    REQUIRE(SLP_OBJ_TYPE(remaining) ==
            SLP_OBJ_STRING);
    CHECK(std::strcmp(
              SLP_AS_STRING(remaining)->chars,
              "def") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib console handles are stable and use the VM output channel") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);
    std::string output;
    slp_vm_set_write_fn(
        vm, capture_stdlib_output, &output);

    REQUIRE(slp_vm_interpret(
        vm,
        "$first = getConsole();"
        "$second = getConsole();"
        "$same = $first is $second;"
        "print($first, 'console');"
        "println($second, '-output');"
        "printEOF($first);"
        "closef($second);") == SLP_OK);

    CHECK(slp_value_as_number(
        get_stdlib_global(vm, "$same")) ==
        1.0);
    CHECK(output == "console-output\n");
    CHECK(vm->console_handle != nullptr);
    CHECK(vm->console_handle->file == stdin);

    slp_vm_free(vm);
}

TEST_CASE("stdlib portable static Java methods use known classes") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);
    std::string output;
    slp_vm_set_write_fn(
        vm, capture_stdlib_output, &output);

    REQUIRE(slp_vm_interpret(
        vm,
        "$power = [Math pow: 3, 4];"
        "[[System out] println: 'short'];"
        "[[java.lang.System out] println: 'qualified'];"
        "$handle = [SleepUtils getIOHandle:"
        "  $null, [System out]];"
        "println($handle, 'handle');") ==
        SLP_OK);

    SlpValue power =
        get_stdlib_global(vm, "$power");
    REQUIRE(SLP_IS_OBJ(power));
    REQUIRE(SLP_OBJ_TYPE(power) ==
            SLP_OBJ_DOUBLE);
    CHECK(SLP_AS_DOUBLE(power)->value ==
          doctest::Approx(81.0));
    CHECK(output ==
          "short\nqualified\nhandle\n");
    CHECK(vm->console_handle != nullptr);

    slp_vm_free(vm);
}

TEST_CASE("stdlib Thread currentThread describes fork contexts") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);
    std::string output;
    slp_vm_set_write_fn(
        vm, capture_stdlib_output, &output);
    slp_vm_set_source_name(
        vm, "thread-test.sl");

    REQUIRE(slp_vm_interpret(
        vm,
        "println([Thread currentThread]);"
        "wait(fork({"
        "  println([Thread currentThread]);"
        "}));") == SLP_OK);

    CHECK(
        output ==
        "Thread[main,5,main]\n"
        "Thread[fork of thread-test.sl:1,5,main]\n");

    slp_vm_free(vm);
}

TEST_CASE("stdlib newInstance proxies dispatch interface methods") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "@values = @('a', 'b');"
        "$proxy = newInstance("
        "  ^Iterator,"
        "  {"
        "    if ($0 eq 'hasNext') {"
        "      return size(@values);"
        "    }"
        "    if ($0 eq 'next') {"
        "      return shift(@values);"
        "    }"
        "  });"
        "@seen = @();"
        "while ([$proxy hasNext]) {"
        "  push(@seen, [$proxy next]);"
        "}"
        "$class = [$proxy getClass];"
        "@interfaces = [$class getInterfaces];"
        "$implements = $proxy isa ^Iterator;") ==
        SLP_OK);

    SlpObjArray *seen =
        SLP_AS_ARRAY(
            get_stdlib_global(vm, "@seen"));
    REQUIRE(seen->count == 2);
    CHECK(std::strcmp(
              SLP_AS_STRING(
                  seen->elements[0])->chars,
              "a") == 0);
    CHECK(std::strcmp(
              SLP_AS_STRING(
                  seen->elements[1])->chars,
              "b") == 0);

    SlpValue proxy =
        get_stdlib_global(vm, "$proxy");
    REQUIRE(SLP_IS_OBJ(proxy));
    REQUIRE(SLP_OBJ_TYPE(proxy) ==
            SLP_OBJ_JAVA_OBJECT);
    CHECK(SLP_AS_JAVA_OBJECT(proxy)->kind ==
          SLP_JAVA_PROXY);
    CHECK(std::strncmp(
              SLP_AS_CLASS(
                  get_stdlib_global(
                      vm, "$class"))
                  ->name->chars,
              "com.sun.proxy.$Proxy",
              std::strlen(
                  "com.sun.proxy.$Proxy")) == 0);

    SlpObjArray *interfaces =
        SLP_AS_ARRAY(
            get_stdlib_global(
                vm, "@interfaces"));
    REQUIRE(interfaces->count == 1);
    CHECK(std::strcmp(
              SLP_AS_CLASS(
                  interfaces->elements[0])
                  ->name->chars,
              "java.util.Iterator") == 0);
    CHECK(slp_value_as_number(
              get_stdlib_global(
                  vm, "$implements")) == 1.0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib setField persists instance and class fields") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$first = [new Object];"
        "$second = [new Object];"
        "setField($first, name => 'first', enabled => 1);"
        "setField($second, name => 'second');"
        "setField(^Object, shared => 'class value');"
        "$first_name = [$first name];"
        "$first_enabled = [$first enabled];"
        "$second_name = [$second name];"
        "$first_shared = [$first shared];"
        "$second_shared = [$second shared];"
        "$class_shared = [^Object shared];") == SLP_OK);

    slp_gc_collect(vm);

    CHECK(std::strcmp(
              SLP_AS_STRING(get_stdlib_global(
                  vm, "$first_name"))->chars,
              "first") == 0);
    CHECK(slp_value_as_number(
              get_stdlib_global(
                  vm, "$first_enabled")) == 1.0);
    CHECK(std::strcmp(
              SLP_AS_STRING(get_stdlib_global(
                  vm, "$second_name"))->chars,
              "second") == 0);
    CHECK(std::strcmp(
              SLP_AS_STRING(get_stdlib_global(
                  vm, "$first_shared"))->chars,
              "class value") == 0);
    CHECK(std::strcmp(
              SLP_AS_STRING(get_stdlib_global(
                  vm, "$second_shared"))->chars,
              "class value") == 0);
    CHECK(std::strcmp(
              SLP_AS_STRING(get_stdlib_global(
                  vm, "$class_shared"))->chars,
              "class value") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib use reports portable loader failures through checkError") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "use('slp-definitely-missing-loadable.jar',"
        "    'example.Loadable');"
        "$missing = checkError($missing_error);"
        "$missing_class = [$missing_error getClass];"
        "$missing_message = [$missing_error getMessage];"
        "use(^String);"
        "$unsupported = checkError($unsupported_error);"
        "$unsupported_class = [$unsupported_error getClass];"
        "$unsupported_message ="
        "    [$unsupported_error getMessage];") == SLP_OK);

    CHECK(!slp_value_is_falsy(
        get_stdlib_global(vm, "$missing")));
    CHECK(std::strcmp(
              SLP_AS_CLASS(get_stdlib_global(
                  vm, "$missing_class"))->name->chars,
              "java.io.FileNotFoundException") == 0);
    CHECK(std::strstr(
              SLP_AS_STRING(get_stdlib_global(
                  vm, "$missing_message"))->chars,
              "slp-definitely-missing-loadable.jar") !=
          nullptr);
    CHECK(!slp_value_is_falsy(
        get_stdlib_global(vm, "$unsupported")));
    CHECK(std::strcmp(
              SLP_AS_CLASS(get_stdlib_global(
                  vm, "$unsupported_class"))->name->chars,
              "java.lang.UnsupportedOperationException") ==
          0);
    CHECK(std::strstr(
              SLP_AS_STRING(get_stdlib_global(
                  vm, "$unsupported_message"))->chars,
              "portable runtime") != nullptr);
    CHECK(SLP_IS_NULL(vm->last_error));

    slp_vm_free(vm);
}

TEST_CASE("stdlib taint and untaint preserve per-scalar state") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$plain = 'same';"
        "$value = taint('same');"
        "$before = -istainted $value;"
        "$plain_state = -istainted $plain;"
        "$text = \"$value\";"
        "$combined = $value . '!';"
        "$combined_state = -istainted $combined;"
        "$number = taint(7);"
        "$sum = $number + 5;"
        "$sum_state = -istainted $sum;"
        "$upper = uc($value);"
        "$upper_state = -istainted $upper;"
        "sub derived { return 99; }"
        "$returned = derived($value);"
        "$return_state = -istainted $returned;"
        "@items = @('a', 'b');"
        "taint(@items);"
        "$item_before = -istainted @items[0];"
        "untaint($value);"
        "untaint(@items);"
        "$after = -istainted $value;"
        "$item_after = -istainted @items[0];"
        "$identity = $value is $plain;") ==
        SLP_OK);

    CHECK(!slp_value_is_falsy(
        get_stdlib_global(vm, "$before")));
    CHECK(slp_value_is_falsy(
        get_stdlib_global(vm, "$plain_state")));
    SlpObjString *text = slp_vm_stringify(
        vm, get_stdlib_global(vm, "$text"));
    REQUIRE(text != nullptr);
    CHECK(std::strcmp(
              text->chars, "same") == 0);
    CHECK(!slp_value_is_falsy(
        get_stdlib_global(vm, "$item_before")));
    CHECK(!slp_value_is_falsy(
        get_stdlib_global(vm, "$combined_state")));
    CHECK(!slp_value_is_falsy(
        get_stdlib_global(vm, "$sum_state")));
    CHECK(slp_value_as_number(
              get_stdlib_global(
                  vm, "$sum")) == 12.0);
    CHECK(!slp_value_is_falsy(
        get_stdlib_global(vm, "$upper_state")));
    CHECK(!slp_value_is_falsy(
        get_stdlib_global(vm, "$return_state")));
    CHECK(slp_value_is_falsy(
        get_stdlib_global(vm, "$after")));
    CHECK(slp_value_is_falsy(
        get_stdlib_global(vm, "$item_after")));
    CHECK(!slp_value_is_falsy(
        get_stdlib_global(vm, "$identity")));

    slp_gc_collect(vm);
    slp_vm_free(vm);
}

TEST_CASE("stdlib setEncoding decodes UTF-8 and Latin-1 characters") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$utf8 = allocate();"
        "writeb($utf8, pack('BB', 195, 169));"
        "closef($utf8);"
        "setEncoding($utf8, 'UTF-8');"
        "$utf8_character = readc($utf8);"
        "$latin1 = allocate();"
        "writeb($latin1, pack('B', 233));"
        "closef($latin1);"
        "setEncoding($latin1, 'ISO-8859-1');"
        "$latin1_character = readc($latin1);") ==
        SLP_OK);

    SlpValue utf8_character =
        get_stdlib_global(
            vm, "$utf8_character");
    SlpValue latin1_character =
        get_stdlib_global(
            vm, "$latin1_character");
    REQUIRE(SLP_IS_OBJ(utf8_character));
    REQUIRE(SLP_OBJ_TYPE(utf8_character) ==
            SLP_OBJ_STRING);
    REQUIRE(SLP_IS_OBJ(latin1_character));
    REQUIRE(SLP_OBJ_TYPE(latin1_character) ==
            SLP_OBJ_STRING);
    CHECK(SLP_AS_STRING(
              utf8_character)->length == 2);
    CHECK(SLP_AS_STRING(
              latin1_character)->length == 2);
    CHECK(std::memcmp(
              SLP_AS_STRING(
                  utf8_character)->chars,
              "\xc3\xa9", 2) == 0);
    CHECK(std::memcmp(
              SLP_AS_STRING(
                  latin1_character)->chars,
              "\xc3\xa9", 2) == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib compile_closure binds values and a closure environment") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$environment = { };"
        "$environment['$shared'] = 'scope';"
        "$compiled = compile_closure("
        "  'return $name . \":\" . $shared;',"
        "  $this => $environment,"
        "  $name => 'bound');"
        "$result = [$compiled];") == SLP_OK);

    SlpValue compiled =
        get_stdlib_global(vm, "$compiled");
    REQUIRE(SLP_IS_OBJ(compiled));
    REQUIRE(SLP_OBJ_TYPE(compiled) ==
            SLP_OBJ_CLOSURE);
    SlpObjClosure *closure =
        SLP_AS_CLOSURE(compiled);
    REQUIRE(closure->function->source_name !=
            nullptr);
    CHECK(std::strcmp(
        closure->function->source_name->chars,
        "eval") == 0);
    CHECK(std::strcmp(
        SLP_AS_STRING(
            get_stdlib_global(vm, "$result"))->chars,
        "bound:scope") == 0);

    slp_vm_free(vm);
}

TEST_CASE("stdlib set operations use type-sensitive Sleep identity") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$closure = { return 1; };"
        "@base = @(4.0, $closure);"
        "@other = @(4, $closure, $closure . '');"
        "@union = copy(@base); addAll(@union, @other);"
        "@difference = copy(@base); removeAll(@difference, @other);"
        "@intersection = copy(@base); retainAll(@intersection, @other);") ==
        SLP_OK);

    SlpObjArray *union_values =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@union"));
    SlpObjArray *difference =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@difference"));
    SlpObjArray *intersection =
        SLP_AS_ARRAY(get_stdlib_global(vm, "@intersection"));
    CHECK(union_values->count == 4);
    REQUIRE(difference->count == 1);
    CHECK(SLP_IS_OBJ(difference->elements[0]));
    CHECK(SLP_OBJ_TYPE(difference->elements[0]) == SLP_OBJ_DOUBLE);
    REQUIRE(intersection->count == 1);
    CHECK(SLP_IS_OBJ(intersection->elements[0]));
    CHECK(SLP_OBJ_TYPE(intersection->elements[0]) == SLP_OBJ_CLOSURE);

    slp_vm_free(vm);
}

TEST_CASE("stdlib ordered hash removal policies run on first insertion and are transient") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    REQUIRE(slp_vm_interpret(
        vm,
        "$calls = 0;"
        "%cache = ohash();"
        "setRemovalPolicy(%cache, { $calls++; return 0; });"
        "%cache['a'] = 1;"
        "$buffer = allocate();"
        "writeObject($buffer, %cache);"
        "closef($buffer);"
        "%copy = readObject($buffer);"
        "%copy['b'] = 2;") == SLP_OK);

    CHECK(slp_value_as_number(
              get_stdlib_global(vm, "$calls")) == 1.0);
    CHECK(SLP_AS_HASH(
              get_stdlib_global(vm, "%copy"))->count == 2);
    slp_vm_free(vm);
}

TEST_CASE("stdlib iterator consumers and comparator sort match Sleep semantics") {
    SlpVM *vm = new_stdlib_vm();
    REQUIRE(vm != nullptr);

    const char *script =
        "sub numbers {"
        "    local('$i');"
        "    for ($i = 0; $i < 4; $i++) { yield $i; }"
        "}"
        "sub nested {"
        "    yield @(1, @(2, 3));"
        "    yield @(4);"
        "}"
        "$joined = join(':', &numbers);"
        "@flat = flatten(&nested);"
        "$found = search(@flat, {"
        "    if ($1 == 3) { return 'index=' . $2; }"
        "});"
        "@sorted = sort({ return $1 cmp $2; }, @('b', 'c', 'a'));";
    REQUIRE(slp_vm_interpret(vm, script) == SLP_OK);

    CHECK(std::strcmp(
        SLP_AS_STRING(get_stdlib_global(vm, "$joined"))->chars,
        "0:1:2:3") == 0);

    SlpObjArray *flat = SLP_AS_ARRAY(get_stdlib_global(vm, "@flat"));
    REQUIRE(flat->count == 4);
    for (int i = 0; i < flat->count; i++)
        CHECK(SLP_AS_NUM(flat->elements[i]) == (double)(i + 1));

    CHECK(std::strcmp(
        SLP_AS_STRING(get_stdlib_global(vm, "$found"))->chars,
        "index=2") == 0);

    SlpObjArray *sorted = SLP_AS_ARRAY(get_stdlib_global(vm, "@sorted"));
    REQUIRE(sorted->count == 3);
    CHECK(std::strcmp(SLP_AS_STRING(sorted->elements[0])->chars, "a") == 0);
    CHECK(std::strcmp(SLP_AS_STRING(sorted->elements[1])->chars, "b") == 0);
    CHECK(std::strcmp(SLP_AS_STRING(sorted->elements[2])->chars, "c") == 0);

    slp_vm_free(vm);
}
