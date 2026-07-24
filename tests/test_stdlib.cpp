#include "doctest.h"
#include <cstdlib>
#include <cstring>

extern "C" {
#include "slp_common.h"
#include "slp_core.h"
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
