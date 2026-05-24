#include "doctest.h"
#include <string.h>

extern "C" {
#include "slp_common.h"
#include "slp_core.h"
#include "slp_value.h"
#include "slp_vm.h"
#include "slp_compiler.h"
#include "aggressor.h"
#include <stdlib.h>

static void *test_vm_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static SlpAllocator vm_allocator = {test_vm_alloc, NULL};

/* Fallback handler for testing */
static int test_fallback_called = 0;
static char test_fallback_func[64] = {0};

static SlpValue test_fallback(SlpVM *vm, const char *func_name, SlpValue *args, int argc, void *user_data) {
    (void)vm; (void)args; (void)argc; (void)user_data;
    test_fallback_called++;
    strncpy(test_fallback_func, func_name, sizeof(test_fallback_func) - 1);
    return SLP_NUM_VAL(42.0);
}

/* Custom override for testing */
static int test_override_called = 0;
static SlpValue test_override(SlpVM *vm, SlpValue *args, int argc, void *user_data) {
    (void)vm; (void)args; (void)argc; (void)user_data;
    test_override_called++;
    return SLP_NUM_VAL(99.0);
}
}

TEST_CASE("libaggressor: init and free") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);

    AggressorConfig cfg = {
        .fallback = test_fallback,
        .user_data = nullptr,
    };
    AggressorState *state = aggressor_init(vm, &cfg);
    REQUIRE(state != nullptr);

    aggressor_deinit(state);
    slp_vm_free(vm);
}

TEST_CASE("libaggressor: fallback and overrides") {
    test_fallback_called = 0;
    test_override_called = 0;
    memset(test_fallback_func, 0, sizeof(test_fallback_func));

    SlpVM *vm = slp_vm_new(&vm_allocator);
    AggressorConfig cfg = {
        .fallback = test_fallback,
        .user_data = nullptr,
    };
    AggressorState *state = aggressor_init(vm, &cfg);

    /* Test fallback call */
    SlpResult r = slp_vm_interpret(vm, "bgetprivs();");
    CHECK(r == SLP_OK);
    CHECK(test_fallback_called == 1);
    CHECK(strcmp(test_fallback_func, "bgetprivs") == 0);

    /* Test override */
    aggressor_set(state, "bgetprivs", test_override);
    r = slp_vm_interpret(vm, "bgetprivs();");
    CHECK(r == SLP_OK);
    CHECK(test_override_called == 1);

    aggressor_deinit(state);
    slp_vm_free(vm);
}

TEST_CASE("libaggressor: builtin pure utilities") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    AggressorConfig cfg = {
        .fallback = nullptr,
        .user_data = nullptr,
    };
    AggressorState *state = aggressor_init(vm, &cfg);

    /* Test iff */
    SlpResult r = slp_vm_interpret(vm, "iff(true, 1, 2);");
    CHECK(r == SLP_OK);

    /* Test strlen */
    r = slp_vm_interpret(vm, "strlen(\"hello\");");
    CHECK(r == SLP_OK);

    /* Test size */
    r = slp_vm_interpret(vm, "size(\"world\");");
    CHECK(r == SLP_OK);

    /* Test lc & uc */
    r = slp_vm_interpret(vm, "lc(\"HELLO\");");
    CHECK(r == SLP_OK);
    r = slp_vm_interpret(vm, "uc(\"world\");");
    CHECK(r == SLP_OK);

    /* Test replace */
    r = slp_vm_interpret(vm, "replace(\"foobar\", \"foo\", \"baz\");");
    CHECK(r == SLP_OK);

    aggressor_deinit(state);
    slp_vm_free(vm);
}
