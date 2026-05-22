#include "doctest.h"
#include <string.h>

extern "C" {
#include "sleepy_common.h"
#include "sleepy_core.h"
#include "sleepy_value.h"
#include "sleepy_vm.h"
#include "sleepy_compiler.h"
#include "aggressor.h"
#include <stdlib.h>

static void *test_vm_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static SleepyAllocator vm_allocator = {test_vm_alloc, NULL};

/* Fallback handler for testing */
static int test_fallback_called = 0;
static char test_fallback_func[64] = {0};

static SleepyValue test_fallback(SleepyVM *vm, const char *func_name, SleepyValue *args, int argc, void *user_data) {
    (void)vm; (void)args; (void)argc; (void)user_data;
    test_fallback_called++;
    strncpy(test_fallback_func, func_name, sizeof(test_fallback_func) - 1);
    return SLEEPY_NUM_VAL(42.0);
}

/* Custom override for testing */
static int test_override_called = 0;
static SleepyValue test_override(SleepyVM *vm, SleepyValue *args, int argc, void *user_data) {
    (void)vm; (void)args; (void)argc; (void)user_data;
    test_override_called++;
    return SLEEPY_NUM_VAL(99.0);
}
}

TEST_CASE("libaggressor: init and free") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    REQUIRE(vm != nullptr);

    AggressorConfig cfg = {
        .fallback = test_fallback,
        .user_data = nullptr,
    };
    AggressorState *state = aggressor_init(vm, &cfg);
    REQUIRE(state != nullptr);

    free(state);
    sleepy_vm_free(vm);
}

TEST_CASE("libaggressor: fallback and overrides") {
    test_fallback_called = 0;
    test_override_called = 0;
    memset(test_fallback_func, 0, sizeof(test_fallback_func));

    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    AggressorConfig cfg = {
        .fallback = test_fallback,
        .user_data = nullptr,
    };
    AggressorState *state = aggressor_init(vm, &cfg);

    /* Test fallback call */
    SleepyResult r = sleepy_vm_interpret(vm, "bgetprivs();");
    CHECK(r == SLEEPY_OK);
    CHECK(test_fallback_called == 1);
    CHECK(strcmp(test_fallback_func, "bgetprivs") == 0);

    /* Test override */
    aggressor_set(state, "bgetprivs", test_override);
    r = sleepy_vm_interpret(vm, "bgetprivs();");
    CHECK(r == SLEEPY_OK);
    CHECK(test_override_called == 1);

    free(state);
    sleepy_vm_free(vm);
}

TEST_CASE("libaggressor: builtin pure utilities") {
    SleepyVM *vm = sleepy_vm_new(&vm_allocator);
    AggressorConfig cfg = {
        .fallback = nullptr,
        .user_data = nullptr,
    };
    AggressorState *state = aggressor_init(vm, &cfg);

    /* Test iff */
    SleepyResult r = sleepy_vm_interpret(vm, "iff(true, 1, 2);");
    CHECK(r == SLEEPY_OK);

    /* Test strlen */
    r = sleepy_vm_interpret(vm, "strlen(\"hello\");");
    CHECK(r == SLEEPY_OK);

    /* Test size */
    r = sleepy_vm_interpret(vm, "size(\"world\");");
    CHECK(r == SLEEPY_OK);

    /* Test lc & uc */
    r = sleepy_vm_interpret(vm, "lc(\"HELLO\");");
    CHECK(r == SLEEPY_OK);
    r = sleepy_vm_interpret(vm, "uc(\"world\");");
    CHECK(r == SLEEPY_OK);

    /* Test replace */
    r = sleepy_vm_interpret(vm, "replace(\"foobar\", \"foo\", \"baz\");");
    CHECK(r == SLEEPY_OK);

    free(state);
    sleepy_vm_free(vm);
}
