#include "slp_jvm.h"
#include "slp_embed.h"

#include <jni.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#if defined(_WIN32)
#define SLP_JVM_PATH_SEPARATOR ';'
#define SLP_JVM_DIR_SEPARATOR '\\'
typedef HMODULE SlpJvmLibrary;
#else
#define SLP_JVM_PATH_SEPARATOR ':'
#define SLP_JVM_DIR_SEPARATOR '/'
typedef void *SlpJvmLibrary;
#endif

typedef jint (JNICALL *SlpCreateJavaVMFn)(JavaVM **, void **, void *);
typedef jint (JNICALL *SlpGetCreatedJavaVMsFn)(JavaVM **, jsize, jsize *);

typedef struct {
    jobject reference;
} SlpJvmObjectHandle;

typedef struct SlpJvmState SlpJvmState;

typedef struct SlpJvmCallableHandle {
    SlpJvmState *state;
    SlpCallable *callable;
    SlpValue value;
    struct SlpJvmCallableHandle *next;
} SlpJvmCallableHandle;

struct SlpJvmState {
    SlpAllocator *allocator;
    SlpVM *vm;
    SlpJvmLibrary library;
    JavaVM *jvm;
    bool owns_jvm;
    bool attached_thread;
    bool sleep_runtime_active;

    jobject bridge;
    jclass bridge_class;
    jclass object_class;
    jclass string_class;
    jclass number_class;
    jclass boolean_class;
    jclass character_class;
    jclass double_class;
    jclass long_class;
    jclass class_class;
    jclass object_array_class;
    jclass array_value_class;
    jclass hash_value_class;

    jmethodID bridge_import;
    jmethodID bridge_resolve;
    jmethodID bridge_construct;
    jmethodID bridge_invoke;
    jmethodID bridge_load_loadable;
    jmethodID bridge_prepare_sleep_runtime;
    jmethodID bridge_invoke_function;
    jmethodID bridge_take_function_error;
    jmethodID bridge_unload_loadables;
    jmethodID bridge_set_native_state;
    jmethodID bridge_sync_variables;
    jmethodID bridge_snapshot_variables;
    jmethodID bridge_make_array;
    jmethodID bridge_make_hash;
    jmethodID bridge_has_predicate;
    jmethodID bridge_decide;
    jmethodID bridge_has_operator;
    jmethodID bridge_operate;
    jmethodID bridge_bind_environment;
    jmethodID bridge_wrap_slp_closure;
    jmethodID bridge_slp_closure_handle;
    jmethodID bridge_is_sleep_closure;
    jmethodID bridge_invoke_sleep_closure;
    jfieldID array_value_identity;
    jfieldID array_value_elements;
    jfieldID hash_value_identity;
    jfieldID hash_value_entries;
    jmethodID object_get_class;
    jmethodID object_to_string;
    jmethodID class_get_name;
    jmethodID boolean_value_of;
    jmethodID boolean_value;
    jmethodID double_value_of;
    jmethodID long_value_of;
    jmethodID number_double_value;
    jmethodID number_long_value;
    SlpJvmCallableHandle *callables;
};

static jobject JNICALL invoke_slp_callable_native(
    JNIEnv *env, jobject bridge, jlong state_handle,
    jlong callable_handle, jstring message,
    jobjectArray arguments);
static void backend_environment_handler(
    SlpVM *vm, const char *keyword,
    const char *identifier, const char *string_arg,
    SlpObjClosure **conditions, size_t condition_count,
    SlpObjClosure *closure,
    void *userdata);

static void set_error(
    SlpJvmError *error, const char *format,
    const char *detail) {
    if (!error) return;
    if (!format) {
        error->message[0] = '\0';
        return;
    }
    snprintf(
        error->message, sizeof(error->message),
        format, detail ? detail : "");
}

static void *jvm_alloc(SlpAllocator *allocator, size_t size) {
    return allocator->reallocate(
        NULL, size, allocator->user_data);
}

static void jvm_free(SlpAllocator *allocator, void *pointer) {
    if (pointer)
        allocator->reallocate(
            pointer, 0, allocator->user_data);
}

static bool join_path(
    char *output, size_t capacity,
    const char *base, const char *suffix) {
    size_t base_length = strlen(base);
    bool needs_separator =
        base_length > 0 &&
        base[base_length - 1] != '/' &&
        base[base_length - 1] != '\\';
    int written = snprintf(
        output, capacity, "%s%s%s", base,
        needs_separator
            ? (SLP_JVM_DIR_SEPARATOR == '\\' ? "\\" : "/")
            : "",
        suffix);
    return written >= 0 && (size_t)written < capacity;
}

static bool executable_path(char *output, size_t capacity) {
#if defined(_WIN32)
    DWORD length = GetModuleFileNameA(
        NULL, output, (DWORD)capacity);
    return length > 0 && (size_t)length < capacity;
#elif defined(__APPLE__)
    uint32_t length = (uint32_t)capacity;
    return _NSGetExecutablePath(output, &length) == 0;
#elif defined(__linux__)
    ssize_t length = readlink(
        "/proc/self/exe", output, capacity - 1);
    if (length < 0 || (size_t)length >= capacity - 1)
        return false;
    output[length] = '\0';
    return true;
#else
    (void)output;
    (void)capacity;
    return false;
#endif
}

static bool discover_runtime(char *output, size_t capacity) {
    char executable[PATH_MAX];
    if (!executable_path(executable, sizeof(executable)))
        return false;
    char *separator = strrchr(executable, '/');
#if defined(_WIN32)
    char *backslash = strrchr(executable, '\\');
    if (!separator || (backslash && backslash > separator))
        separator = backslash;
#endif
    if (!separator) return false;
    *separator = '\0';
    return join_path(output, capacity, executable, "runtime");
}

#if defined(_WIN32)
static SlpJvmLibrary open_library(const char *path) {
    return LoadLibraryA(path);
}

static void close_library(SlpJvmLibrary library) {
    if (library) FreeLibrary(library);
}

static void *load_symbol(SlpJvmLibrary library, const char *name) {
    FARPROC symbol = GetProcAddress(library, name);
    void *result = NULL;
    size_t bytes = sizeof(result) < sizeof(symbol)
        ? sizeof(result) : sizeof(symbol);
    memcpy(&result, &symbol, bytes);
    return result;
}

