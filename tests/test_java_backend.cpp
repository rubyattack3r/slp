#include "doctest.h"

#include <cstdlib>
#include <cstring>

extern "C" {
#include "slp_vm.h"
#include "slp_stdlib.h"
}

namespace {

struct MockJavaState {
    int imports;
    int resolutions;
    int invocations;
    int predicates;
    int operators;
    int callable_calls;
    int loads;
    int releases;
    int closing_order;
    int release_order;
    int destroy_order;
    int next_order;
};

static void *mock_alloc(
    void *pointer, size_t size, void *userdata) {
    (void)userdata;
    if (size == 0) {
        std::free(pointer);
        return nullptr;
    }
    return std::realloc(pointer, size);
}

static SlpValue dereference(SlpValue value) {
    while (SLP_IS_OBJ(value) &&
           SLP_OBJ_TYPE(value) ==
               SLP_OBJ_SCALAR_CELL)
        value = SLP_AS_SCALAR_CELL(value)->value;
    return slp_value_unwrap_taint(value);
}

static SlpJavaBackendResult mock_import(
    SlpVM *vm, const char *target,
    const char *source_path, void *userdata) {
    (void)vm;
    MockJavaState *state =
        static_cast<MockJavaState*>(userdata);
    CHECK(std::strcmp(target, "demo.Widget") == 0);
    CHECK(std::strcmp(source_path, "widget.jar") == 0);
    state->imports++;
    return SLP_JAVA_HANDLED;
}

static SlpJavaBackendResult mock_resolve(
    SlpVM *vm, const char *name,
    SlpValue *out_class, void *userdata) {
    MockJavaState *state =
        static_cast<MockJavaState*>(userdata);
    CHECK(std::strcmp(name, "Widget") == 0);
    state->resolutions++;
    SlpObjClass *class_object =
        slp_vm_new_class(vm, "demo.Widget");
    REQUIRE(class_object != nullptr);
    *out_class = SLP_OBJ_VAL(class_object);
    return SLP_JAVA_HANDLED;
}

static SlpJavaBackendResult mock_invoke(
    SlpVM *vm, SlpValue target,
    const char *message,
    const SlpValue *arguments, int argument_count,
    SlpValue *out_result, void *userdata) {
    MockJavaState *state =
        static_cast<MockJavaState*>(userdata);
    state->invocations++;

    if (SLP_IS_NULL(target) &&
        std::strcmp(message, "Widget") == 0) {
        CHECK(argument_count == 1);
        SlpValue argument =
            dereference(arguments[0]);
        REQUIRE(SLP_IS_OBJ(argument));
        CHECK(SLP_OBJ_TYPE(argument) == SLP_OBJ_STRING);
        CHECK(std::strcmp(
                  SLP_AS_STRING(argument)->chars,
                  "created") == 0);
        int *handle = static_cast<int*>(
            vm->allocator->reallocate(
                nullptr, sizeof(int),
                vm->allocator->user_data));
        REQUIRE(handle != nullptr);
        *handle = 42;
        SlpObjJavaObject *object =
            slp_vm_new_java_reference(
                vm, "demo.Widget",
                SLP_JAVA_GENERIC, handle);
        REQUIRE(object != nullptr);
        *out_result = SLP_OBJ_VAL(object);
        return SLP_JAVA_HANDLED;
    }

    if (SLP_IS_OBJ(target) &&
        SLP_OBJ_TYPE(target) ==
            SLP_OBJ_JAVA_OBJECT &&
        SLP_AS_JAVA_OBJECT(target)->backend_handle &&
        std::strcmp(message, "echo") == 0) {
        CHECK(argument_count == 1);
        *out_result = dereference(arguments[0]);
        return SLP_JAVA_HANDLED;
    }

    return SLP_JAVA_UNHANDLED;
}

static SlpJavaBackendResult mock_load(
    SlpVM *vm, const SlpValue *arguments,
    int argument_count, SlpValue *out_result,
    void *userdata) {
    MockJavaState *state =
        static_cast<MockJavaState*>(userdata);
    REQUIRE(argument_count == 1);
    SlpValue loadable = dereference(arguments[0]);
    REQUIRE(SLP_IS_OBJ(loadable));
    REQUIRE(SLP_OBJ_TYPE(loadable) == SLP_OBJ_CLASS);
    CHECK(std::strcmp(
              SLP_AS_CLASS(loadable)->name->chars,
              "demo.Widget") == 0);
    slp_vm_register_predicate_name(vm, "javaeq");
    state->loads++;
    *out_result = SLP_NULL_VAL;
    return SLP_JAVA_HANDLED;
}

static SlpJavaBackendResult mock_decide(
    SlpVM *vm, const char *name,
    const SlpValue *terms, int term_count,
    bool *out_result, void *userdata) {
    (void)vm;
    MockJavaState *state =
        static_cast<MockJavaState*>(userdata);
    if (std::strcmp(name, "javaeq") != 0)
        return SLP_JAVA_UNHANDLED;
    REQUIRE(term_count == 2);
    REQUIRE(SLP_IS_NUM(dereference(terms[0])));
    REQUIRE(SLP_IS_NUM(dereference(terms[1])));
    *out_result =
        SLP_AS_NUM(dereference(terms[0])) ==
        SLP_AS_NUM(dereference(terms[1]));
    state->predicates++;
    return SLP_JAVA_HANDLED;
}

static SlpJavaBackendResult mock_operate(
    SlpVM *vm, const char *name,
    const SlpValue *terms, int term_count,
    SlpValue *out_result, void *userdata) {
    (void)vm;
    MockJavaState *state =
        static_cast<MockJavaState*>(userdata);
    if (std::strcmp(name, "+") != 0)
        return SLP_JAVA_UNHANDLED;
    REQUIRE(term_count == 2);
    *out_result = SLP_NUM_VAL(
        SLP_AS_NUM(dereference(terms[0])) * 100.0 +
        SLP_AS_NUM(dereference(terms[1])));
    state->operators++;
    return SLP_JAVA_HANDLED;
}

static SlpJavaBackendResult mock_call_callable(
    SlpVM *vm, SlpValue callable,
    const char *message,
    const SlpValue *arguments, int argument_count,
    SlpValue *out_result, void *userdata) {
    (void)vm;
    MockJavaState *state =
        static_cast<MockJavaState*>(userdata);
    REQUIRE(SLP_IS_OBJ(callable));
    REQUIRE(SLP_OBJ_TYPE(callable) == SLP_OBJ_JAVA_OBJECT);
    CHECK(std::strcmp(message, "") == 0);
    REQUIRE(argument_count == 1);
    *out_result = dereference(arguments[0]);
    state->callable_calls++;
    return SLP_JAVA_HANDLED;
}

static SlpValue mock_named_function(
    SlpVM *vm, const char *name,
    SlpValue *arguments, int argument_count,
    void *userdata) {
    (void)vm;
    int *calls = static_cast<int*>(userdata);
    CHECK(std::strcmp(name, "providerEcho") == 0);
    REQUIRE(argument_count == 1);
    (*calls)++;
    return arguments[0];
}

static void mock_release(
    SlpVM *vm, void *object_handle,
    void *userdata) {
    MockJavaState *state =
        static_cast<MockJavaState*>(userdata);
    state->releases++;
    state->release_order = ++state->next_order;
    vm->allocator->reallocate(
        object_handle, 0,
        vm->allocator->user_data);
}

static void mock_closing(
    SlpVM *vm, void *userdata) {
    (void)vm;
    MockJavaState *state =
        static_cast<MockJavaState*>(userdata);
    state->closing_order = ++state->next_order;
}

static void mock_destroy(
    SlpVM *vm, void *userdata) {
    (void)vm;
    MockJavaState *state =
        static_cast<MockJavaState*>(userdata);
    state->destroy_order = ++state->next_order;
}

struct PredicateEnvironmentState {
    int bindings;
    int true_conditions;
    int false_conditions;
    int body_calls;
    bool metadata_valid;
    SlpValue body_result;
};

static void predicate_environment_handler(
    SlpVM *vm, const char *keyword,
    const char *identifier, const char *string_argument,
    SlpObjClosure **conditions, size_t condition_count,
    SlpObjClosure *body,
    void *userdata) {
    PredicateEnvironmentState *state =
        static_cast<PredicateEnvironmentState*>(userdata);
    state->bindings++;
    const char *expected_shape = state->bindings == 1
        ? "O(P2:==,P7:-istrue)"
        : "A(P2:==,P7:-istrue)";
    state->metadata_valid =
        state->metadata_valid &&
        std::strcmp(keyword, "when") == 0 &&
        identifier == nullptr && string_argument != nullptr &&
        std::strcmp(string_argument, expected_shape) == 0 &&
        conditions != nullptr && condition_count == 2 &&
        body != nullptr;
    if (!conditions || condition_count != 2 || !body) return;

    SlpValue condition_result = SLP_NULL_VAL;
    if (slp_vm_call_value(
            vm, SLP_OBJ_VAL(conditions[0]),
            nullptr, 0, &condition_result) != SLP_OK)
        return;
    if (slp_value_is_falsy(dereference(condition_result))) {
        state->false_conditions++;
        return;
    }

    state->true_conditions++;
    if (slp_vm_call_value(
            vm, SLP_OBJ_VAL(body), nullptr, 0,
            &state->body_result) == SLP_OK)
        state->body_calls++;
}

} // namespace

