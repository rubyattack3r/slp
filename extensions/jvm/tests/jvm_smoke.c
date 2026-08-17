#include "slp_jvm.h"
#include "slp_stdlib.h"

#include <stdio.h>
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

int main(int argc, char **argv) {
    if (argc != 1 && argc != 3) {
        fprintf(
            stderr,
            "usage: jvm_smoke [JAVA_RUNTIME BRIDGE_JAR]\n");
        return 2;
    }

    SlpAllocator allocator = {smoke_alloc, NULL};
    SlpVM *vm = slp_vm_new(&allocator);
    if (!vm) return 2;
    slp_stdlib_init(vm);
    slp_vm_set_error_fn(vm, smoke_error, NULL);

    SlpJvmOptions options;
    memset(&options, 0, sizeof(options));
    if (argc == 3) {
        options.runtime_dir = argv[1];
        options.bridge_jar = argv[2];
    }
    SlpJvmError error;
    SlpJvmResult installed = slp_jvm_install(
        &allocator, vm, &options, &error);
    if (installed != SLP_JVM_OK) {
        fprintf(
            stderr, "JVM install failed (%s): %s\n",
            slp_jvm_result_string(
                &allocator, installed),
            error.message);
        slp_vm_free(vm);
        return 1;
    }

    SlpResult interpreted = slp_vm_interpret(
        vm,
        "import java.util.*;"
        "$list = [new ArrayList];"
        "$added = [$list add: 'hello'];"
        "$size = [$list size];"
        "$value = [$list get: 0];"
        "$class = ^ArrayList;"
        "$maximum = [^java.lang.Math max: 2, 7];"
        "$limit = [^java.lang.Integer MAX_VALUE];"
        "$both = [^java.lang.Boolean logicalAnd: true, false];"
        "use(^String);"
        "$loadableError = checkError();");
    if (interpreted != SLP_OK) {
        slp_vm_free(vm);
        return 1;
    }

    SlpValue size = global_value(vm, "$size");
    SlpValue value = global_value(vm, "$value");
    SlpValue class_value = global_value(vm, "$class");
    SlpValue maximum = global_value(vm, "$maximum");
    SlpValue limit = global_value(vm, "$limit");
    SlpValue both = global_value(vm, "$both");
    SlpValue loadable_error =
        global_value(vm, "$loadableError");
    bool valid =
        SLP_IS_NUM(size) && SLP_AS_NUM(size) == 1.0 &&
        SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) == SLP_OBJ_STRING &&
        strcmp(
            SLP_AS_STRING(value)->chars,
            "hello") == 0 &&
        SLP_IS_OBJ(class_value) &&
        SLP_OBJ_TYPE(class_value) == SLP_OBJ_CLASS &&
        strcmp(
            SLP_AS_CLASS(class_value)->name->chars,
            "java.util.ArrayList") == 0 &&
        SLP_IS_NUM(maximum) &&
        SLP_AS_NUM(maximum) == 7.0 &&
        SLP_IS_NUM(limit) &&
        SLP_AS_NUM(limit) == 2147483647.0 &&
        SLP_IS_FALSE(both) &&
        !slp_value_is_falsy(loadable_error);
    slp_vm_free(vm);
    if (!valid) {
        fprintf(stderr, "JVM smoke result did not match\n");
        return 1;
    }
    puts("JVM smoke test passed");
    return 0;
}
