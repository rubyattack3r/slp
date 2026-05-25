#include "doctest.h"
#include <string.h>

extern "C" {
#include "slp_common.h"
#include "slp_core.h"
#include "slp_value.h"
#include "slp_vm.h"
#include "slp_compiler.h"
#include "aggressor.h"
#include "slp_stdlib.h"
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
    slp_stdlib_init(vm);
    REQUIRE(vm != nullptr);

    AggressorConfig cfg = {
        .fallback = test_fallback,
        .user_data = nullptr,
    };
    AggressorState *state = aggressor_new(vm, &cfg);
    REQUIRE(state != nullptr);

    aggressor_free(state);
    slp_vm_free(vm);
}

TEST_CASE("libaggressor: fallback and overrides") {
    test_fallback_called = 0;
    test_override_called = 0;
    memset(test_fallback_func, 0, sizeof(test_fallback_func));

    SlpVM *vm = slp_vm_new(&vm_allocator);
    slp_stdlib_init(vm);
    AggressorConfig cfg = {
        .fallback = test_fallback,
        .user_data = nullptr,
    };
    AggressorState *state = aggressor_new(vm, &cfg);

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

    aggressor_free(state);
    slp_vm_free(vm);
}

TEST_CASE("libaggressor: builtin pure utilities") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    slp_stdlib_init(vm);
    AggressorConfig cfg = {
        .fallback = nullptr,
        .user_data = nullptr,
    };
    AggressorState *state = aggressor_new(vm, &cfg);

    /* Test iff */
    SlpResult r = slp_vm_interpret(vm, "assert iff(true, 1, 2) == 1; assert iff(false, 1, 2) == 2;");
    CHECK(r == SLP_OK);

    /* Test strlen */
    r = slp_vm_interpret(vm, "assert strlen(\"hello\") == 5;");
    CHECK(r == SLP_OK);

    /* Test size */
    r = slp_vm_interpret(vm, "assert size(\"world\") == 5;");
    CHECK(r == SLP_OK);

    /* Test lc & uc */
    r = slp_vm_interpret(vm, "assert lc(\"HELLO\") == \"hello\"; assert uc(\"world\") == \"WORLD\";");
    CHECK(r == SLP_OK);

    /* Test replace */
    r = slp_vm_interpret(vm, "assert replace(\"foobar\", \"foo\", \"baz\") == \"bazbar\";");
    CHECK(r == SLP_OK);

    /* Test split & join */
    r = slp_vm_interpret(vm, "assert join(\"-\", split(\" \", \"a b c\")) == \"a-b-c\";");
    CHECK(r == SLP_OK);

    /* Test keys & values */
    r = slp_vm_interpret(vm, "$h = %(\"a\" => 1); assert size(keys($h)) == 1; assert keys($h)[0] == \"a\"; assert values($h)[0] == 1;");
    CHECK(r == SLP_OK);

    /* Test sublist */
    r = slp_vm_interpret(vm, "$arr = @(1, 2, 3, 4); $sub = sublist($arr, 1, 3); assert size($sub) == 2; assert $sub[0] == 2; assert $sub[1] == 3;");
    CHECK(r == SLP_OK);

    /* Test rand & tstamp */
    r = slp_vm_interpret(vm, "assert rand(10) >= 0; assert tstamp() > 0;");
    CHECK(r == SLP_OK);

    aggressor_free(state);
    slp_vm_free(vm);
}

static void test_aggressor_error(void *ud, int line, const char *msg) {
    (void)ud;
    fprintf(stderr, "[Test VM Error] Line %d: %s\n", line, msg);
}