TEST_CASE("Java backend: dispatches imports, classes, and objects") {
    SlpAllocator allocator = {mock_alloc, nullptr};
    SlpVM *vm = slp_vm_new(&allocator);
    REQUIRE(vm != nullptr);

    MockJavaState state = {};
    SlpJavaBackend backend = {};
    backend.abi_version =
        SLP_JAVA_BACKEND_ABI_VERSION;
    backend.userdata = &state;
    backend.import_class = mock_import;
    backend.resolve_class = mock_resolve;
    backend.invoke = mock_invoke;
    backend.release_object = mock_release;
    backend.vm_closing = mock_closing;
    backend.destroy = mock_destroy;

    CHECK(slp_vm_install_java_backend(
        vm, &backend));
    CHECK(slp_vm_has_java_backend(vm));
    CHECK_FALSE(slp_vm_install_java_backend(
        vm, &backend));

    SlpResult result = slp_vm_interpret(
        vm,
        "import demo.Widget from: 'widget.jar';"
        "$class = ^Widget;"
        "$object = [new Widget: 'created'];"
        "$answer = [$object echo: 'hello'];");
    REQUIRE(result == SLP_OK);

    SlpValue answer = slp_obj_hash_get(
        vm->globals,
        SLP_OBJ_VAL(slp_vm_copy_cstr(
            vm, "$answer")));
    REQUIRE(SLP_IS_OBJ(answer));
    REQUIRE(SLP_OBJ_TYPE(answer) ==
            SLP_OBJ_STRING);
    CHECK(std::strcmp(
              SLP_AS_STRING(answer)->chars,
              "hello") == 0);
    CHECK(state.imports == 1);
    CHECK(state.resolutions == 1);
    CHECK(state.invocations == 2);

    slp_vm_free(vm);
    CHECK(state.releases == 1);
    CHECK(state.closing_order < state.release_order);
    CHECK(state.release_order < state.destroy_order);
}

