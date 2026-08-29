#include "slp_jvm.h"
#include "slp_stdlib.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void *smoke_alloc(
    void *pointer, size_t size, void *userdata) {
    (void)userdata;
    if (size == 0) {
        free(pointer);
        return NULL;
    }
    return realloc(pointer, size);
}

static void smoke_error(
    void *userdata, int line, const char *message) {
    (void)userdata;
    fprintf(
        stderr, "SLP error%s%d: %s\n",
        line > 0 ? " at line " : " ",
        line > 0 ? line : 0, message);
}

static SlpValue global_value(
    SlpVM *vm, const char *name) {
    SlpObjString *key = slp_vm_copy_cstr(vm, name);
    return key
        ? slp_obj_hash_get(
              vm->globals, SLP_OBJ_VAL(key))
        : SLP_NULL_VAL;
}

static bool string_equals(SlpValue value, const char *expected) {
    return SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) == SLP_OBJ_STRING &&
        strcmp(SLP_AS_STRING(value)->chars, expected) == 0;
}

static void print_value(
    SlpVM *vm, const char *name, SlpValue value) {
    SlpObjString *text = slp_vm_stringify(vm, value);
    fprintf(
        stderr, "%s: type=%d value=%s\n", name,
        SLP_IS_OBJ(value) ? (int)SLP_OBJ_TYPE(value) : -1,
        text ? text->chars : "<unprintable>");
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(
            stderr,
            "usage: jvm_loadable_smoke RUNTIME BRIDGE_JAR "
            "SLEEP_JAR LOADABLE_JAR MARKER\n");
        return 2;
    }
    remove(argv[5]);

    char marker_option[4096];
    if (snprintf(
            marker_option, sizeof(marker_option),
            "-Dslp.test.unload.marker=%s", argv[5]) >=
        (int)sizeof(marker_option))
        return 2;
    const char *vm_options[] = {marker_option};

    SlpAllocator allocator = {smoke_alloc, NULL};
    SlpVM *vm = slp_vm_new(&allocator);
    if (!vm) return 2;
    slp_stdlib_init(vm);
    slp_vm_set_error_fn(vm, smoke_error, NULL);
    slp_vm_set_source_name(vm, "loadable-smoke.sl");

    SlpJvmOptions options;
    memset(&options, 0, sizeof(options));
    options.runtime_dir = argv[1];
    options.bridge_jar = argv[2];
    options.classpath = argv[3];
    options.vm_options = vm_options;
    options.vm_option_count = 1;
    SlpJvmError error;
    SlpJvmResult installed = slp_jvm_install(
        &allocator, vm, &options, &error);
    if (installed != SLP_JVM_OK) {
        fprintf(stderr, "JVM install failed: %s\n", error.message);
        slp_vm_free(vm);
        return 1;
    }

    char source[8192];
    if (snprintf(
            source, sizeof(source),
            "$fromSlp = 40;"
            "use('%s', 'dev.slp.jvm.test.TestLoadable');"
            "use(^TestLoadable);"
            "$sum = javaAdd(20, 22);"
            "$order = javaOrder(2, 3);"
            "$scriptName = javaScriptName();"
            "$object = javaObject();"
            "$objectText = [$object toString];"
            "$arraySize = javaArraySize(@(1, 2, 3));"
            "$longValue = javaLong();"
            "$ignored = javaFlagError();"
            "$caught = checkError($error);"
            "$seenFromSlp = javaReadVariable();"
            "javaWriteVariables();",
            argv[4]) >= (int)sizeof(source)) {
        slp_vm_free(vm);
        return 2;
    }

    const char *bridge_source =
            "$unaryPredicate = -javaeven 4;"
            "$binaryPredicate = 4 javaeq 4;"
            "$operatorResult = 2 + 3;"
            "javaenv bound { return 77; }"
            "$environmentResult = javaRunBound();"
            "javafilter filtered 'tag' { return 88; }"
            "$filteredResult = javaRunFiltered();"
            "$predicateValue = 7;"
            "$predicateBodyRuns = 0;"
            "javapredenv ($predicateValue == 7 && "
                "(1 == 1 || javaFlagError())) {"
                "$predicateBodyRuns = 1;"
                "return $predicateValue;"
            "}"
            "javapredenv ($predicateValue == 8 && "
                "javaFlagError()) {"
                "$predicateBodyRuns = 100;"
                "return 99;"
            "}"
            "$predicateTrue = javaPredicateMatch(0);"
            "$predicateFalse = javaPredicateMatch(1);"
            "$predicateBodyResult = javaRunPredicate(0);"
            "$predicateBodyRunsAfter = $predicateBodyRuns;"
            "$predicateDescription = javaPredicateDescription(0);"
            "$predicateValue = 8;"
            "$predicateFailureMatch = javaPredicateMatch(1);"
            "$predicateFailureCaught = "
                "checkError($predicateFailureError);"
            "$callbackResult = javaCallClosure("
                "{ return $0 . ':' . ($1 * 2); }, 21);"
            "$javaClosure = javaMakeClosure();"
            "$javaClosureResult = $javaClosure(6);"
            "@liveArray = @(1, 2);"
            "@liveAlias = @liveArray;"
            "%liveHash = %(\"answer\" => 3);"
            "%liveHashAlias = %liveHash;"
            "@cycle = @();"
            "@cycle[0] = @cycle;"
            "javaRememberCollections();"
            "$javaAliasIdentity = javaCollectionAliases();"
            "$javaCycleIdentity = javaCycleIdentity();"
            "@liveArray[0] = 7;"
            "%liveHash[\"answer\"] = 8;"
            "$retainedArraySeesSlp = javaRetainedArrayValue();"
            "$retainedHashSeesSlp = javaRetainedHashValue();"
            "javaMutateRetainedCollections();"
            "$slpArraySeesJava = @liveAlias[0];"
            "$slpHashSeesJava = %liveHashAlias[\"answer\"];"
            "javaMutateCycle();"
            "$slpCycleIdentity = @cycle[0] is @cycle;"
            "$slpCycleValue = @cycle[1];"
            "javaWriteAliasedCollections();"
            "@javaShared[0] = 31;"
            "%javaShared[\"answer\"] = 41;"
            "$slpJavaArrayAlias = @javaSharedAlias[0];"
            "$slpJavaHashAlias = %javaSharedAlias[\"answer\"];"
            "$javaCreatedArraySeesSlp = javaCreatedArrayValue();"
            "$javaCreatedHashSeesSlp = javaCreatedHashValue();"
            "javaMutateCreatedCollections();"
            "$slpCreatedArraySeesJava = @javaSharedAlias[0];"
            "$slpCreatedHashSeesJava = "
                "%javaSharedAlias[\"answer\"];"
            "$unloadValue = 73;";
    size_t source_length = strlen(source);
    size_t bridge_length = strlen(bridge_source);
    if (source_length + bridge_length + 1 > sizeof(source)) {
        slp_vm_free(vm);
        return 2;
    }
    memcpy(
        source + source_length, bridge_source,
        bridge_length + 1);
    /* use() and every syntax registration intentionally share one parse. */
    SlpResult interpreted = slp_vm_interpret(vm, source);
    SlpValue sum = global_value(vm, "$sum");
    SlpValue script_name = global_value(vm, "$scriptName");
    SlpValue order = global_value(vm, "$order");
    SlpValue object_text = global_value(vm, "$objectText");
    SlpValue array_size = global_value(vm, "$arraySize");
    SlpValue long_value = global_value(vm, "$longValue");
    SlpValue error_value = global_value(vm, "$error");
    SlpValue seen_from_slp = global_value(vm, "$seenFromSlp");
    SlpValue from_java = global_value(vm, "$fromJava");
    SlpValue from_java_array = global_value(vm, "@fromJava");
    SlpValue from_java_hash = global_value(vm, "%fromJava");
    SlpValue unary_predicate = global_value(vm, "$unaryPredicate");
    SlpValue binary_predicate = global_value(vm, "$binaryPredicate");
    SlpValue operator_result = global_value(vm, "$operatorResult");
    SlpValue environment_result = global_value(vm, "$environmentResult");
    SlpValue filtered_result = global_value(vm, "$filteredResult");
    SlpValue predicate_true = global_value(vm, "$predicateTrue");
    SlpValue predicate_false = global_value(vm, "$predicateFalse");
    SlpValue predicate_body_result =
        global_value(vm, "$predicateBodyResult");
    SlpValue predicate_body_runs =
        global_value(vm, "$predicateBodyRunsAfter");
    SlpValue predicate_description =
        global_value(vm, "$predicateDescription");
    SlpValue predicate_failure_match =
        global_value(vm, "$predicateFailureMatch");
    SlpValue predicate_failure_caught =
        global_value(vm, "$predicateFailureCaught");
    SlpValue predicate_failure_error =
        global_value(vm, "$predicateFailureError");
    SlpValue callback_result = global_value(vm, "$callbackResult");
    SlpValue java_closure_result = global_value(vm, "$javaClosureResult");
    SlpValue java_alias_identity = global_value(vm, "$javaAliasIdentity");
    SlpValue java_cycle_identity = global_value(vm, "$javaCycleIdentity");
    SlpValue retained_array_sees_slp =
        global_value(vm, "$retainedArraySeesSlp");
    SlpValue retained_hash_sees_slp =
        global_value(vm, "$retainedHashSeesSlp");
    SlpValue slp_array_sees_java =
        global_value(vm, "$slpArraySeesJava");
    SlpValue slp_hash_sees_java =
        global_value(vm, "$slpHashSeesJava");
    SlpValue slp_cycle_identity =
        global_value(vm, "$slpCycleIdentity");
    SlpValue slp_cycle_value = global_value(vm, "$slpCycleValue");
    SlpValue slp_java_array_alias =
        global_value(vm, "$slpJavaArrayAlias");
    SlpValue slp_java_hash_alias =
        global_value(vm, "$slpJavaHashAlias");
    SlpValue java_created_array_sees_slp =
        global_value(vm, "$javaCreatedArraySeesSlp");
    SlpValue java_created_hash_sees_slp =
        global_value(vm, "$javaCreatedHashSeesSlp");
    SlpValue slp_created_array_sees_java =
        global_value(vm, "$slpCreatedArraySeesJava");
    SlpValue slp_created_hash_sees_java =
        global_value(vm, "$slpCreatedHashSeesJava");
    SlpValue live_array = global_value(vm, "@liveArray");
    SlpValue live_alias = global_value(vm, "@liveAlias");
    SlpValue java_shared = global_value(vm, "@javaShared");
    SlpValue java_shared_alias = global_value(vm, "@javaSharedAlias");
    SlpValue answer_key = SLP_OBJ_VAL(slp_vm_copy_cstr(vm, "answer"));
    SlpValue hash_answer =
        SLP_IS_OBJ(from_java_hash) &&
                SLP_OBJ_TYPE(from_java_hash) == SLP_OBJ_HASH
            ? slp_vm_hash_get(
                  vm, SLP_AS_HASH(from_java_hash), answer_key)
            : SLP_NULL_VAL;
    bool valid =
        interpreted == SLP_OK &&
        SLP_IS_NUM(sum) && SLP_AS_NUM(sum) == 42.0 &&
        SLP_IS_NUM(order) && SLP_AS_NUM(order) == 23.0 &&
        string_equals(script_name, "loadable-smoke.sl") &&
        string_equals(object_text, "loadable-object") &&
        SLP_IS_NUM(array_size) &&
        SLP_AS_NUM(array_size) == 3.0 &&
        SLP_IS_OBJ(long_value) &&
        SLP_OBJ_TYPE(long_value) == SLP_OBJ_LONG &&
        SLP_AS_LONG(long_value)->value == INT64_C(9007199254740993) &&
        string_equals(error_value, "loadable-error") &&
        SLP_IS_NUM(seen_from_slp) &&
        SLP_AS_NUM(seen_from_slp) == 40.0 &&
        SLP_IS_NUM(from_java) && SLP_AS_NUM(from_java) == 91.0 &&
        SLP_IS_OBJ(from_java_array) &&
        SLP_OBJ_TYPE(from_java_array) == SLP_OBJ_ARRAY &&
        SLP_AS_ARRAY(from_java_array)->count == 2 &&
        SLP_IS_NUM(hash_answer) && SLP_AS_NUM(hash_answer) == 42.0 &&
        !slp_value_is_falsy(unary_predicate) &&
        !slp_value_is_falsy(binary_predicate) &&
        SLP_IS_NUM(operator_result) &&
        SLP_AS_NUM(operator_result) == 203.0 &&
        SLP_IS_NUM(environment_result) &&
        SLP_AS_NUM(environment_result) == 77.0 &&
        string_equals(filtered_result, "'tag':88.0") &&
        !slp_value_is_falsy(predicate_true) &&
        slp_value_is_falsy(predicate_false) &&
        SLP_IS_NUM(predicate_body_result) &&
        SLP_AS_NUM(predicate_body_result) == 7.0 &&
        SLP_IS_NUM(predicate_body_runs) &&
        SLP_AS_NUM(predicate_body_runs) == 1.0 &&
        string_equals(
            predicate_description,
            "And(Eval:==,Or(Eval:==,Eval:-istrue))") &&
        slp_value_is_falsy(predicate_failure_match) &&
        !slp_value_is_falsy(predicate_failure_caught) &&
        string_equals(
            predicate_failure_error, "loadable-error") &&
        string_equals(callback_result, "from-java:42") &&
        SLP_IS_NUM(java_closure_result) &&
        SLP_AS_NUM(java_closure_result) == 18.0 &&
        !slp_value_is_falsy(java_alias_identity) &&
        !slp_value_is_falsy(java_cycle_identity) &&
        SLP_IS_NUM(retained_array_sees_slp) &&
        SLP_AS_NUM(retained_array_sees_slp) == 7.0 &&
        SLP_IS_NUM(retained_hash_sees_slp) &&
        SLP_AS_NUM(retained_hash_sees_slp) == 8.0 &&
        SLP_IS_NUM(slp_array_sees_java) &&
        SLP_AS_NUM(slp_array_sees_java) == 17.0 &&
        SLP_IS_NUM(slp_hash_sees_java) &&
        SLP_AS_NUM(slp_hash_sees_java) == 18.0 &&
        !slp_value_is_falsy(slp_cycle_identity) &&
        SLP_IS_NUM(slp_cycle_value) &&
        SLP_AS_NUM(slp_cycle_value) == 19.0 &&
        SLP_IS_NUM(slp_java_array_alias) &&
        SLP_AS_NUM(slp_java_array_alias) == 31.0 &&
        SLP_IS_NUM(slp_java_hash_alias) &&
        SLP_AS_NUM(slp_java_hash_alias) == 41.0 &&
        SLP_IS_NUM(java_created_array_sees_slp) &&
        SLP_AS_NUM(java_created_array_sees_slp) == 31.0 &&
        SLP_IS_NUM(java_created_hash_sees_slp) &&
        SLP_AS_NUM(java_created_hash_sees_slp) == 41.0 &&
        SLP_IS_NUM(slp_created_array_sees_java) &&
        SLP_AS_NUM(slp_created_array_sees_java) == 50.0 &&
        SLP_IS_NUM(slp_created_hash_sees_java) &&
        SLP_AS_NUM(slp_created_hash_sees_java) == 60.0 &&
        slp_value_identity_equals(live_array, live_alias) &&
        slp_value_identity_equals(java_shared, java_shared_alias);
    if (!valid) {
        fprintf(stderr, "interpret result: %d\n", (int)interpreted);
        print_value(vm, "$sum", sum);
        print_value(vm, "$order", order);
        print_value(vm, "$scriptName", script_name);
        print_value(vm, "$objectText", object_text);
        print_value(vm, "$arraySize", array_size);
        print_value(vm, "$longValue", long_value);
        print_value(vm, "$error", error_value);
        print_value(vm, "$seenFromSlp", seen_from_slp);
        print_value(vm, "$fromJava", from_java);
        print_value(vm, "@fromJava", from_java_array);
        print_value(vm, "%fromJava", from_java_hash);
        print_value(vm, "$unaryPredicate", unary_predicate);
        print_value(vm, "$binaryPredicate", binary_predicate);
        print_value(vm, "$operatorResult", operator_result);
        print_value(vm, "$environmentResult", environment_result);
        print_value(vm, "$filteredResult", filtered_result);
        print_value(vm, "$predicateTrue", predicate_true);
        print_value(vm, "$predicateFalse", predicate_false);
        print_value(
            vm, "$predicateBodyResult", predicate_body_result);
        print_value(
            vm, "$predicateBodyRunsAfter", predicate_body_runs);
        print_value(
            vm, "$predicateDescription",
            predicate_description);
        print_value(
            vm, "$predicateFailureMatch",
            predicate_failure_match);
        print_value(
            vm, "$predicateFailureCaught",
            predicate_failure_caught);
        print_value(
            vm, "$predicateFailureError",
            predicate_failure_error);
        print_value(vm, "$callbackResult", callback_result);
        print_value(vm, "$javaClosureResult", java_closure_result);
        print_value(vm, "$javaAliasIdentity", java_alias_identity);
        print_value(vm, "$javaCycleIdentity", java_cycle_identity);
        print_value(
            vm, "$retainedArraySeesSlp", retained_array_sees_slp);
        print_value(
            vm, "$retainedHashSeesSlp", retained_hash_sees_slp);
        print_value(vm, "$slpArraySeesJava", slp_array_sees_java);
        print_value(vm, "$slpHashSeesJava", slp_hash_sees_java);
        print_value(vm, "$slpCycleIdentity", slp_cycle_identity);
        print_value(vm, "$slpCycleValue", slp_cycle_value);
        print_value(vm, "$slpJavaArrayAlias", slp_java_array_alias);
        print_value(vm, "$slpJavaHashAlias", slp_java_hash_alias);
        print_value(
            vm, "$javaCreatedArraySeesSlp",
            java_created_array_sees_slp);
        print_value(
            vm, "$javaCreatedHashSeesSlp",
            java_created_hash_sees_slp);
        print_value(
            vm, "$slpCreatedArraySeesJava",
            slp_created_array_sees_java);
        print_value(
            vm, "$slpCreatedHashSeesJava",
            slp_created_hash_sees_java);
    }

    FILE *marker = fopen(argv[5], "rb");
    if (marker) {
        fclose(marker);
        valid = false;
    }
    slp_vm_free(vm);

    marker = fopen(argv[5], "rb");
    if (!marker) {
        valid = false;
    } else {
        char state[16] = {0};
        size_t read = fread(state, 1, sizeof(state) - 1, marker);
        fclose(marker);
        if (read != strlen("unloaded:2:73") ||
            strcmp(state, "unloaded:2:73") != 0)
            valid = false;
    }
    remove(argv[5]);
    if (!valid) {
        fprintf(stderr, "JVM Loadable smoke result did not match\n");
        return 1;
    }
    puts("JVM Loadable smoke test passed");
    return 0;
}
