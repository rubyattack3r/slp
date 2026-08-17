#ifndef SLP_JVM_H
#define SLP_JVM_H

#include "slp_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SLP_JVM_OK,
    SLP_JVM_INVALID_ARGUMENT,
    SLP_JVM_RUNTIME_NOT_FOUND,
    SLP_JVM_LIBRARY_ERROR,
    SLP_JVM_CREATE_ERROR,
    SLP_JVM_BRIDGE_ERROR,
    SLP_JVM_OUT_OF_MEMORY
} SlpJvmResult;

typedef struct {
    /* NULL discovers a runtime directory next to the executable. */
    const char *runtime_dir;
    /* NULL uses <runtime_dir>/lib/slp-jvm-bridge.jar. */
    const char *bridge_jar;
    /* Optional platform-separated class path appended after the bridge. */
    const char *classpath;
    const char *const *vm_options;
    int vm_option_count;
    bool ignore_unrecognized_options;
} SlpJvmOptions;

typedef struct {
    char message[512];
} SlpJvmError;

/*
 * Installs a regular JVM as this VM's optional Java provider. libjvm is
 * loaded at runtime, so linking the extension does not create a host-Java
 * requirement. The backend and any JVM it creates live until slp_vm_free().
 */
SlpJvmResult slp_jvm_install(
    SlpAllocator *allocator, SlpVM *vm,
    const SlpJvmOptions *options, SlpJvmError *error);

const char *slp_jvm_result_string(
    SlpAllocator *allocator, SlpJvmResult result);

#ifdef __cplusplus
}
#endif

#endif