TEST_CASE("Java backend: rejects incompatible ABI versions") {
    SlpAllocator allocator = {mock_alloc, nullptr};
    SlpVM *vm = slp_vm_new(&allocator);
    REQUIRE(vm != nullptr);

    SlpJavaBackend backend = {};
    backend.abi_version =
        SLP_JAVA_BACKEND_ABI_VERSION + 1;
    CHECK_FALSE(slp_vm_install_java_backend(
        vm, &backend));
    CHECK_FALSE(slp_vm_has_java_backend(vm));

    slp_vm_free(vm);
}

TEST_CASE("Java backend: stdlib use delegates Loadable installation") {
    SlpAllocator allocator = {mock_alloc, nullptr};
    SlpVM *vm = slp_vm_new(&allocator);
    REQUIRE(vm != nullptr);
    slp_stdlib_init(vm);

    MockJavaState state = {};
    SlpJavaBackend backend = {};
    backend.abi_version =
        SLP_JAVA_BACKEND_ABI_VERSION;
    backend.userdata = &state;
    backend.resolve_class = mock_resolve;
    backend.load_extension = mock_load;
    backend.decide = mock_decide;
    REQUIRE(slp_vm_install_java_backend(vm, &backend));

    CHECK(slp_vm_interpret(
        vm,
        "use(^Widget);"
        "$sameSourcePredicate = 4 javaeq 4;") == SLP_OK);
    CHECK(state.resolutions == 1);
    CHECK(state.loads == 1);
    CHECK(state.predicates == 1);
    slp_vm_free(vm);
}