static const char *library_error(void) {
    static char message[64];
    snprintf(
        message, sizeof(message),
        "Windows error %lu", (unsigned long)GetLastError());
    return message;
}
#else
static SlpJvmLibrary open_library(const char *path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

static void close_library(SlpJvmLibrary library) {
    if (library) dlclose(library);
}

static void *load_symbol(SlpJvmLibrary library, const char *name) {
    return dlsym(library, name);
}

static const char *library_error(void) {
    const char *message = dlerror();
    return message ? message : "unknown dynamic loader error";
}
#endif

static SlpJvmLibrary load_jvm_library(
    const char *runtime_dir, char *loaded_path,
    size_t loaded_path_capacity, SlpJvmError *error) {
#if defined(_WIN32)
    static const char *candidates[] = {
        "bin\\server\\jvm.dll",
        "jre\\bin\\server\\jvm.dll"
    };
#elif defined(__APPLE__)
    static const char *candidates[] = {
        "lib/server/libjvm.dylib",
        "Contents/Home/lib/server/libjvm.dylib",
        "jre/lib/server/libjvm.dylib"
    };
#else
    static const char *candidates[] = {
        "lib/server/libjvm.so",
        "jre/lib/amd64/server/libjvm.so",
        "jre/lib/aarch64/server/libjvm.so"
    };
#endif
    size_t count = sizeof(candidates) / sizeof(candidates[0]);
    char path[PATH_MAX];
    const char *last_error = "libjvm was not found";
    for (size_t index = 0; index < count; index++) {
        if (!join_path(
                path, sizeof(path), runtime_dir,
                candidates[index]))
            continue;
        SlpJvmLibrary library = open_library(path);
        if (library) {
            snprintf(
                loaded_path, loaded_path_capacity,
                "%s", path);
            return library;
        }
        last_error = library_error();
    }
    set_error(error, "Unable to load bundled JVM: %s", last_error);
    return (SlpJvmLibrary)0;
}

static bool copy_function_pointer(
    void *symbol, void *function_pointer,
    size_t function_pointer_size) {
    if (!symbol || function_pointer_size > sizeof(symbol))
        return false;
    memcpy(function_pointer, &symbol, function_pointer_size);
    return true;
}

static JNIEnv *current_env(SlpJvmState *state, bool *attached_here) {
    JNIEnv *env = NULL;
    *attached_here = false;
    jint status = (*state->jvm)->GetEnv(
        state->jvm, (void **)&env, JNI_VERSION_1_8);
    if (status == JNI_OK) return env;
    if (status != JNI_EDETACHED) return NULL;
    if ((*state->jvm)->AttachCurrentThread(
            state->jvm, (void **)&env, NULL) != JNI_OK)
        return NULL;
    *attached_here = true;
    return env;
}

static void detach_if_needed(
    SlpJvmState *state, bool attached_here) {
    if (attached_here)
        (*state->jvm)->DetachCurrentThread(state->jvm);
}

static jclass global_class(JNIEnv *env, const char *name) {
    jclass local = (*env)->FindClass(env, name);
    if (!local) return NULL;
    jclass global = (jclass)(*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    return global;
}

static bool exception_message(
    SlpJvmState *state, JNIEnv *env,
    const char *context, char *output, size_t capacity) {
    if (!(*env)->ExceptionCheck(env)) return false;
    jthrowable exception = (*env)->ExceptionOccurred(env);
    (*env)->ExceptionClear(env);
    const char *description = "Java exception";
    jstring text = NULL;
    if (exception && state->object_to_string) {
        text = (jstring)(*env)->CallObjectMethod(
            env, exception, state->object_to_string);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            text = NULL;
        }
    }
    const char *utf = NULL;
    if (text)
        utf = (*env)->GetStringUTFChars(env, text, NULL);
    if (utf) description = utf;
    snprintf(
        output, capacity, "%s: %s",
        context ? context : "JVM", description);
    if (utf)
        (*env)->ReleaseStringUTFChars(env, text, utf);
    if (text) (*env)->DeleteLocalRef(env, text);
    if (exception) (*env)->DeleteLocalRef(env, exception);
    return true;
}

static bool warn_exception(
    SlpVM *vm, SlpJvmState *state,
    JNIEnv *env, const char *context) {
    char message[768];
    if (!exception_message(
            state, env, context,
            message, sizeof(message)))
        return false;
    slp_vm_warning(vm, message);
    return true;
}

static bool flag_exception(
    SlpVM *vm, SlpJvmState *state,
    JNIEnv *env, const char *context) {
    char message[768];
    if (!exception_message(
            state, env, context,
            message, sizeof(message)))
        return false;
    slp_vm_flag_error(
        vm,
        slp_vm_new_error(
            vm, "java.lang.RuntimeException",
            message, message, NULL));
    return true;
}

static bool cache_bridge(
    SlpJvmState *state, JNIEnv *env,
    SlpJvmError *error) {
    state->object_class = global_class(env, "java/lang/Object");
    state->string_class = global_class(env, "java/lang/String");
    state->number_class = global_class(env, "java/lang/Number");
    state->boolean_class = global_class(env, "java/lang/Boolean");
    state->character_class = global_class(env, "java/lang/Character");
    state->double_class = global_class(env, "java/lang/Double");
    state->long_class = global_class(env, "java/lang/Long");
    state->class_class = global_class(env, "java/lang/Class");
    state->object_array_class = global_class(env, "[Ljava/lang/Object;");
    state->bridge_class = global_class(env, "dev/slp/jvm/Bridge");
    state->array_value_class = global_class(
        env, "dev/slp/jvm/Bridge$ArrayValue");
    state->hash_value_class = global_class(
        env, "dev/slp/jvm/Bridge$HashValue");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        set_error(
            error,
            "Unable to load slp-jvm-bridge.jar: %s",
            "dev.slp.jvm.Bridge is not on the JVM class path");
        return false;
    }
    if (!state->object_class || !state->string_class ||
        !state->number_class || !state->boolean_class ||
        !state->character_class || !state->double_class ||
        !state->long_class || !state->class_class ||
        !state->object_array_class || !state->bridge_class ||
        !state->array_value_class || !state->hash_value_class) {
        set_error(error, "Unable to initialize JNI class cache: %s", "class lookup failed");
        return false;
    }

    jmethodID bridge_init = (*env)->GetMethodID(
        env, state->bridge_class, "<init>", "()V");
    state->bridge_import = (*env)->GetMethodID(
        env, state->bridge_class, "importTarget",
        "(Ljava/lang/String;Ljava/lang/String;)V");
    state->bridge_resolve = (*env)->GetMethodID(
        env, state->bridge_class, "resolve",
        "(Ljava/lang/String;)Ljava/lang/Class;");
    state->bridge_construct = (*env)->GetMethodID(
        env, state->bridge_class, "construct",
        "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/Object;");
    state->bridge_invoke = (*env)->GetMethodID(
        env, state->bridge_class, "invoke",
        "(Ljava/lang/Object;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/Object;");
    state->bridge_load_loadable = (*env)->GetMethodID(
        env, state->bridge_class, "loadLoadable",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/Object;)[Ljava/lang/String;");
    state->bridge_prepare_sleep_runtime = (*env)->GetMethodID(
        env, state->bridge_class, "prepareSleepRuntime",
        "(Ljava/lang/String;)V");
    state->bridge_invoke_function = (*env)->GetMethodID(
        env, state->bridge_class, "invokeFunction",
        "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/Object;");
    state->bridge_take_function_error = (*env)->GetMethodID(
        env, state->bridge_class, "takeFunctionError",
        "()Ljava/lang/Object;");
    state->bridge_unload_loadables = (*env)->GetMethodID(
        env, state->bridge_class, "unloadLoadables", "()V");
    state->bridge_set_native_state = (*env)->GetMethodID(
        env, state->bridge_class, "setNativeState", "(J)V");
    state->bridge_sync_variables = (*env)->GetMethodID(
        env, state->bridge_class, "syncVariables",
        "([Ljava/lang/String;[Ljava/lang/Object;)V");
    state->bridge_snapshot_variables = (*env)->GetMethodID(
        env, state->bridge_class, "snapshotVariables",
        "()[Ljava/lang/Object;");
    state->bridge_make_array = (*env)->GetMethodID(
        env, state->bridge_class, "makeArray",
        "(J[Ljava/lang/Object;)Ldev/slp/jvm/Bridge$ArrayValue;");
    state->bridge_make_hash = (*env)->GetMethodID(
        env, state->bridge_class, "makeHash",
        "(J[Ljava/lang/Object;)Ldev/slp/jvm/Bridge$HashValue;");
    state->bridge_has_predicate = (*env)->GetMethodID(
        env, state->bridge_class, "hasPredicate",
        "(Ljava/lang/String;)Z");
    state->bridge_decide = (*env)->GetMethodID(
        env, state->bridge_class, "decide",
        "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/Boolean;");
    state->bridge_has_operator = (*env)->GetMethodID(
        env, state->bridge_class, "hasOperator",
        "(Ljava/lang/String;)Z");
    state->bridge_operate = (*env)->GetMethodID(
        env, state->bridge_class, "operate",
        "(Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/Object;");
    state->bridge_bind_environment = (*env)->GetMethodID(
        env, state->bridge_class, "bindEnvironment",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[JJ)[Ljava/lang/String;");
    state->bridge_wrap_slp_closure = (*env)->GetMethodID(
        env, state->bridge_class, "wrapSlpClosure",
        "(J)Ljava/lang/Object;");
    state->bridge_slp_closure_handle = (*env)->GetMethodID(
        env, state->bridge_class, "slpClosureHandle",
        "(Ljava/lang/Object;)J");
    state->bridge_is_sleep_closure = (*env)->GetMethodID(
        env, state->bridge_class, "isSleepClosure",
        "(Ljava/lang/Object;)Z");
    state->bridge_invoke_sleep_closure = (*env)->GetMethodID(
        env, state->bridge_class, "invokeSleepClosure",
        "(Ljava/lang/Object;Ljava/lang/String;[Ljava/lang/Object;)Ljava/lang/Object;");
    state->array_value_identity = (*env)->GetFieldID(
        env, state->array_value_class, "identity", "J");
    state->array_value_elements = (*env)->GetFieldID(
        env, state->array_value_class, "elements",
        "[Ljava/lang/Object;");
    state->hash_value_identity = (*env)->GetFieldID(
        env, state->hash_value_class, "identity", "J");
    state->hash_value_entries = (*env)->GetFieldID(
        env, state->hash_value_class, "entries",
        "[Ljava/lang/Object;");
    state->object_get_class = (*env)->GetMethodID(
        env, state->object_class, "getClass", "()Ljava/lang/Class;");
    state->object_to_string = (*env)->GetMethodID(
        env, state->object_class, "toString", "()Ljava/lang/String;");
    state->class_get_name = (*env)->GetMethodID(
        env, state->class_class, "getName", "()Ljava/lang/String;");
    state->boolean_value_of = (*env)->GetStaticMethodID(
        env, state->boolean_class, "valueOf", "(Z)Ljava/lang/Boolean;");
    state->boolean_value = (*env)->GetMethodID(
        env, state->boolean_class, "booleanValue", "()Z");
    state->double_value_of = (*env)->GetStaticMethodID(
        env, state->double_class,
        "valueOf", "(D)Ljava/lang/Double;");
    state->long_value_of = (*env)->GetStaticMethodID(
        env, state->long_class,
        "valueOf", "(J)Ljava/lang/Long;");
    state->number_double_value = (*env)->GetMethodID(
        env, state->number_class, "doubleValue", "()D");
    state->number_long_value = (*env)->GetMethodID(
        env, state->number_class, "longValue", "()J");
    if (!bridge_init || !state->bridge_import ||
        !state->bridge_resolve || !state->bridge_construct ||
        !state->bridge_invoke || !state->bridge_load_loadable ||
        !state->bridge_prepare_sleep_runtime ||
        !state->bridge_invoke_function ||
        !state->bridge_take_function_error ||
        !state->bridge_unload_loadables ||
        !state->bridge_set_native_state ||
        !state->bridge_sync_variables ||
        !state->bridge_snapshot_variables ||
        !state->bridge_make_array ||
        !state->bridge_make_hash ||
        !state->bridge_has_predicate || !state->bridge_decide ||
        !state->bridge_has_operator || !state->bridge_operate ||
        !state->bridge_bind_environment ||
        !state->bridge_wrap_slp_closure ||
        !state->bridge_slp_closure_handle ||
        !state->bridge_is_sleep_closure ||
        !state->bridge_invoke_sleep_closure ||
        !state->array_value_identity ||
        !state->array_value_elements ||
        !state->hash_value_identity ||
        !state->hash_value_entries ||
        !state->object_get_class ||
        !state->object_to_string || !state->class_get_name ||
        !state->boolean_value_of || !state->boolean_value ||
        !state->double_value_of || !state->long_value_of ||
        !state->number_double_value || !state->number_long_value ||
        (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        set_error(error, "Unable to initialize Java bridge methods: %s", "JNI lookup failed");
        return false;
    }

    JNINativeMethod native_method = {
        "invokeSlpCallableNative",
        "(JJLjava/lang/String;[Ljava/lang/Object;)Ljava/lang/Object;",
        (void*)invoke_slp_callable_native};
    if ((*env)->RegisterNatives(
            env, state->bridge_class,
            &native_method, 1) != JNI_OK ||
        (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        set_error(
            error, "Unable to initialize Java bridge methods: %s",
            "native callback registration failed");
        return false;
    }

    jobject local_bridge = (*env)->NewObject(
        env, state->bridge_class, bridge_init);
    if (!local_bridge || (*env)->ExceptionCheck(env)) {
        char detail[512];
        if (!exception_message(
                state, env, "Bridge construction",
                detail, sizeof(detail)))
            snprintf(detail, sizeof(detail), "%s", "object creation failed");
        set_error(error, "Unable to initialize Java bridge: %s", detail);
        return false;
    }
    state->bridge = (*env)->NewGlobalRef(env, local_bridge);
    (*env)->DeleteLocalRef(env, local_bridge);
    if (!state->bridge) {
        set_error(error, "Unable to initialize Java bridge: %s", "global reference failed");
        return false;
    }
    (*env)->CallVoidMethod(
        env, state->bridge, state->bridge_set_native_state,
        (jlong)(intptr_t)state);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        set_error(
            error, "Unable to initialize Java bridge: %s",
            "native state registration failed");
        return false;
    }
    return true;
}

static void delete_global_classes(
    SlpJvmState *state, JNIEnv *env) {
    jobject references[] = {
        state->bridge,
        state->bridge_class,
        state->object_class,
        state->string_class,
        state->number_class,
        state->boolean_class,
        state->character_class,
        state->double_class,
        state->long_class,
        state->class_class,
        state->object_array_class,
        state->array_value_class,
        state->hash_value_class
    };
    size_t count = sizeof(references) / sizeof(references[0]);
    for (size_t index = 0; index < count; index++) {
        if (references[index])
            (*env)->DeleteGlobalRef(env, references[index]);
    }
    state->bridge = NULL;
    state->bridge_class = NULL;
}

static SlpValue dereference_slp_value(SlpValue value) {
    while (SLP_IS_OBJ(value) &&
           SLP_OBJ_TYPE(value) == SLP_OBJ_SCALAR_CELL)
        value = SLP_AS_SCALAR_CELL(value)->value;
    return slp_value_unwrap_taint(value);
}

static jobject resolve_class_object(
    SlpJvmState *state, JNIEnv *env,
    const char *name) {
    jstring java_name = (*env)->NewStringUTF(env, name);
    if (!java_name) return NULL;
    jobject result = (*env)->CallObjectMethod(
        env, state->bridge, state->bridge_resolve,
        java_name);
    (*env)->DeleteLocalRef(env, java_name);
    return result;
}

static jobject to_java_value(
    SlpVM *vm, SlpJvmState *state,
    JNIEnv *env, SlpValue value, bool *ok);

static SlpJvmCallableHandle *retain_callable(
    SlpVM *vm, SlpJvmState *state, SlpValue value) {
    value = dereference_slp_value(value);
    for (SlpJvmCallableHandle *handle = state->callables;
         handle; handle = handle->next) {
        if (slp_value_identity_equals(handle->value, value))
            return handle;
    }
    SlpCallable *callable = NULL;
    if (slp_callable_acquire(
            vm, value, &callable) != SLP_OK)
        return NULL;
    SlpJvmCallableHandle *handle =
        (SlpJvmCallableHandle*)jvm_alloc(
            vm->allocator, sizeof(*handle));
    if (!handle) {
        slp_callable_release(callable);
        return NULL;
    }
    handle->state = state;
    handle->callable = callable;
    handle->value = value;
    handle->next = state->callables;
    state->callables = handle;
    return handle;
}

static SlpJvmCallableHandle *find_callable(
    SlpJvmState *state, jlong pointer) {
    SlpJvmCallableHandle *target =
        (SlpJvmCallableHandle*)(intptr_t)pointer;
    for (SlpJvmCallableHandle *handle = state->callables;
         handle; handle = handle->next)
        if (handle == target)
            return handle;
    return NULL;
}

static jobjectArray to_java_arguments(
    SlpVM *vm, SlpJvmState *state,
    JNIEnv *env, const SlpValue *arguments,
    int argument_count) {
    jobjectArray result = (*env)->NewObjectArray(
        env, argument_count, state->object_class, NULL);
    if (!result) return NULL;
    for (int index = 0; index < argument_count; index++) {
        bool ok = false;
        jobject argument = to_java_value(
            vm, state, env, arguments[index], &ok);
        if (!ok) {
            if (argument) (*env)->DeleteLocalRef(env, argument);
            (*env)->DeleteLocalRef(env, result);
            return NULL;
        }
        (*env)->SetObjectArrayElement(env, result, index, argument);
        if (argument) (*env)->DeleteLocalRef(env, argument);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->DeleteLocalRef(env, result);
            return NULL;
        }
    }
    return result;
}

static jobject to_java_value(
    SlpVM *vm, SlpJvmState *state,
    JNIEnv *env, SlpValue value, bool *ok) {
    value = dereference_slp_value(value);
    *ok = true;
    if (SLP_IS_NULL(value)) return NULL;
    if (SLP_IS_BOOL(value))
        return (*env)->CallStaticObjectMethod(
            env, state->boolean_class,
            state->boolean_value_of,
            (jboolean)SLP_AS_BOOL(value));
    if (SLP_IS_NUM(value)) {
        return (*env)->CallStaticObjectMethod(
            env, state->double_class, state->double_value_of,
            (jdouble)SLP_AS_NUM(value));
    }
    if (!SLP_IS_OBJ(value)) {
        *ok = false;
        return NULL;
    }

    switch (SLP_OBJ_TYPE(value)) {
    case SLP_OBJ_STRING:
        return (*env)->NewStringUTF(
            env, SLP_AS_STRING(value)->chars);
    case SLP_OBJ_LONG: {
        return (*env)->CallStaticObjectMethod(
            env, state->long_class, state->long_value_of,
            (jlong)SLP_AS_LONG(value)->value);
    }
    case SLP_OBJ_DOUBLE: {
        return (*env)->CallStaticObjectMethod(
            env, state->double_class, state->double_value_of,
            (jdouble)SLP_AS_DOUBLE(value)->value);
    }
    case SLP_OBJ_JAVA_OBJECT: {
        SlpJvmObjectHandle *handle =
            (SlpJvmObjectHandle*)
                SLP_AS_JAVA_OBJECT(value)->backend_handle;
        if (!handle || !handle->reference) {
            *ok = false;
            return NULL;
        }
        return (*env)->NewLocalRef(env, handle->reference);
    }
    case SLP_OBJ_CLASS:
        return resolve_class_object(
            state, env,
            SLP_AS_CLASS(value)->name->chars);
    case SLP_OBJ_ARRAY: {
        SlpObjArray *array = SLP_AS_ARRAY(value);
        if (array->bridge_identity == 0)
            array->bridge_identity = ++vm->next_bridge_identity;
        jobject shell = (*env)->CallObjectMethod(
            env, state->bridge, state->bridge_make_array,
            (jlong)array->bridge_identity, NULL);
        if (!shell || (*env)->ExceptionCheck(env)) {
            *ok = false;
            return NULL;
        }
        if (array->bridge_serializing)
            return shell;

        /* Publish the carrier before descending so self-referential and
           mutually recursive arrays resolve to the same Java object. */
        array->bridge_serializing = true;
        jobjectArray elements = to_java_arguments(
            vm, state, env, array->elements,
            array->count);
        array->bridge_serializing = false;
        if (!elements) {
            (*env)->DeleteLocalRef(env, shell);
            *ok = false;
            return NULL;
        }
        jobject result = (*env)->CallObjectMethod(
            env, state->bridge, state->bridge_make_array,
            (jlong)array->bridge_identity, elements);
        (*env)->DeleteLocalRef(env, shell);
        (*env)->DeleteLocalRef(env, elements);
        if (!result || (*env)->ExceptionCheck(env)) {
            *ok = false;
            return NULL;
        }
        return result;
    }
    case SLP_OBJ_HASH: {
        SlpObjHash *hash = SLP_AS_HASH(value);
        if (hash->bridge_identity == 0)
            hash->bridge_identity = ++vm->next_bridge_identity;
        jobject shell = (*env)->CallObjectMethod(
            env, state->bridge, state->bridge_make_hash,
            (jlong)hash->bridge_identity, NULL);
        if (!shell || (*env)->ExceptionCheck(env)) {
            *ok = false;
            return NULL;
        }
        if (hash->bridge_serializing)
            return shell;

        /* Hash values may point back to this hash (or to an array which does),
           so cache the empty carrier before recursively converting entries. */
        hash->bridge_serializing = true;
        jobjectArray entries = (*env)->NewObjectArray(
            env, hash->count * 2, state->object_class, NULL);
        if (!entries) {
            hash->bridge_serializing = false;
            (*env)->DeleteLocalRef(env, shell);
            *ok = false;
            return NULL;
        }
        int output = 0;
        for (int index = 0; index < hash->capacity; index++) {
            if (SLP_IS_NULL(hash->entries[index].key)) continue;
            bool key_ok = false;
            bool value_ok = false;
            jobject key = to_java_value(
                vm, state, env, hash->entries[index].key,
                &key_ok);
            jobject element = to_java_value(
                vm, state, env, hash->entries[index].value,
                &value_ok);
            if (!key_ok || !value_ok) {
                if (key) (*env)->DeleteLocalRef(env, key);
                if (element) (*env)->DeleteLocalRef(env, element);
                (*env)->DeleteLocalRef(env, entries);
                (*env)->DeleteLocalRef(env, shell);
                hash->bridge_serializing = false;
                *ok = false;
                return NULL;
            }
            (*env)->SetObjectArrayElement(env, entries, output++, key);
            (*env)->SetObjectArrayElement(env, entries, output++, element);
            if (key) (*env)->DeleteLocalRef(env, key);
            if (element) (*env)->DeleteLocalRef(env, element);
        }
        hash->bridge_serializing = false;
        jobject result = (*env)->CallObjectMethod(
            env, state->bridge, state->bridge_make_hash,
            (jlong)hash->bridge_identity, entries);
        (*env)->DeleteLocalRef(env, shell);
        (*env)->DeleteLocalRef(env, entries);
        if (!result || (*env)->ExceptionCheck(env)) {
            *ok = false;
            return NULL;
        }
        return result;
    }
    case SLP_OBJ_CLOSURE:
    case SLP_OBJ_FUNCTION:
    case SLP_OBJ_NATIVE: {
        SlpJvmCallableHandle *handle =
            retain_callable(vm, state, value);
        if (!handle) {
            *ok = false;
            return NULL;
        }
        jobject result = (*env)->CallObjectMethod(
            env, state->bridge,
            state->bridge_wrap_slp_closure,
            (jlong)(intptr_t)handle);
        if (!result || (*env)->ExceptionCheck(env)) {
            *ok = false;
            return NULL;
        }
        return result;
    }
    default:
        *ok = false;
        return NULL;
    }
}

static bool java_string_to_slp(
    SlpVM *vm, JNIEnv *env,
    jstring string, SlpValue *out_value) {
    const char *utf = (*env)->GetStringUTFChars(
        env, string, NULL);
    if (!utf) return false;
    jsize length = (*env)->GetStringUTFLength(env, string);
    SlpObjString *result = slp_vm_copy_string(
        vm, utf, (uint32_t)length);
    (*env)->ReleaseStringUTFChars(env, string, utf);
    if (!result) return false;
    *out_value = SLP_OBJ_VAL(result);
    return true;
}

static bool class_name(
    SlpJvmState *state, JNIEnv *env,
    jobject value, char *output, size_t capacity) {
    jobject type = (*env)->CallObjectMethod(
        env, value, state->object_get_class);
    if (!type || (*env)->ExceptionCheck(env)) return false;
    jstring name = (jstring)(*env)->CallObjectMethod(
        env, type, state->class_get_name);
    (*env)->DeleteLocalRef(env, type);
    if (!name || (*env)->ExceptionCheck(env)) return false;
    const char *utf = (*env)->GetStringUTFChars(env, name, NULL);
    if (!utf) {
        (*env)->DeleteLocalRef(env, name);
        return false;
    }
    snprintf(output, capacity, "%s", utf);
    (*env)->ReleaseStringUTFChars(env, name, utf);
    (*env)->DeleteLocalRef(env, name);
    return true;
}

static bool from_java_value(
    SlpVM *vm, SlpJvmState *state,
    JNIEnv *env, jobject value, SlpValue *out_value);

static SlpObjArray *find_bridge_array(
    SlpVM *vm, uint64_t identity) {
    for (SlpObj *object = vm->objects; object; object = object->next) {
        if (object->type == SLP_OBJ_ARRAY &&
            ((SlpObjArray*)object)->bridge_identity == identity)
            return (SlpObjArray*)object;
    }
    return NULL;
}

static SlpObjHash *find_bridge_hash(
    SlpVM *vm, uint64_t identity) {
    for (SlpObj *object = vm->objects; object; object = object->next) {
        if (object->type == SLP_OBJ_HASH &&
            ((SlpObjHash*)object)->bridge_identity == identity)
            return (SlpObjHash*)object;
    }
    return NULL;
}

static void accept_bridge_identity(
    SlpVM *vm, jlong identity) {
    if (identity > 0 &&
        (uint64_t)identity > vm->next_bridge_identity)
        vm->next_bridge_identity = (uint64_t)identity;
}

static bool from_java_array_value(
    SlpVM *vm, SlpJvmState *state,
    JNIEnv *env, jobject value, SlpValue *out_value) {
    jlong java_identity = (*env)->GetLongField(
        env, value, state->array_value_identity);
    jobjectArray elements = (jobjectArray)(*env)->GetObjectField(
        env, value, state->array_value_elements);
    if (java_identity == 0 || !elements ||
        (*env)->ExceptionCheck(env)) {
        if (elements) (*env)->DeleteLocalRef(env, elements);
        return false;
    }

    uint64_t identity = (uint64_t)java_identity;
    /* A single identity namespace prevents a malformed carrier from changing
       an established array into a hash (or vice versa). */
    if (find_bridge_hash(vm, identity)) {
        (*env)->DeleteLocalRef(env, elements);
        return false;
    }
    SlpObjArray *array = find_bridge_array(vm, identity);
    if (!array) {
        array = slp_vm_new_array(vm);
        if (!array) {
            (*env)->DeleteLocalRef(env, elements);
            return false;
        }
        array->bridge_identity = identity;
        accept_bridge_identity(vm, java_identity);
    }
    *out_value = SLP_OBJ_VAL(array);
    if (array->bridge_syncing) {
        (*env)->DeleteLocalRef(env, elements);
        return true;
    }

    array->bridge_syncing = true;
    slp_vm_push(vm, *out_value);
    while (array->count > 0)
        slp_obj_array_pop(array);
    jsize count = (*env)->GetArrayLength(env, elements);
    bool converted = true;
    for (jsize index = 0; index < count; index++) {
        jobject element = (*env)->GetObjectArrayElement(
            env, elements, index);
        SlpValue item = SLP_NULL_VAL;
        converted = from_java_value(
            vm, state, env, element, &item);
        if (element) (*env)->DeleteLocalRef(env, element);
        if (!converted) break;
        slp_obj_array_push(vm->allocator, array, item);
    }
    slp_vm_pop(vm);
    array->bridge_syncing = false;
    (*env)->DeleteLocalRef(env, elements);
    return converted;
}

static bool from_java_hash_value(
    SlpVM *vm, SlpJvmState *state,
    JNIEnv *env, jobject value, SlpValue *out_value) {
    jlong java_identity = (*env)->GetLongField(
        env, value, state->hash_value_identity);
    jobjectArray entries = (jobjectArray)(*env)->GetObjectField(
        env, value, state->hash_value_entries);
    if (java_identity == 0 || !entries ||
        (*env)->ExceptionCheck(env)) {
        if (entries) (*env)->DeleteLocalRef(env, entries);
        return false;
    }

    uint64_t identity = (uint64_t)java_identity;
    if (find_bridge_array(vm, identity)) {
        (*env)->DeleteLocalRef(env, entries);
        return false;
    }
    SlpObjHash *hash = find_bridge_hash(vm, identity);
    if (!hash) {
        hash = slp_vm_new_hash(vm);
        if (!hash) {
            (*env)->DeleteLocalRef(env, entries);
            return false;
        }
        hash->bridge_identity = identity;
        accept_bridge_identity(vm, java_identity);
    }
    *out_value = SLP_OBJ_VAL(hash);
    if (hash->bridge_syncing) {
        (*env)->DeleteLocalRef(env, entries);
        return true;
    }

    hash->bridge_syncing = true;
    slp_vm_push(vm, *out_value);
    jsize count = (*env)->GetArrayLength(env, entries);
    size_t pair_count = (size_t)count / 2;
    SlpValue *keys = pair_count > 0
        ? (SlpValue*)jvm_alloc(
              vm->allocator, sizeof(*keys) * pair_count)
        : NULL;
    SlpValue *values = pair_count > 0
        ? (SlpValue*)jvm_alloc(
              vm->allocator, sizeof(*values) * pair_count)
        : NULL;
    if (pair_count > 0 && (!keys || !values)) {
        jvm_free(vm->allocator, keys);
        jvm_free(vm->allocator, values);
        slp_vm_pop(vm);
        hash->bridge_syncing = false;
        (*env)->DeleteLocalRef(env, entries);
        return false;
    }
    bool converted = true;
    for (size_t pair = 0; pair < pair_count; pair++) {
        jsize index = (jsize)(pair * 2);
        jobject key_object = (*env)->GetObjectArrayElement(
            env, entries, index);
        jobject value_object = (*env)->GetObjectArrayElement(
            env, entries, index + 1);
        converted = from_java_value(
            vm, state, env, key_object, &keys[pair]);
        if (converted) {
            converted = from_java_value(
                vm, state, env, value_object, &values[pair]);
        }
        if (key_object) (*env)->DeleteLocalRef(env, key_object);
        if (value_object) (*env)->DeleteLocalRef(env, value_object);
        if (!converted) break;
    }
    if (converted) {
        /* Reconcile rather than rebuild: references to existing hash entry
           scalar cells remain valid just like retained Java Scalar values. */
        for (int index = 0; index < hash->capacity; index++) {
            SlpValue old_key = hash->entries[index].key;
            if (SLP_IS_NULL(old_key)) continue;
            bool found = false;
            for (size_t pair = 0; pair < pair_count; pair++) {
                if (slp_vm_hash_contains(vm, hash, keys[pair]) &&
                    slp_value_identity_equals(old_key, keys[pair])) {
                    found = true;
                    break;
                }
            }
            if (!found)
                slp_vm_hash_delete(vm, hash, old_key);
        }
        for (size_t pair = 0; pair < pair_count; pair++) {
            SlpValue existing =
                slp_obj_hash_get_raw(hash, keys[pair]);
            if (SLP_IS_OBJ(existing) &&
                SLP_OBJ_TYPE(existing) == SLP_OBJ_SCALAR_CELL) {
                SLP_AS_SCALAR_CELL(existing)->value = values[pair];
            } else if (!slp_vm_hash_set(
                           vm, hash, keys[pair], values[pair])) {
                converted = false;
                break;
            }
        }
    }
    jvm_free(vm->allocator, keys);
    jvm_free(vm->allocator, values);
    slp_vm_pop(vm);
    hash->bridge_syncing = false;
    (*env)->DeleteLocalRef(env, entries);
    return converted;
}

static bool from_java_value(
    SlpVM *vm, SlpJvmState *state,
    JNIEnv *env, jobject value, SlpValue *out_value) {
    if (!value) {
        *out_value = SLP_NULL_VAL;
        return true;
    }
    jlong native_handle = (*env)->CallLongMethod(
        env, state->bridge,
        state->bridge_slp_closure_handle, value);
    if ((*env)->ExceptionCheck(env)) return false;
    if (native_handle != 0) {
        SlpJvmCallableHandle *handle =
            find_callable(state, native_handle);
        if (!handle || !handle->callable) return false;
        *out_value = handle->value;
        return true;
    }
    if ((*env)->IsInstanceOf(
            env, value, state->array_value_class))
        return from_java_array_value(
            vm, state, env, value, out_value);
    if ((*env)->IsInstanceOf(
            env, value, state->hash_value_class))
        return from_java_hash_value(
            vm, state, env, value, out_value);
    if ((*env)->IsInstanceOf(env, value, state->string_class))
        return java_string_to_slp(
            vm, env, (jstring)value, out_value);
    if ((*env)->IsInstanceOf(env, value, state->boolean_class)) {
        jboolean boolean_value = (*env)->CallBooleanMethod(
            env, value, state->boolean_value);
        *out_value = SLP_BOOL_VAL(boolean_value == JNI_TRUE);
        return !(*env)->ExceptionCheck(env);
    }
    if ((*env)->IsInstanceOf(env, value, state->character_class)) {
        jstring text = (jstring)(*env)->CallObjectMethod(
            env, value, state->object_to_string);
        if (!text || (*env)->ExceptionCheck(env)) return false;
        bool converted = java_string_to_slp(
            vm, env, text, out_value);
        (*env)->DeleteLocalRef(env, text);
        return converted;
    }
    if ((*env)->IsInstanceOf(env, value, state->long_class)) {
        jlong number = (*env)->CallLongMethod(
            env, value, state->number_long_value);
        SlpObjLong *result =
            slp_vm_new_long(vm, (int64_t)number);
        if (!result) return false;
        *out_value = SLP_OBJ_VAL(result);
        return !(*env)->ExceptionCheck(env);
    }
    if ((*env)->IsInstanceOf(env, value, state->number_class)) {
        jdouble number = (*env)->CallDoubleMethod(
            env, value, state->number_double_value);
        *out_value = SLP_NUM_VAL((double)number);
        return !(*env)->ExceptionCheck(env);
    }
    if ((*env)->IsInstanceOf(env, value, state->class_class)) {
        jstring name = (jstring)(*env)->CallObjectMethod(
            env, value, state->class_get_name);
        if (!name || (*env)->ExceptionCheck(env)) return false;
        const char *utf = (*env)->GetStringUTFChars(env, name, NULL);
        if (!utf) {
            (*env)->DeleteLocalRef(env, name);
            return false;
        }
        SlpObjClass *class_object =
            slp_vm_new_class(vm, utf);
        (*env)->ReleaseStringUTFChars(env, name, utf);
        (*env)->DeleteLocalRef(env, name);
        if (!class_object) return false;
        *out_value = SLP_OBJ_VAL(class_object);
        return true;
    }
    if ((*env)->IsInstanceOf(env, value, state->object_array_class)) {
        jsize count = (*env)->GetArrayLength(
            env, (jarray)value);
        SlpObjArray *array = slp_vm_new_array(vm);
        if (!array) return false;
        slp_vm_push(vm, SLP_OBJ_VAL(array));
        for (jsize index = 0; index < count; index++) {
            jobject element = (*env)->GetObjectArrayElement(
                env, (jobjectArray)value, index);
            SlpValue converted = SLP_NULL_VAL;
            bool ok = from_java_value(
                vm, state, env, element, &converted);
            if (element) (*env)->DeleteLocalRef(env, element);
            if (!ok) {
                slp_vm_pop(vm);
                return false;
            }
            slp_obj_array_push(
                vm->allocator, array, converted);
        }
        slp_vm_pop(vm);
        *out_value = SLP_OBJ_VAL(array);
        return true;
    }

    char type_name[512];
    if (!class_name(
            state, env, value,
            type_name, sizeof(type_name)))
        return false;
    SlpJvmObjectHandle *handle =
        (SlpJvmObjectHandle*)jvm_alloc(
            vm->allocator, sizeof(*handle));
    if (!handle) return false;
    handle->reference = (*env)->NewGlobalRef(env, value);
    if (!handle->reference) {
        jvm_free(vm->allocator, handle);
        return false;
    }
    SlpObjJavaObject *object =
        slp_vm_new_java_reference(
            vm, type_name, SLP_JAVA_GENERIC,
            handle);
    if (!object) return false;
    *out_value = SLP_OBJ_VAL(object);
    return true;
}

static bool is_global_variable_key(SlpValue key) {
    if (!SLP_IS_OBJ(key) ||
        SLP_OBJ_TYPE(key) != SLP_OBJ_STRING)
        return false;
    SlpObjString *name = SLP_AS_STRING(key);
    return name->length > 0 &&
        (name->chars[0] == '$' || name->chars[0] == '@' ||
         name->chars[0] == '%');
}

static bool make_variable_arrays(
    SlpVM *vm, SlpJvmState *state, JNIEnv *env,
    jobjectArray *out_names, jobjectArray *out_values) {
    int count = 0;
    for (int index = 0; index < vm->globals->capacity; index++)
        if (is_global_variable_key(
                vm->globals->entries[index].key))
            count++;

    jobjectArray names = (*env)->NewObjectArray(
        env, count, state->string_class, NULL);
    jobjectArray values = (*env)->NewObjectArray(
        env, count, state->object_class, NULL);
    if (!names || !values) {
        if (names) (*env)->DeleteLocalRef(env, names);
        if (values) (*env)->DeleteLocalRef(env, values);
        return false;
    }
    int output = 0;
    for (int index = 0; index < vm->globals->capacity; index++) {
        SlpValue key = vm->globals->entries[index].key;
        if (!is_global_variable_key(key)) continue;
        jstring name = (*env)->NewStringUTF(
            env, SLP_AS_STRING(key)->chars);
        bool converted = false;
        jobject value = to_java_value(
            vm, state, env,
            vm->globals->entries[index].value,
            &converted);
        if (!name || !converted || (*env)->ExceptionCheck(env)) {
            if (name) (*env)->DeleteLocalRef(env, name);
            if (value) (*env)->DeleteLocalRef(env, value);
            (*env)->DeleteLocalRef(env, names);
            (*env)->DeleteLocalRef(env, values);
            return false;
        }
        (*env)->SetObjectArrayElement(env, names, output, name);
        (*env)->SetObjectArrayElement(env, values, output, value);
        (*env)->DeleteLocalRef(env, name);
        if (value) (*env)->DeleteLocalRef(env, value);
        output++;
    }
    *out_names = names;
    *out_values = values;
    return true;
}

static bool sync_variables_to_java(
    SlpVM *vm, SlpJvmState *state, JNIEnv *env) {
    jobjectArray names = NULL;
    jobjectArray values = NULL;
    if (!make_variable_arrays(
            vm, state, env, &names, &values))
        return false;
    (*env)->CallVoidMethod(
        env, state->bridge, state->bridge_sync_variables,
        names, values);
    (*env)->DeleteLocalRef(env, names);
    (*env)->DeleteLocalRef(env, values);
    return !(*env)->ExceptionCheck(env);
}

static bool sync_variables_from_java(
    SlpVM *vm, SlpJvmState *state, JNIEnv *env) {
    jobjectArray snapshot = (jobjectArray)(*env)->CallObjectMethod(
        env, state->bridge,
        state->bridge_snapshot_variables);
    if (!snapshot || (*env)->ExceptionCheck(env))
        return false;
    jsize count = (*env)->GetArrayLength(env, snapshot);
    int pair_count = (int)(count / 2);
    SlpValue *names = pair_count > 0
        ? (SlpValue*)jvm_alloc(
              vm->allocator,
              sizeof(*names) * (size_t)pair_count)
        : NULL;
    SlpValue *values = pair_count > 0
        ? (SlpValue*)jvm_alloc(
              vm->allocator,
              sizeof(*values) * (size_t)pair_count)
        : NULL;
    if (pair_count > 0 && (!names || !values)) {
        jvm_free(vm->allocator, names);
        jvm_free(vm->allocator, values);
        (*env)->DeleteLocalRef(env, snapshot);
        return false;
    }
    bool converted = true;
    for (int pair = 0; pair < pair_count; pair++) {
        jobject name = (*env)->GetObjectArrayElement(
            env, snapshot, (jsize)(pair * 2));
        jobject value = (*env)->GetObjectArrayElement(
            env, snapshot, (jsize)(pair * 2 + 1));
        converted = from_java_value(
                        vm, state, env, name, &names[pair]) &&
                    from_java_value(
                        vm, state, env, value, &values[pair]);
        if (name) (*env)->DeleteLocalRef(env, name);
        if (value) (*env)->DeleteLocalRef(env, value);
        if (!converted) break;
    }
    (*env)->DeleteLocalRef(env, snapshot);
    if (!converted) {
        jvm_free(vm->allocator, names);
        jvm_free(vm->allocator, values);
        return false;
    }

    for (int index = 0; index < vm->globals->capacity; index++) {
        SlpValue key = vm->globals->entries[index].key;
        if (!is_global_variable_key(key)) continue;
        const char *existing = SLP_AS_STRING(key)->chars;
        bool found = false;
        for (int pair = 0; pair < pair_count; pair++) {
            if (SLP_IS_OBJ(names[pair]) &&
                SLP_OBJ_TYPE(names[pair]) == SLP_OBJ_STRING &&
                strcmp(
                    existing,
                    SLP_AS_STRING(names[pair])->chars) == 0) {
                found = true;
                break;
            }
        }
        if (!found)
            slp_vm_delete_global_value(vm, existing);
    }
    for (int pair = 0; pair < pair_count; pair++) {
        if (!SLP_IS_OBJ(names[pair]) ||
            SLP_OBJ_TYPE(names[pair]) != SLP_OBJ_STRING) {
            converted = false;
            break;
        }
        converted = slp_vm_set_global_value(
                        vm, SLP_AS_STRING(names[pair])->chars,
                        values[pair]) && converted;
    }
    jvm_free(vm->allocator, names);
    jvm_free(vm->allocator, values);
    return converted;
}

static SlpJavaBackendResult backend_import(
    SlpVM *vm, const char *target,
    const char *source_path, void *userdata) {
    SlpJvmState *state = (SlpJvmState*)userdata;
    bool attached_here;
    JNIEnv *env = current_env(state, &attached_here);
    if (!env) return SLP_JAVA_FAILED;
    jstring java_target = (*env)->NewStringUTF(env, target);
    jstring java_path = source_path
        ? (*env)->NewStringUTF(env, source_path)
        : NULL;
    if (!java_target || (source_path && !java_path)) {
        if (java_target) (*env)->DeleteLocalRef(env, java_target);
        if (java_path) (*env)->DeleteLocalRef(env, java_path);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    (*env)->CallVoidMethod(
        env, state->bridge, state->bridge_import,
        java_target, java_path);
    (*env)->DeleteLocalRef(env, java_target);
    if (java_path) (*env)->DeleteLocalRef(env, java_path);
    bool failed = warn_exception(
        vm, state, env, "Java import failed");
    detach_if_needed(state, attached_here);
    return failed ? SLP_JAVA_FAILED : SLP_JAVA_HANDLED;
}

static SlpJavaBackendResult backend_resolve(
    SlpVM *vm, const char *name,
    SlpValue *out_class, void *userdata) {
    SlpJvmState *state = (SlpJvmState*)userdata;
    bool attached_here;
    JNIEnv *env = current_env(state, &attached_here);
    if (!env) return SLP_JAVA_FAILED;
    jobject resolved = resolve_class_object(state, env, name);
    if (warn_exception(
            vm, state, env, "Java class resolution failed")) {
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    if (!resolved) {
        char message[640];
        snprintf(
            message, sizeof(message),
            "Java class not found: %s", name);
        slp_vm_warning(vm, message);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    jstring qualified_name =
        (jstring)(*env)->CallObjectMethod(
            env, resolved,
            state->class_get_name);
    if (!qualified_name || warn_exception(
            vm, state, env,
            "Java class name lookup failed")) {
        if (qualified_name)
            (*env)->DeleteLocalRef(env, qualified_name);
        (*env)->DeleteLocalRef(env, resolved);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    const char *qualified_utf =
        (*env)->GetStringUTFChars(
            env, qualified_name, NULL);
    if (!qualified_utf) {
        (*env)->DeleteLocalRef(env, qualified_name);
        (*env)->DeleteLocalRef(env, resolved);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    SlpObjClass *class_object =
        slp_vm_new_class(vm, qualified_utf);
    (*env)->ReleaseStringUTFChars(
        env, qualified_name, qualified_utf);
    (*env)->DeleteLocalRef(env, qualified_name);
    (*env)->DeleteLocalRef(env, resolved);
    detach_if_needed(state, attached_here);
    if (!class_object) return SLP_JAVA_FAILED;
    *out_class = SLP_OBJ_VAL(class_object);
    return SLP_JAVA_HANDLED;
}

static SlpJavaBackendResult backend_invoke(
    SlpVM *vm, SlpValue target,
    const char *message,
    const SlpValue *arguments, int argument_count,
    SlpValue *out_result, void *userdata) {
    SlpJvmState *state = (SlpJvmState*)userdata;
    bool constructor = SLP_IS_NULL(target);
    target = dereference_slp_value(target);
    bool target_is_class =
        SLP_IS_OBJ(target) &&
        SLP_OBJ_TYPE(target) == SLP_OBJ_CLASS;
    bool target_is_java =
        SLP_IS_OBJ(target) &&
        SLP_OBJ_TYPE(target) == SLP_OBJ_JAVA_OBJECT &&
        SLP_AS_JAVA_OBJECT(target)->backend_handle;
    if (!constructor && !target_is_class && !target_is_java)
        return SLP_JAVA_UNHANDLED;

    bool attached_here;
    JNIEnv *env = current_env(state, &attached_here);
    if (!env) return SLP_JAVA_FAILED;
    jobject java_target = NULL;
    if (constructor) {
        jobject resolved = resolve_class_object(
            state, env, message);
        if ((*env)->ExceptionCheck(env)) {
            warn_exception(
                vm, state, env,
                "Java constructor resolution failed");
            detach_if_needed(state, attached_here);
            return SLP_JAVA_FAILED;
        }
        if (!resolved) {
            detach_if_needed(state, attached_here);
            return SLP_JAVA_UNHANDLED;
        }
        (*env)->DeleteLocalRef(env, resolved);
    } else {
        bool conversion_ok = false;
        java_target = to_java_value(
            vm, state, env, target,
            &conversion_ok);
        if (!conversion_ok || !java_target) {
            if (java_target)
                (*env)->DeleteLocalRef(env, java_target);
            detach_if_needed(state, attached_here);
            return SLP_JAVA_FAILED;
        }
    }

    jobjectArray java_arguments = to_java_arguments(
        vm, state, env, arguments, argument_count);
    if (!java_arguments || (*env)->ExceptionCheck(env)) {
        if (!warn_exception(
                vm, state, env,
                "Unable to convert Java arguments"))
            slp_vm_warning(
                vm,
                "Unable to convert an SLP value to a Java argument");
        if (java_target)
            (*env)->DeleteLocalRef(env, java_target);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    jstring java_message = (*env)->NewStringUTF(env, message);
    if (!java_message) {
        (*env)->DeleteLocalRef(env, java_arguments);
        if (java_target)
            (*env)->DeleteLocalRef(env, java_target);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }

    jobject result;
    if (constructor) {
        result = (*env)->CallObjectMethod(
            env, state->bridge,
            state->bridge_construct,
            java_message, java_arguments);
    } else {
        result = (*env)->CallObjectMethod(
            env, state->bridge,
            state->bridge_invoke,
            java_target, java_message,
            java_arguments);
    }
    (*env)->DeleteLocalRef(env, java_message);
    (*env)->DeleteLocalRef(env, java_arguments);
    if (java_target)
        (*env)->DeleteLocalRef(env, java_target);
    if (warn_exception(
            vm, state, env,
            constructor
                ? "Java construction failed"
                : "Java invocation failed")) {
        if (result) (*env)->DeleteLocalRef(env, result);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }

    bool converted = from_java_value(
        vm, state, env, result, out_result);
    if (result) (*env)->DeleteLocalRef(env, result);
    if (!converted) {
        if (!warn_exception(
                vm, state, env,
                "Unable to convert Java result"))
            slp_vm_warning(
                vm,
                "Unable to convert the Java result to an SLP value");
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    detach_if_needed(state, attached_here);
    return SLP_JAVA_HANDLED;
}

static SlpValue backend_named_function(
    SlpVM *vm, const char *name,
    SlpValue *arguments, int argument_count,
    void *userdata) {
    SlpJvmState *state = (SlpJvmState*)userdata;
    bool attached_here;
    JNIEnv *env = current_env(state, &attached_here);
    if (!env) {
        slp_vm_flag_error(
            vm,
            slp_vm_new_error(
                vm, "java.lang.IllegalStateException",
                "unable to attach the JVM thread",
                "unable to attach the JVM thread", NULL));
        return SLP_NULL_VAL;
    }

    if (!sync_variables_to_java(vm, state, env)) {
        if (!flag_exception(
                vm, state, env,
                "Unable to synchronize Sleep variables"))
            slp_vm_flag_error(
                vm, slp_vm_new_error(
                        vm, "java.lang.IllegalStateException",
                        "unable to synchronize Sleep variables",
                        "unable to synchronize Sleep variables", NULL));
        detach_if_needed(state, attached_here);
        return SLP_NULL_VAL;
    }

    jobjectArray java_arguments = to_java_arguments(
        vm, state, env, arguments, argument_count);
    jstring java_name = (*env)->NewStringUTF(env, name);
    if (!java_arguments || !java_name ||
        (*env)->ExceptionCheck(env)) {
        if (!flag_exception(
                vm, state, env,
                "Unable to convert Loadable function arguments"))
            slp_vm_flag_error(
                vm,
                slp_vm_new_error(
                    vm, "java.lang.IllegalArgumentException",
                    "unable to convert an SLP function argument",
                    "unable to convert an SLP function argument", NULL));
        if (java_arguments)
            (*env)->DeleteLocalRef(env, java_arguments);
        if (java_name)
            (*env)->DeleteLocalRef(env, java_name);
        detach_if_needed(state, attached_here);
        return SLP_NULL_VAL;
    }

    jobject result = (*env)->CallObjectMethod(
        env, state->bridge,
        state->bridge_invoke_function,
        java_name, java_arguments);
    (*env)->DeleteLocalRef(env, java_name);
    (*env)->DeleteLocalRef(env, java_arguments);
    if (flag_exception(
            vm, state, env,
            "Sleep Loadable function failed")) {
        if (result) (*env)->DeleteLocalRef(env, result);
        detach_if_needed(state, attached_here);
        return SLP_NULL_VAL;
    }

    SlpValue converted = SLP_NULL_VAL;
    if (!from_java_value(
            vm, state, env, result, &converted)) {
        if (!flag_exception(
                vm, state, env,
                "Unable to convert Loadable function result"))
            slp_vm_flag_error(
                vm,
                slp_vm_new_error(
                    vm, "java.lang.IllegalArgumentException",
                    "unable to convert a Loadable function result",
                    "unable to convert a Loadable function result", NULL));
        if (result) (*env)->DeleteLocalRef(env, result);
        detach_if_needed(state, attached_here);
        return SLP_NULL_VAL;
    }
    if (result) (*env)->DeleteLocalRef(env, result);

    /* Keep the return value rooted while translating the destructive
       ScriptEnvironment.checkError slot. */
    slp_vm_push(vm, converted);
    jobject java_error = (*env)->CallObjectMethod(
        env, state->bridge,
        state->bridge_take_function_error);
    if (flag_exception(
            vm, state, env,
            "Unable to retrieve Loadable function error")) {
        slp_vm_pop(vm);
        if (java_error) (*env)->DeleteLocalRef(env, java_error);
        detach_if_needed(state, attached_here);
        return SLP_NULL_VAL;
    }
    if (java_error) {
        SlpValue error_value = SLP_NULL_VAL;
        if (from_java_value(
                vm, state, env,
                java_error, &error_value))
            slp_vm_flag_error(vm, error_value);
        else if (!flag_exception(
                     vm, state, env,
                     "Unable to convert Loadable function error"))
            slp_vm_flag_error(
                vm,
                slp_vm_new_error(
                    vm, "java.lang.RuntimeException",
                    "Loadable function flagged an unreadable error",
                    "Loadable function flagged an unreadable error", NULL));
        (*env)->DeleteLocalRef(env, java_error);
    }
    if (!sync_variables_from_java(vm, state, env)) {
        if (!flag_exception(
                vm, state, env,
                "Unable to synchronize SLP variables"))
            slp_vm_flag_error(
                vm, slp_vm_new_error(
                        vm, "java.lang.IllegalStateException",
                        "unable to synchronize SLP variables",
                        "unable to synchronize SLP variables", NULL));
    }
    converted = slp_vm_pop(vm);
    detach_if_needed(state, attached_here);
    return converted;
}

static SlpJavaBackendResult backend_decide(
    SlpVM *vm, const char *name,
    const SlpValue *terms, int term_count,
    bool *out_result, void *userdata) {
    SlpJvmState *state = (SlpJvmState*)userdata;
    bool attached_here;
    JNIEnv *env = current_env(state, &attached_here);
    if (!env) return SLP_JAVA_FAILED;
    jstring java_name = (*env)->NewStringUTF(env, name);
    if (!java_name) {
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    jboolean registered = (*env)->CallBooleanMethod(
        env, state->bridge, state->bridge_has_predicate,
        java_name);
    if ((*env)->ExceptionCheck(env)) {
        flag_exception(vm, state, env, "Sleep Predicate lookup failed");
        (*env)->DeleteLocalRef(env, java_name);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    if (registered != JNI_TRUE) {
        (*env)->DeleteLocalRef(env, java_name);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_UNHANDLED;
    }
    jobjectArray java_terms = NULL;
    if (!sync_variables_to_java(vm, state, env) ||
        !(java_terms = to_java_arguments(
              vm, state, env, terms, term_count))) {
        flag_exception(vm, state, env, "Sleep Predicate setup failed");
        (*env)->DeleteLocalRef(env, java_name);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    jobject result = (*env)->CallObjectMethod(
        env, state->bridge, state->bridge_decide,
        java_name, java_terms);
    (*env)->DeleteLocalRef(env, java_name);
    (*env)->DeleteLocalRef(env, java_terms);
    if (!result || flag_exception(
            vm, state, env, "Sleep Predicate failed")) {
        if (result) (*env)->DeleteLocalRef(env, result);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    *out_result = (*env)->CallBooleanMethod(
        env, result, state->boolean_value) == JNI_TRUE;
    (*env)->DeleteLocalRef(env, result);
    bool synchronized = sync_variables_from_java(vm, state, env);
    if (!synchronized || flag_exception(
            vm, state, env, "Sleep Predicate synchronization failed")) {
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    detach_if_needed(state, attached_here);
    return SLP_JAVA_HANDLED;
}

static SlpJavaBackendResult backend_operate(
    SlpVM *vm, const char *name,
    const SlpValue *terms, int term_count,
    SlpValue *out_result, void *userdata) {
    SlpJvmState *state = (SlpJvmState*)userdata;
    bool attached_here;
    JNIEnv *env = current_env(state, &attached_here);
    if (!env) return SLP_JAVA_FAILED;
    jstring java_name = (*env)->NewStringUTF(env, name);
    if (!java_name) {
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    jboolean registered = (*env)->CallBooleanMethod(
        env, state->bridge, state->bridge_has_operator,
        java_name);
    if ((*env)->ExceptionCheck(env)) {
        flag_exception(vm, state, env, "Sleep Operator lookup failed");
        (*env)->DeleteLocalRef(env, java_name);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    if (registered != JNI_TRUE) {
        (*env)->DeleteLocalRef(env, java_name);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_UNHANDLED;
    }
    jobjectArray java_terms = NULL;
    if (!sync_variables_to_java(vm, state, env) ||
        !(java_terms = to_java_arguments(
              vm, state, env, terms, term_count))) {
        flag_exception(vm, state, env, "Sleep Operator setup failed");
        (*env)->DeleteLocalRef(env, java_name);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    jobject result = (*env)->CallObjectMethod(
        env, state->bridge, state->bridge_operate,
        java_name, java_terms);
    (*env)->DeleteLocalRef(env, java_name);
    (*env)->DeleteLocalRef(env, java_terms);
    if (flag_exception(vm, state, env, "Sleep Operator failed") ||
        !from_java_value(vm, state, env, result, out_result)) {
        if (result) (*env)->DeleteLocalRef(env, result);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    if (result) (*env)->DeleteLocalRef(env, result);
    bool synchronized = sync_variables_from_java(vm, state, env);
    if (!synchronized || flag_exception(
            vm, state, env, "Sleep Operator synchronization failed")) {
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    detach_if_needed(state, attached_here);
    return SLP_JAVA_HANDLED;
}

static SlpJavaBackendResult backend_call_callable(
    SlpVM *vm, SlpValue callable, const char *message,
    const SlpValue *arguments, int argument_count,
    SlpValue *out_result, void *userdata) {
    callable = dereference_slp_value(callable);
    if (!SLP_IS_OBJ(callable) ||
        SLP_OBJ_TYPE(callable) != SLP_OBJ_JAVA_OBJECT ||
        !SLP_AS_JAVA_OBJECT(callable)->backend_handle)
        return SLP_JAVA_UNHANDLED;
    SlpJvmState *state = (SlpJvmState*)userdata;
    bool attached_here;
    JNIEnv *env = current_env(state, &attached_here);
    if (!env) return SLP_JAVA_FAILED;
    bool target_ok = false;
    jobject target = to_java_value(
        vm, state, env, callable, &target_ok);
    if (!target_ok || !target) {
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    jboolean is_closure = (*env)->CallBooleanMethod(
        env, state->bridge,
        state->bridge_is_sleep_closure, target);
    if ((*env)->ExceptionCheck(env) || is_closure != JNI_TRUE) {
        if ((*env)->ExceptionCheck(env))
            flag_exception(vm, state, env, "SleepClosure lookup failed");
        (*env)->DeleteLocalRef(env, target);
        detach_if_needed(state, attached_here);
        return is_closure == JNI_TRUE
            ? SLP_JAVA_FAILED
            : SLP_JAVA_UNHANDLED;
    }
    jobjectArray java_arguments = NULL;
    jstring java_message = NULL;
    if (!sync_variables_to_java(vm, state, env) ||
        !(java_arguments = to_java_arguments(
              vm, state, env, arguments, argument_count)) ||
        !(java_message = (*env)->NewStringUTF(
              env, message ? message : ""))) {
        flag_exception(vm, state, env, "SleepClosure setup failed");
        if (java_arguments) (*env)->DeleteLocalRef(env, java_arguments);
        (*env)->DeleteLocalRef(env, target);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    jobject result = (*env)->CallObjectMethod(
        env, state->bridge,
        state->bridge_invoke_sleep_closure,
        target, java_message, java_arguments);
    (*env)->DeleteLocalRef(env, target);
    (*env)->DeleteLocalRef(env, java_message);
    (*env)->DeleteLocalRef(env, java_arguments);
    if (flag_exception(vm, state, env, "SleepClosure failed") ||
        !from_java_value(vm, state, env, result, out_result)) {
        if (result) (*env)->DeleteLocalRef(env, result);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    if (result) (*env)->DeleteLocalRef(env, result);
    bool synchronized = sync_variables_from_java(vm, state, env);
    if (!synchronized || flag_exception(
            vm, state, env, "SleepClosure synchronization failed")) {
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    detach_if_needed(state, attached_here);
    return SLP_JAVA_HANDLED;
}

static bool install_registrations(
    SlpVM *vm, SlpJvmState *state, JNIEnv *env,
    jobjectArray registrations) {
    if (!registrations) return false;
    jsize count = (*env)->GetArrayLength(env, registrations);
    for (jsize index = 0; index < count; index++) {
        jstring java_name = (jstring)(*env)->GetObjectArrayElement(
            env, registrations, index);
        if (!java_name) continue;
        const char *name = (*env)->GetStringUTFChars(
            env, java_name, NULL);
        if (name && name[0] && name[1] == ':' && name[2]) {
            const char *registered_name = name + 2;
            if (name[0] == 'F') {
                slp_vm_register_named_native(
                    vm, registered_name[0] == '&'
                            ? registered_name + 1
                            : registered_name,
                    backend_named_function, state);
            } else if (name[0] == 'P') {
                slp_vm_register_predicate_name(
                    vm, registered_name);
            } else if (name[0] == 'E' || name[0] == 'R') {
                slp_vm_register_bridge_type(
                    vm, registered_name,
                    backend_environment_handler, state);
            } else if (name[0] == 'Q') {
                slp_vm_register_predicate_bridge_type(
                    vm, registered_name,
                    backend_environment_handler, state);
            }
        }
        if (name)
            (*env)->ReleaseStringUTFChars(
                env, java_name, name);
        (*env)->DeleteLocalRef(env, java_name);
    }
    return !(*env)->ExceptionCheck(env);
}

static void backend_environment_handler(
    SlpVM *vm, const char *keyword,
    const char *identifier, const char *string_arg,
    SlpObjClosure **conditions, size_t condition_count,
    SlpObjClosure *closure,
    void *userdata) {
    SlpJvmState *state = (SlpJvmState*)userdata;
    SlpJvmCallableHandle *handle = retain_callable(
        vm, state, SLP_OBJ_VAL(closure));
    bool attached_here;
    JNIEnv *env = current_env(state, &attached_here);
    jlongArray condition_handles = NULL;
    bool retained_conditions = true;
    if (env && condition_count > 0) {
        condition_handles = (*env)->NewLongArray(
            env, (jsize)condition_count);
        jlong *handles = condition_handles
            ? (*env)->GetLongArrayElements(
                  env, condition_handles, NULL)
            : NULL;
        if (!handles) {
            retained_conditions = false;
        } else {
            for (size_t index = 0;
                 index < condition_count; index++) {
                SlpJvmCallableHandle *condition_handle =
                    retain_callable(
                        vm, state,
                        SLP_OBJ_VAL(conditions[index]));
                if (!condition_handle) {
                    retained_conditions = false;
                    break;
                }
                handles[index] =
                    (jlong)(intptr_t)condition_handle;
            }
            (*env)->ReleaseLongArrayElements(
                env, condition_handles, handles, 0);
        }
    }
    if (!handle || !retained_conditions || !env) {
        if (condition_handles && env)
            (*env)->DeleteLocalRef(env, condition_handles);
        slp_vm_flag_error(
            vm, slp_vm_new_error(
                    vm, "java.lang.IllegalStateException",
                    "unable to retain Sleep environment block",
                    "unable to retain Sleep environment block", NULL));
        return;
    }
    jstring java_keyword = (*env)->NewStringUTF(env, keyword);
    jstring java_identifier = identifier
        ? (*env)->NewStringUTF(env, identifier)
        : NULL;
    jstring java_string = string_arg
        ? (*env)->NewStringUTF(env, string_arg)
        : NULL;
    if (!sync_variables_to_java(vm, state, env) ||
        !java_keyword || (identifier && !java_identifier) ||
        (string_arg && !java_string)) {
        flag_exception(vm, state, env, "Sleep Environment setup failed");
    } else {
        jobjectArray registrations =
            (jobjectArray)(*env)->CallObjectMethod(
            env, state->bridge,
            state->bridge_bind_environment,
            java_keyword, java_identifier, java_string,
            condition_handles,
            (jlong)(intptr_t)handle);
        if (!flag_exception(
                vm, state, env, "Sleep Environment failed") &&
            install_registrations(
                vm, state, env, registrations) &&
            !sync_variables_from_java(vm, state, env))
            flag_exception(
                vm, state, env,
                "Sleep Environment synchronization failed");
        if (registrations)
            (*env)->DeleteLocalRef(env, registrations);
    }
    if (java_keyword) (*env)->DeleteLocalRef(env, java_keyword);
    if (java_identifier) (*env)->DeleteLocalRef(env, java_identifier);
    if (java_string) (*env)->DeleteLocalRef(env, java_string);
    if (condition_handles)
        (*env)->DeleteLocalRef(env, condition_handles);
    detach_if_needed(state, attached_here);
}

static SlpJavaBackendResult backend_load_extension(
    SlpVM *vm, const SlpValue *arguments,
    int argument_count, SlpValue *out_result,
    void *userdata) {
    if (argument_count < 1 || argument_count > 2)
        return SLP_JAVA_UNHANDLED;

    SlpValue class_value = dereference_slp_value(
        arguments[argument_count - 1]);
    const char *class_name = NULL;
    SlpObjString *class_text = NULL;
    if (SLP_IS_OBJ(class_value) &&
        SLP_OBJ_TYPE(class_value) == SLP_OBJ_CLASS) {
        class_name = SLP_AS_CLASS(class_value)->name->chars;
    } else if (argument_count == 2) {
        class_text = slp_vm_stringify(vm, class_value);
        if (class_text) class_name = class_text->chars;
    }
    if (!class_name || !class_name[0]) {
        slp_vm_flag_error(
            vm,
            slp_vm_new_error(
                vm, "java.lang.ClassNotFoundException",
                "use() expected a Java class",
                "use() expected a Java class", NULL));
        return SLP_JAVA_FAILED;
    }

    const char *source_path = NULL;
    SlpObjString *source_text = NULL;
    if (argument_count == 2) {
        source_text = slp_vm_stringify(
            vm, dereference_slp_value(arguments[0]));
        if (!source_text) return SLP_JAVA_FAILED;
        source_path = source_text->chars;
        FILE *source = fopen(source_path, "rb");
        if (!source) {
            char message[2304];
            snprintf(
                message, sizeof(message),
                "could not locate source '%s'",
                source_path);
            slp_vm_flag_error(
                vm,
                slp_vm_new_error(
                    vm, "java.io.FileNotFoundException",
                    message, message, NULL));
            return SLP_JAVA_FAILED;
        }
        fclose(source);
    }

    SlpJvmState *state = (SlpJvmState*)userdata;
    bool attached_here;
    JNIEnv *env = current_env(state, &attached_here);
    if (!env) return SLP_JAVA_FAILED;
    jstring java_source = source_path
        ? (*env)->NewStringUTF(env, source_path)
        : NULL;
    jstring java_class = (*env)->NewStringUTF(env, class_name);
    jstring java_script = (*env)->NewStringUTF(
        env, vm->source_name
            ? vm->source_name
            : "<SLP script>");
    if ((source_path && !java_source) ||
        !java_class || !java_script) {
        if (java_source) (*env)->DeleteLocalRef(env, java_source);
        if (java_class) (*env)->DeleteLocalRef(env, java_class);
        if (java_script) (*env)->DeleteLocalRef(env, java_script);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }

    (*env)->CallVoidMethod(
        env, state->bridge,
        state->bridge_prepare_sleep_runtime,
        java_script);
    if (flag_exception(
            vm, state, env,
            "Sleep runtime initialization failed")) {
        if (java_source) (*env)->DeleteLocalRef(env, java_source);
        (*env)->DeleteLocalRef(env, java_class);
        (*env)->DeleteLocalRef(env, java_script);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    jobjectArray variable_names = NULL;
    jobjectArray variable_values = NULL;
    if (!make_variable_arrays(
            vm, state, env,
            &variable_names, &variable_values)) {
        flag_exception(
            vm, state, env,
            "Sleep variable conversion failed");
        if (java_source) (*env)->DeleteLocalRef(env, java_source);
        (*env)->DeleteLocalRef(env, java_class);
        (*env)->DeleteLocalRef(env, java_script);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }

    jobjectArray registrations =
        (jobjectArray)(*env)->CallObjectMethod(
        env, state->bridge,
        state->bridge_load_loadable,
        java_source, java_class, java_script,
        variable_names, variable_values);
    (*env)->DeleteLocalRef(env, variable_names);
    (*env)->DeleteLocalRef(env, variable_values);
    if (java_source) (*env)->DeleteLocalRef(env, java_source);
    (*env)->DeleteLocalRef(env, java_class);
    (*env)->DeleteLocalRef(env, java_script);
    if (flag_exception(
            vm, state, env,
            "Sleep Loadable installation failed")) {
        if (registrations)
            (*env)->DeleteLocalRef(env, registrations);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    if (!registrations ||
        !sync_variables_from_java(vm, state, env)) {
        if (registrations)
            (*env)->DeleteLocalRef(env, registrations);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    if (!install_registrations(
            vm, state, env, registrations)) {
        (*env)->DeleteLocalRef(env, registrations);
        detach_if_needed(state, attached_here);
        return SLP_JAVA_FAILED;
    }
    (*env)->DeleteLocalRef(env, registrations);
    detach_if_needed(state, attached_here);
    state->sleep_runtime_active = true;
    *out_result = SLP_NULL_VAL;
    return SLP_JAVA_HANDLED;
}

static void throw_native_error(JNIEnv *env, const char *message) {
    if ((*env)->ExceptionCheck(env)) {
        return;
    }
    jclass error_class = (*env)->FindClass(
        env, "java/lang/IllegalStateException");
    if (error_class) {
        (*env)->ThrowNew(
            env, error_class,
            message ? message : "SLP callback failed");
        (*env)->DeleteLocalRef(env, error_class);
    }
}

static jobject JNICALL invoke_slp_callable_native(
    JNIEnv *env, jobject bridge, jlong state_handle,
    jlong callable_handle, jstring message,
    jobjectArray arguments) {
    (void)bridge;
    SlpJvmState *state =
        (SlpJvmState*)(intptr_t)state_handle;
    if (!state || !state->vm) {
        throw_native_error(env, "SLP VM is no longer available");
        return NULL;
    }
    SlpJvmCallableHandle *handle =
        find_callable(state, callable_handle);
    if (!handle || !handle->callable) {
        throw_native_error(env, "SLP callable is no longer available");
        return NULL;
    }
    SlpVM *vm = state->vm;
    if (!sync_variables_from_java(vm, state, env)) {
        throw_native_error(
            env, "unable to synchronize variables before SLP callback");
        return NULL;
    }
    jsize count = arguments
        ? (*env)->GetArrayLength(env, arguments)
        : 0;
    SlpValue *converted = count > 0
        ? (SlpValue*)jvm_alloc(
              vm->allocator,
              sizeof(*converted) * (size_t)count)
        : NULL;
    if (count > 0 && !converted) {
        throw_native_error(env, "out of memory entering SLP callback");
        return NULL;
    }
    bool ok = true;
    for (jsize index = 0; index < count; index++) {
        jobject argument = (*env)->GetObjectArrayElement(
            env, arguments, index);
        ok = from_java_value(
            vm, state, env, argument,
            &converted[index]);
        if (argument) (*env)->DeleteLocalRef(env, argument);
        if (!ok) break;
    }
    const char *message_utf = "";
    if (ok && message) {
        message_utf = (*env)->GetStringUTFChars(
            env, message, NULL);
        ok = message_utf != NULL;
    }
    SlpValue result = SLP_NULL_VAL;
    if (ok)
        ok = slp_callable_call_message(
                 handle->callable, message_utf,
                 converted, (int)count, &result) == SLP_OK;
    if (message && message_utf)
        (*env)->ReleaseStringUTFChars(
            env, message, message_utf);
    jvm_free(vm->allocator, converted);
    if (!ok || !sync_variables_to_java(vm, state, env)) {
        throw_native_error(env, "SLP callable execution failed");
        return NULL;
    }
    bool result_ok = false;
    jobject java_result = to_java_value(
        vm, state, env, result, &result_ok);
    if (!result_ok) {
        if (java_result) (*env)->DeleteLocalRef(env, java_result);
        throw_native_error(
            env, "unable to convert the SLP callback result");
        return NULL;
    }
    return java_result;
}

static void release_callables(
    SlpVM *vm, SlpJvmState *state) {
    SlpJvmCallableHandle *handle = state->callables;
    state->callables = NULL;
    while (handle) {
        SlpJvmCallableHandle *next = handle->next;
        if (handle->callable)
            slp_callable_release(handle->callable);
        handle->callable = NULL;
        jvm_free(vm->allocator, handle);
        handle = next;
    }
}

static void backend_closing(
    SlpVM *vm, void *userdata) {
    SlpJvmState *state = (SlpJvmState*)userdata;
    bool attached_here;
    JNIEnv *env = current_env(state, &attached_here);
    if (!env) {
        release_callables(vm, state);
        state->vm = NULL;
        return;
    }
    if (state->sleep_runtime_active &&
        !sync_variables_to_java(vm, state, env) &&
        !warn_exception(
            vm, state, env,
            "Sleep variable synchronization before unload failed"))
        slp_vm_warning(
            vm,
            "Unable to synchronize Sleep variables before unload");
    (*env)->CallVoidMethod(
        env, state->bridge,
        state->bridge_unload_loadables);
    warn_exception(
        vm, state, env,
        "Sleep Loadable unload failed");
    release_callables(vm, state);
    (*env)->CallVoidMethod(
        env, state->bridge,
        state->bridge_set_native_state, (jlong)0);
    state->vm = NULL;
    detach_if_needed(state, attached_here);
}

static void backend_release(
    SlpVM *vm, void *object_handle,
    void *userdata) {
    SlpJvmState *state = (SlpJvmState*)userdata;
    SlpJvmObjectHandle *handle =
        (SlpJvmObjectHandle*)object_handle;
    bool attached_here;
    JNIEnv *env = current_env(state, &attached_here);
    if (env && handle->reference)
        (*env)->DeleteGlobalRef(env, handle->reference);
    detach_if_needed(state, attached_here);
    jvm_free(vm->allocator, handle);
}

static void backend_destroy(
    SlpVM *vm, void *userdata) {
    SlpJvmState *state = (SlpJvmState*)userdata;
    if (state->callables)
        release_callables(vm, state);
    bool attached_here;
    JNIEnv *env = current_env(state, &attached_here);
    if (env) delete_global_classes(state, env);
    if (state->owns_jvm) {
        (*state->jvm)->DestroyJavaVM(state->jvm);
    } else {
        detach_if_needed(
            state,
            attached_here || state->attached_thread);
    }
    /* A created JVM is not safely reloadable; retain the loader reference
       until process exit even though the provider state is now gone. */
    jvm_free(vm->allocator, state);
}

static void cleanup_failed_state(
    SlpJvmState *state, JNIEnv *env) {
    if (!state) return;
    if (env) delete_global_classes(state, env);
    if (state->jvm && state->owns_jvm)
        (*state->jvm)->DestroyJavaVM(state->jvm);
    else if (state->jvm && state->attached_thread)
        (*state->jvm)->DetachCurrentThread(state->jvm);
    close_library(state->library);
    jvm_free(state->allocator, state);
}

static char *make_option(
    SlpAllocator *allocator,
    const char *prefix, const char *value,
    const char *extra) {
    size_t length = strlen(prefix) + strlen(value) + 1;
    if (extra) length += strlen(extra);
    char *result = (char*)jvm_alloc(allocator, length);
    if (!result) return NULL;
    snprintf(
        result, length, "%s%s%s",
        prefix, value, extra ? extra : "");
    return result;
}

SlpJvmResult slp_jvm_install(
    SlpAllocator *allocator, SlpVM *vm,
    const SlpJvmOptions *options, SlpJvmError *error) {
    if (error) error->message[0] = '\0';
    if (!allocator || !allocator->reallocate || !vm ||
        vm->allocator != allocator ||
        slp_vm_has_java_backend(vm)) {
        set_error(error, "Invalid JVM installation request: %s", "allocator, VM, or backend state");
        return SLP_JVM_INVALID_ARGUMENT;
    }

    SlpJvmOptions defaults;
    memset(&defaults, 0, sizeof(defaults));
    if (!options) options = &defaults;
    if (options->vm_option_count < 0 ||
        options->vm_option_count > 1024 ||
        (options->vm_option_count > 0 && !options->vm_options)) {
        set_error(error, "Invalid JVM installation request: %s", "VM option array");
        return SLP_JVM_INVALID_ARGUMENT;
    }
    for (int index = 0;
         index < options->vm_option_count;
         index++) {
        if (!options->vm_options[index]) {
            set_error(
                error,
                "Invalid JVM installation request: %s",
                "VM options may not be NULL");
            return SLP_JVM_INVALID_ARGUMENT;
        }
    }

    char runtime_dir[PATH_MAX];
    if (options->runtime_dir) {
        if (snprintf(
                runtime_dir, sizeof(runtime_dir),
                "%s", options->runtime_dir) >=
            (int)sizeof(runtime_dir)) {
            set_error(error, "JVM runtime path is too long: %s", options->runtime_dir);
            return SLP_JVM_INVALID_ARGUMENT;
        }
    } else if (!discover_runtime(
                   runtime_dir,
                   sizeof(runtime_dir))) {
        set_error(error, "Unable to discover bundled runtime: %s", "expected runtime/ beside the executable");
        return SLP_JVM_RUNTIME_NOT_FOUND;
    }

    char bridge_jar[PATH_MAX];
    if (options->bridge_jar) {
        if (snprintf(
                bridge_jar, sizeof(bridge_jar),
                "%s", options->bridge_jar) >=
            (int)sizeof(bridge_jar)) {
            set_error(error, "JVM bridge path is too long: %s", options->bridge_jar);
            return SLP_JVM_INVALID_ARGUMENT;
        }
    } else if (!join_path(
                   bridge_jar, sizeof(bridge_jar),
                   runtime_dir,
                   "lib/slp-jvm-bridge.jar")) {
        set_error(error, "JVM bridge path is too long: %s", runtime_dir);
        return SLP_JVM_INVALID_ARGUMENT;
    }

    char sleep_jar[PATH_MAX];
    if (!join_path(
            sleep_jar, sizeof(sleep_jar),
            runtime_dir, "lib/sleep.jar")) {
        set_error(error, "Sleep compatibility path is too long: %s", runtime_dir);
        return SLP_JVM_INVALID_ARGUMENT;
    }

    SlpJvmState *state =
        (SlpJvmState*)jvm_alloc(
            allocator, sizeof(*state));
    if (!state) return SLP_JVM_OUT_OF_MEMORY;
    memset(state, 0, sizeof(*state));
    state->allocator = allocator;
    state->vm = vm;
    char loaded_path[PATH_MAX];
    state->library = load_jvm_library(
        runtime_dir, loaded_path,
        sizeof(loaded_path), error);
    if (!state->library) {
        jvm_free(allocator, state);
        return SLP_JVM_LIBRARY_ERROR;
    }

    SlpCreateJavaVMFn create_vm = NULL;
    SlpGetCreatedJavaVMsFn get_created_vms = NULL;
    if (!copy_function_pointer(
            load_symbol(state->library, "JNI_CreateJavaVM"),
            &create_vm, sizeof(create_vm)) ||
        !copy_function_pointer(
            load_symbol(state->library, "JNI_GetCreatedJavaVMs"),
            &get_created_vms, sizeof(get_created_vms))) {
        set_error(error, "Bundled JVM is missing invocation API: %s", loaded_path);
        cleanup_failed_state(state, NULL);
        return SLP_JVM_LIBRARY_ERROR;
    }

    JNIEnv *env = NULL;
    JavaVM *existing = NULL;
    jsize existing_count = 0;
    if (get_created_vms(
            &existing, 1, &existing_count) != JNI_OK) {
        set_error(error, "Unable to inspect created JVMs: %s", loaded_path);
        cleanup_failed_state(state, NULL);
        return SLP_JVM_CREATE_ERROR;
    }
    if (existing_count > 0) {
        state->jvm = existing;
        jint status = (*state->jvm)->GetEnv(
            state->jvm, (void**)&env,
            JNI_VERSION_1_8);
        if (status == JNI_EDETACHED) {
            status = (*state->jvm)->AttachCurrentThread(
                state->jvm, (void**)&env, NULL);
            state->attached_thread = status == JNI_OK;
        }
        if (status != JNI_OK) {
            set_error(error, "Unable to attach to existing JVM: %s", loaded_path);
            cleanup_failed_state(state, NULL);
            return SLP_JVM_CREATE_ERROR;
        }
    } else {
        int option_count = options->vm_option_count + 2;
        JavaVMOption *java_options =
            (JavaVMOption*)jvm_alloc(
                allocator,
                sizeof(*java_options) *
                    (size_t)option_count);
        if (!java_options) {
            cleanup_failed_state(state, NULL);
            return SLP_JVM_OUT_OF_MEMORY;
        }
        memset(
            java_options, 0,
            sizeof(*java_options) *
                (size_t)option_count);

        size_t classpath_length =
            strlen(bridge_jar) + strlen(sleep_jar) + 2;
        if (options->classpath)
            classpath_length +=
                strlen(options->classpath) + 1;
        char *classpath =
            (char*)jvm_alloc(
                allocator, classpath_length);
        if (classpath) {
            if (options->classpath)
                snprintf(
                    classpath, classpath_length,
                    "%s%c%s%c%s", bridge_jar,
                    SLP_JVM_PATH_SEPARATOR,
                    sleep_jar,
                    SLP_JVM_PATH_SEPARATOR,
                    options->classpath);
            else
                snprintf(
                    classpath, classpath_length,
                    "%s%c%s", bridge_jar,
                    SLP_JVM_PATH_SEPARATOR,
                    sleep_jar);
        }
        java_options[0].optionString = classpath
            ? make_option(
                  allocator, "-Djava.class.path=",
                  classpath, NULL)
            : NULL;
        java_options[1].optionString = make_option(
            allocator, "-Djava.home=",
            runtime_dir, NULL);
        for (int index = 0;
             index < options->vm_option_count;
             index++) {
            java_options[index + 2].optionString =
                make_option(
                    allocator, "",
                    options->vm_options[index], NULL);
        }
        bool options_ok =
            classpath && java_options[0].optionString &&
            java_options[1].optionString;
        for (int index = 2;
             index < option_count;
             index++)
            options_ok = options_ok &&
                java_options[index].optionString;
        jvm_free(allocator, classpath);
        if (!options_ok) {
            for (int index = 0;
                 index < option_count; index++)
                jvm_free(
                    allocator,
                    java_options[index].optionString);
            jvm_free(allocator, java_options);
            cleanup_failed_state(state, NULL);
            return SLP_JVM_OUT_OF_MEMORY;
        }

        JavaVMInitArgs arguments;
        memset(&arguments, 0, sizeof(arguments));
        arguments.version = JNI_VERSION_1_8;
        arguments.nOptions = option_count;
        arguments.options = java_options;
        arguments.ignoreUnrecognized =
            options->ignore_unrecognized_options
                ? JNI_TRUE : JNI_FALSE;
        jint status = create_vm(
            &state->jvm, (void**)&env,
            &arguments);
        for (int index = 0;
             index < option_count; index++)
            jvm_free(
                allocator,
                java_options[index].optionString);
        jvm_free(allocator, java_options);
        if (status != JNI_OK || !state->jvm || !env) {
            char detail[96];
            snprintf(
                detail, sizeof(detail),
                "JNI_CreateJavaVM returned %d", status);
            set_error(error, "Unable to create bundled JVM: %s", detail);
            cleanup_failed_state(state, NULL);
            return SLP_JVM_CREATE_ERROR;
        }
        state->owns_jvm = true;
    }

    if (!cache_bridge(state, env, error)) {
        cleanup_failed_state(state, env);
        return SLP_JVM_BRIDGE_ERROR;
    }

    SlpJavaBackend backend;
    memset(&backend, 0, sizeof(backend));
    backend.abi_version = SLP_JAVA_BACKEND_ABI_VERSION;
    backend.userdata = state;
    backend.import_class = backend_import;
    backend.resolve_class = backend_resolve;
    backend.invoke = backend_invoke;
    backend.decide = backend_decide;
    backend.operate = backend_operate;
    backend.call_callable = backend_call_callable;
    backend.load_extension = backend_load_extension;
    backend.release_object = backend_release;
    backend.vm_closing = backend_closing;
    backend.destroy = backend_destroy;
    if (!slp_vm_install_java_backend(vm, &backend)) {
        set_error(error, "Unable to install JVM backend: %s", "backend rejected by VM");
        cleanup_failed_state(state, env);
        return SLP_JVM_INVALID_ARGUMENT;
    }
    return SLP_JVM_OK;
}

const char *slp_jvm_result_string(
    SlpAllocator *allocator, SlpJvmResult result) {
    (void)allocator;
    switch (result) {
    case SLP_JVM_OK: return "ok";
    case SLP_JVM_INVALID_ARGUMENT: return "invalid argument";
    case SLP_JVM_RUNTIME_NOT_FOUND: return "runtime not found";
    case SLP_JVM_LIBRARY_ERROR: return "JVM library error";
    case SLP_JVM_CREATE_ERROR: return "JVM creation error";
    case SLP_JVM_BRIDGE_ERROR: return "Java bridge error";
    case SLP_JVM_OUT_OF_MEMORY: return "out of memory";
    default: return "unknown JVM result";
    }
}