TEST_CASE("libaggressor: stateful beacon and dialog mocks") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    slp_stdlib_init(vm);
    slp_vm_set_error_fn(vm, test_aggressor_error, nullptr);
    AggressorConfig cfg = {
        .fallback = nullptr,
        .user_data = nullptr,
    };
    AggressorState *state = aggressor_new(vm, &cfg);

    /* Test beacons() array and size */
    SlpResult r = slp_vm_interpret(vm, 
        "$b = beacons();"
        "assert size($b) == 3;"
        "assert $b[0]['id'] eq \"12345\";"
        "assert $b[0]['user'] eq \"SYSTEM\";"
        "assert $b[1]['id'] eq \"67890\";"
        "assert $b[1]['user'] eq \"jdoe\";"
        "assert $b[2]['id'] eq \"11111\";"
        "assert $b[2]['user'] eq \"root\";"
    );
    CHECK(r == SLP_OK);

    /* Test beacon_ids() array */
    r = slp_vm_interpret(vm,
        "$ids = beacon_ids();"
        "assert size($ids) == 3;"
        "assert $ids[0] eq \"12345\";"
        "assert $ids[1] eq \"67890\";"
        "assert $ids[2] eq \"11111\";"
    );
    CHECK(r == SLP_OK);

    /* Test stateful beacon_info() attributes lookup */
    r = slp_vm_interpret(vm,
        "assert beacon_info(\"12345\", \"user\") eq \"SYSTEM\";"
        "assert beacon_info(\"67890\", \"computer\") eq \"HR-PC\";"
        "assert beacon_info(\"11111\", \"os\") eq \"Linux\";"
        "assert beacon_info(\"unknown\", \"barch\") eq \"x64\";"
    );
    CHECK(r == SLP_OK);

    /* Test dialog builder, drow, dbutton */
    r = slp_vm_interpret(vm,
        "sub my_callback { }"
        "$d = dialog(\"My Harvester\", %(user => \"default_user\"), &my_callback);"
    );
    CHECK(r == SLP_OK);

    r = slp_vm_interpret(vm, "assert $d['__title'] eq \"My Harvester\";");
    CHECK(r == SLP_OK);

    r = slp_vm_interpret(vm, "assert $d['user'] eq \"default_user\";");
    CHECK(r == SLP_OK);

    r = slp_vm_interpret(vm, "drow_text($d, \"user\", \"Username:\");");
    CHECK(r == SLP_OK);

    r = slp_vm_interpret(vm, "assert size($d['__controls']) == 1;");
    CHECK(r == SLP_OK);

    r = slp_vm_interpret(vm, "assert $d['__controls'][0]['type'] eq \"drow_text\";");
    CHECK(r == SLP_OK);

    r = slp_vm_interpret(vm, "assert $d['__controls'][0]['key'] eq \"user\";");
    CHECK(r == SLP_OK);

    r = slp_vm_interpret(vm, "dcheckbox($d, \"flag\", \"Enable Checkbox\");");
    CHECK(r == SLP_OK);

    r = slp_vm_interpret(vm, "assert size($d['__controls']) == 2;");
    CHECK(r == SLP_OK);

    r = slp_vm_interpret(vm, "assert $d['__controls'][1]['type'] eq \"dcheckbox\";");
    CHECK(r == SLP_OK);

    r = slp_vm_interpret(vm, "assert $d['__controls'][1]['key'] eq \"flag\";");
    CHECK(r == SLP_OK);

    r = slp_vm_interpret(vm, "dbutton($d, \"Launch\", \"submit_action\");");
    CHECK(r == SLP_OK);

    r = slp_vm_interpret(vm, "assert size($d['__buttons']) == 1;");
    CHECK(r == SLP_OK);

    r = slp_vm_interpret(vm, "assert $d['__buttons'][0]['label'] eq \"Launch\";");
    CHECK(r == SLP_OK);

    r = slp_vm_interpret(vm, "assert $d['__buttons'][0]['action'] eq \"submit_action\";");
    CHECK(r == SLP_OK);

    aggressor_free(state);
    slp_vm_free(vm);
}

TEST_CASE("libaggressor: dynamic mock beacon configuration") {
    SlpVM *vm = slp_vm_new(&vm_allocator);
    slp_stdlib_init(vm);
    slp_vm_set_error_fn(vm, test_aggressor_error, nullptr);
    AggressorConfig cfg = {
        .fallback = nullptr,
        .user_data = nullptr,
    };
    AggressorState *state = aggressor_new(vm, &cfg);

    /* 1. Update an existing property */
    SlpResult r = slp_vm_interpret(vm, 
        "beacon_info_set(\"12345\", \"computer\", \"WIN-NEW-PC\");"
        "assert beacon_info(\"12345\", \"computer\") eq \"WIN-NEW-PC\";"
    );
    CHECK(r == SLP_OK);

    /* 2. Add a new beacon with custom properties */
    r = slp_vm_interpret(vm,
        "beacon_info_set(\"99999\", \"process\", \"lsass.exe\");"
        "beacon_info_set(\"99999\", \"computer\", \"TARGET-DC\");"
        "assert beacon_info(\"99999\", \"process\") eq \"lsass.exe\";"
        "assert beacon_info(\"99999\", \"computer\") eq \"TARGET-DC\";"
    );
    CHECK(r == SLP_OK);

    /* 3. Check beacon_ids() updates */
    r = slp_vm_interpret(vm,
        "$ids = beacon_ids();"
        "assert size($ids) == 4;"
        "assert $ids[3] eq \"99999\";"
    );
    CHECK(r == SLP_OK);

    /* 4. Check beacons() hash dynamic collection updates */
    r = slp_vm_interpret(vm,
        "$b = beacons();"
        "assert size($b) == 4;"
        "assert $b[3]['id'] eq \"99999\";"
        "assert $b[3]['process'] eq \"lsass.exe\";"
        "assert $b[3]['computer'] eq \"TARGET-DC\";"
    );
    CHECK(r == SLP_OK);

    aggressor_free(state);
    slp_vm_free(vm);
}