TEST_CASE("Java backend: provider named natives receive identity and state") {
    SlpAllocator allocator = {mock_alloc, nullptr};
    SlpVM *vm = slp_vm_new(&allocator);
    REQUIRE(vm != nullptr);
    int calls = 0;
    slp_vm_register_named_native(
        vm, "providerEcho", mock_named_function,
        &calls);

    REQUIRE(slp_vm_interpret(
        vm, "$answer = providerEcho('hello');") == SLP_OK);
    SlpValue answer = slp_obj_hash_get(
        vm->globals,
        SLP_OBJ_VAL(slp_vm_copy_cstr(
            vm, "$answer")));
    REQUIRE(SLP_IS_OBJ(answer));
    REQUIRE(SLP_OBJ_TYPE(answer) == SLP_OBJ_STRING);
    CHECK(std::strcmp(
              SLP_AS_STRING(answer)->chars,
              "hello") == 0);
    CHECK(calls == 1);
    slp_vm_free(vm);
}

TEST_CASE("Java backend: dispatches Sleep bridges and Java callables") {
    SlpAllocator allocator = {mock_alloc, nullptr};
    SlpVM *vm = slp_vm_new(&allocator);
    REQUIRE(vm != nullptr);

    MockJavaState state = {};
    SlpJavaBackend backend = {};
    backend.abi_version = SLP_JAVA_BACKEND_ABI_VERSION;
    backend.userdata = &state;
    backend.decide = mock_decide;
    backend.operate = mock_operate;
    backend.call_callable = mock_call_callable;
    backend.release_object = mock_release;
    REQUIRE(slp_vm_install_java_backend(vm, &backend));
    slp_vm_register_predicate_name(vm, "javaeq");

    int *handle = static_cast<int*>(allocator.reallocate(
        nullptr, sizeof(int), allocator.user_data));
    REQUIRE(handle != nullptr);
    *handle = 1;
    SlpObjJavaObject *callable = slp_vm_new_java_reference(
        vm, "sleep.bridges.SleepClosure",
        SLP_JAVA_GENERIC, handle);
    REQUIRE(callable != nullptr);
    REQUIRE(slp_vm_set_global_value(
        vm, "$javaClosure", SLP_OBJ_VAL(callable)));

    REQUIRE(slp_vm_interpret(
        vm,
        "$predicate = 4 javaeq 4;"
        "$operator = 2 + 3;"
        "$called = $javaClosure(7);") == SLP_OK);
    SlpValue predicate = slp_obj_hash_get(
        vm->globals,
        SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "$predicate")));
    SlpValue operated = slp_obj_hash_get(
        vm->globals,
        SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "$operator")));
    SlpValue called = slp_obj_hash_get(
        vm->globals,
        SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "$called")));
    CHECK_FALSE(slp_value_is_falsy(predicate));
    REQUIRE(SLP_IS_NUM(operated));
    CHECK(SLP_AS_NUM(operated) == 203.0);
    REQUIRE(SLP_IS_NUM(called));
    CHECK(SLP_AS_NUM(called) == 7.0);
    CHECK(state.predicates == 1);
    CHECK(state.operators == 1);
    CHECK(state.callable_calls == 1);

    slp_vm_free(vm);
    CHECK(state.releases == 1);
}

TEST_CASE("Predicate environments preserve conditions and short circuiting") {
    SlpAllocator allocator = {mock_alloc, nullptr};
    SlpVM *vm = slp_vm_new(&allocator);
    REQUIRE(vm != nullptr);
    PredicateEnvironmentState state = {};
    state.metadata_valid = true;
    state.body_result = SLP_NULL_VAL;
    slp_vm_register_predicate_bridge_type(
        vm, "when", predicate_environment_handler,
        &state);

    REQUIRE(slp_vm_interpret(
        vm,
        "$enabled = 1;"
        "$bodyRan = 0;"
        "when ($enabled == 1 || missingConditionCall()) {"
            "$bodyRan = 1;"
            "return 42;"
        "}"
        "when ($enabled == 0 && missingConditionCall()) {"
            "$bodyRan = 2;"
            "return 99;"
        "}") == SLP_OK);

    SlpValue body_ran = slp_obj_hash_get(
        vm->globals,
        SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "$bodyRan")));
    CHECK(state.bindings == 2);
    CHECK(state.true_conditions == 1);
    CHECK(state.false_conditions == 1);
    CHECK(state.body_calls == 1);
    CHECK(state.metadata_valid);
    REQUIRE(SLP_IS_NUM(dereference(state.body_result)));
    CHECK(SLP_AS_NUM(dereference(state.body_result)) == 42.0);
    REQUIRE(SLP_IS_NUM(dereference(body_ran)));
    CHECK(SLP_AS_NUM(dereference(body_ran)) == 1.0);

    slp_vm_free(vm);
}
