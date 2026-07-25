#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "slp_stdlib.h"
#include "slp_value.h"
#include "slp_vm.h"
#include "slp_platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/wait.h>
#endif

static bool stdlib_path_is_absolute(
    const char *path);

/* Allocator wrappers using VM allocator */
static void *stdlib_alloc(SlpVM *vm, size_t size) {
    return vm->allocator->reallocate(NULL, size, vm->allocator->user_data);
}

static void stdlib_free(SlpVM *vm, void *ptr) {
    vm->allocator->reallocate(ptr, 0, vm->allocator->user_data);
}

static bool stdlib_is_number(SlpValue value) {
    return SLP_IS_NUM(value) ||
           (SLP_IS_OBJ(value) &&
            (SLP_OBJ_TYPE(value) == SLP_OBJ_LONG ||
             SLP_OBJ_TYPE(value) == SLP_OBJ_DOUBLE));
}

static SlpValue stdlib_dereference(SlpValue value) {
    while (SLP_IS_OBJ(value) &&
           SLP_OBJ_TYPE(value) == SLP_OBJ_SCALAR_CELL)
        value = SLP_AS_SCALAR_CELL(value)->value;
    return value;
}

static int64_t stdlib_as_int64(SlpValue value) {
    value = stdlib_dereference(value);
    if (SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) == SLP_OBJ_LONG)
        return SLP_AS_LONG(value)->value;
    return (int64_t)slp_value_as_number(value);
}

typedef struct {
    SlpObj **sources;
    SlpValue *copies;
    int count;
    int capacity;
} SerializationCloneContext;

static SlpValue clone_serializable_value(
    SlpVM *vm, SerializationCloneContext *context, SlpValue value);

static bool clone_context_add(
    SlpVM *vm, SerializationCloneContext *context,
    SlpObj *source, SlpValue copy) {
    if (context->count == context->capacity) {
        int capacity =
            context->capacity < 8 ? 8 : context->capacity * 2;
        SlpObj **sources = (SlpObj**)vm->allocator->reallocate(
            context->sources, sizeof(SlpObj*) * (size_t)capacity,
            vm->allocator->user_data);
        if (!sources) return false;
        context->sources = sources;
        SlpValue *copies = (SlpValue*)vm->allocator->reallocate(
            context->copies, sizeof(SlpValue) * (size_t)capacity,
            vm->allocator->user_data);
        if (!copies) return false;
        context->copies = copies;
        context->capacity = capacity;
    }
    context->sources[context->count] = source;
    context->copies[context->count] = copy;
    context->count++;
    return true;
}

static bool clone_context_find(
    SerializationCloneContext *context, SlpObj *source, SlpValue *copy) {
    for (int i = 0; i < context->count; i++) {
        if (context->sources[i] == source) {
            *copy = context->copies[i];
            return true;
        }
    }
    return false;
}

static SlpValue clone_serializable_array(
    SlpVM *vm, SerializationCloneContext *context, SlpObjArray *source) {
    SlpObjArray *copy = slp_vm_new_array(vm);
    if (!copy) return SLP_NULL_VAL;
    SlpValue result = SLP_OBJ_VAL(copy);
    if (!clone_context_add(vm, context, &source->obj, result))
        return SLP_NULL_VAL;
    for (int i = 0; i < source->count; i++) {
        slp_obj_array_push(
            vm->allocator, copy,
            clone_serializable_value(vm, context, source->elements[i]));
    }
    return result;
}

static SlpValue clone_serializable_hash(
    SlpVM *vm, SerializationCloneContext *context, SlpObjHash *source) {
    SlpObjHash *copy = slp_vm_new_hash(vm);
    if (!copy) return SLP_NULL_VAL;
    copy->order_mode = source->order_mode;
    SlpValue result = SLP_OBJ_VAL(copy);
    if (!clone_context_add(vm, context, &source->obj, result))
        return SLP_NULL_VAL;
    for (int ordinal = 0;
         ordinal < slp_obj_hash_visible_count(source); ordinal++) {
        int index = slp_obj_hash_ordered_index(source, ordinal);
        if (index < 0) continue;
        SlpHashEntry *entry = &source->entries[index];
        slp_vm_hash_set(
            vm, copy,
            clone_serializable_value(vm, context, entry->key),
            clone_serializable_value(vm, context, entry->value));
    }
    return result;
}

static SlpValue clone_serializable_closure(
    SlpVM *vm, SerializationCloneContext *context, SlpObjClosure *source) {
    SlpObjClosure *copy = slp_vm_clone_closure(vm, source);
    if (!copy) return SLP_NULL_VAL;
    copy->identity = source->identity;
    SlpValue result = SLP_OBJ_VAL(copy);
    if (!clone_context_add(vm, context, &source->obj, result))
        return SLP_NULL_VAL;
    copy->call_name = source->call_name;
    if (source->scope) {
        SlpValue scope = clone_serializable_value(
            vm, context, SLP_OBJ_VAL(source->scope));
        if (SLP_IS_OBJ(scope) && SLP_OBJ_TYPE(scope) == SLP_OBJ_HASH)
            copy->scope = SLP_AS_HASH(scope);
    }
    if (source->coroutine_stack) {
        copy->coroutine_stack_count = source->coroutine_stack_count;
        copy->coroutine_frame_count = source->coroutine_frame_count;
        copy->coroutine_try_handler_count =
            source->coroutine_try_handler_count;
        copy->coroutine_needs_result =
            source->coroutine_needs_result;
        if (copy->coroutine_stack_count > 0)
            copy->coroutine_stack = SLP_ALLOCATE_ARRAY(
                vm->allocator, SlpValue,
                copy->coroutine_stack_count);
        if (copy->coroutine_frame_count > 0)
            copy->coroutine_frames = SLP_ALLOCATE_ARRAY(
                vm->allocator, SlpCallFrame,
                copy->coroutine_frame_count);
        if (copy->coroutine_try_handler_count > 0)
            copy->coroutine_try_handlers = SLP_ALLOCATE_ARRAY(
                vm->allocator, SlpTryHandler,
                copy->coroutine_try_handler_count);
        if ((copy->coroutine_stack_count > 0 &&
             !copy->coroutine_stack) ||
            (copy->coroutine_frame_count > 0 &&
             !copy->coroutine_frames) ||
            (copy->coroutine_try_handler_count > 0 &&
             !copy->coroutine_try_handlers))
            return SLP_NULL_VAL;

        for (int i = 0; i < copy->coroutine_stack_count; i++)
            copy->coroutine_stack[i] = clone_serializable_value(
                vm, context, source->coroutine_stack[i]);
        if (copy->coroutine_frame_count > 0)
            memcpy(
                copy->coroutine_frames, source->coroutine_frames,
                sizeof(SlpCallFrame) *
                    (size_t)copy->coroutine_frame_count);
        for (int i = 0; i < copy->coroutine_frame_count; i++) {
            SlpCallFrame *source_frame =
                &source->coroutine_frames[i];
            SlpCallFrame *copy_frame =
                &copy->coroutine_frames[i];
            ptrdiff_t slot_offset =
                source_frame->slots - source->coroutine_stack;
            copy_frame->slots =
                copy->coroutine_stack + slot_offset;
            SlpValue frame_closure = clone_serializable_value(
                vm, context, SLP_OBJ_VAL(source_frame->closure));
            if (SLP_IS_OBJ(frame_closure) &&
                SLP_OBJ_TYPE(frame_closure) == SLP_OBJ_CLOSURE)
                copy_frame->closure =
                    SLP_AS_CLOSURE(frame_closure);
            if (source_frame->local_scopes) {
                SlpValue local_scopes = clone_serializable_value(
                    vm, context,
                    SLP_OBJ_VAL(source_frame->local_scopes));
                if (SLP_IS_OBJ(local_scopes) &&
                    SLP_OBJ_TYPE(local_scopes) == SLP_OBJ_ARRAY)
                    copy_frame->local_scopes =
                        SLP_AS_ARRAY(local_scopes);
            }
            if (source_frame->closure_scope) {
                SlpValue closure_scope = clone_serializable_value(
                    vm, context,
                    SLP_OBJ_VAL(source_frame->closure_scope));
                if (SLP_IS_OBJ(closure_scope) &&
                    SLP_OBJ_TYPE(closure_scope) == SLP_OBJ_HASH)
                    copy_frame->closure_scope =
                        SLP_AS_HASH(closure_scope);
            }
            copy_frame->continuation_return = NULL;
        }
        if (copy->coroutine_try_handler_count > 0)
            memcpy(
                copy->coroutine_try_handlers,
                source->coroutine_try_handlers,
                sizeof(SlpTryHandler) *
                    (size_t)copy->coroutine_try_handler_count);
    }
    return result;
}

static SlpValue clone_serializable_value(
    SlpVM *vm, SerializationCloneContext *context, SlpValue value) {
    value = stdlib_dereference(value);
    if (!SLP_IS_OBJ(value)) return value;

    SlpValue previous;
    if (clone_context_find(context, SLP_AS_OBJ(value), &previous))
        return previous;

    switch (SLP_OBJ_TYPE(value)) {
    case SLP_OBJ_ARRAY:
        return clone_serializable_array(
            vm, context, SLP_AS_ARRAY(value));
    case SLP_OBJ_HASH:
        return clone_serializable_hash(
            vm, context, SLP_AS_HASH(value));
    case SLP_OBJ_CLOSURE:
        return clone_serializable_closure(
            vm, context, SLP_AS_CLOSURE(value));
    default:
        return value;
    }
}

static SlpValue clone_serializable(SlpVM *vm, SlpValue value) {
    SerializationCloneContext context;
    memset(&context, 0, sizeof(context));
    SlpValue copy = clone_serializable_value(vm, &context, value);
    if (context.sources) stdlib_free(vm, context.sources);
    if (context.copies) stdlib_free(vm, context.copies);
    return copy;
}

/* -----------------------------------------------------------------------
 * Control Flow Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_iff(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NULL_VAL;
    if (!slp_value_is_falsy(args[0]))
        return argc >= 2 ? args[1] : SLP_NUM_VAL(1.0);
    return argc >= 3 ? args[2] : SLP_NULL_VAL;
}

static SlpValue builtin_function(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        SLP_AS_STRING(args[0])->length == 0 ||
        SLP_AS_STRING(args[0])->chars[0] != '&') {
        slp_vm_abort_warning(
            vm,
            "&function: requested function name must begin with '&'");
        return SLP_NULL_VAL;
    }

    SlpObjString *requested = SLP_AS_STRING(args[0]);
    SlpObjString *name = slp_vm_copy_string(
        vm, requested->chars + 1, requested->length - 1);
    if (!name) return SLP_NULL_VAL;
    return slp_obj_hash_get(vm->globals, SLP_OBJ_VAL(name));
}

static SlpValue builtin_setf(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        SLP_AS_STRING(args[0])->length == 0 ||
        SLP_AS_STRING(args[0])->chars[0] != '&') {
        const char *name =
            argc > 0 && SLP_IS_OBJ(args[0]) &&
                    SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING
                ? SLP_AS_STRING(args[0])->chars
                : "";
        char message[320];
        snprintf(
            message, sizeof(message),
            "&setf: invalid function name '%s'", name);
        slp_vm_abort_warning(vm, message);
        return SLP_NULL_VAL;
    }

    SlpObjString *requested = SLP_AS_STRING(args[0]);
    SlpObjString *name = slp_vm_copy_string(
        vm, requested->chars + 1, requested->length - 1);
    if (!name) return SLP_NULL_VAL;
    if (SLP_IS_NULL(args[1]))
        slp_obj_hash_delete(vm->allocator, vm->globals, SLP_OBJ_VAL(name));
    else if (
        SLP_IS_OBJ(args[1]) &&
        (SLP_OBJ_TYPE(args[1]) == SLP_OBJ_CLOSURE ||
         SLP_OBJ_TYPE(args[1]) == SLP_OBJ_NATIVE ||
         SLP_OBJ_TYPE(args[1]) == SLP_OBJ_CONTINUATION))
        slp_obj_hash_set(
            vm->allocator, vm->globals, SLP_OBJ_VAL(name), args[1]);
    else {
        const char *class_name = "java.lang.Object";
        if (SLP_IS_OBJ(args[1]) &&
            SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING)
            class_name = "java.lang.String";
        else if (SLP_IS_NUM(args[1]) || SLP_IS_BOOL(args[1]))
            class_name = "java.lang.Integer";
        else if (SLP_IS_OBJ(args[1]) &&
                 SLP_OBJ_TYPE(args[1]) == SLP_OBJ_LONG)
            class_name = "java.lang.Long";
        else if (SLP_IS_OBJ(args[1]) &&
                 SLP_OBJ_TYPE(args[1]) == SLP_OBJ_DOUBLE)
            class_name = "java.lang.Double";
        char message[384];
        snprintf(
            message, sizeof(message),
            "&setf: can not set function %s to a class %s",
            requested->chars, class_name);
        slp_vm_abort_warning(vm, message);
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_warn(SlpVM *vm, SlpValue *args, int argc) {
    SlpObjString *message = argc > 0
        ? slp_vm_stringify(vm, args[0])
        : slp_vm_copy_cstr(vm, "");
    slp_vm_warning(vm, message ? message->chars : "");
    return SLP_NULL_VAL;
}

static SlpValue builtin_ordered_hash(
    SlpVM *vm, SlpValue *args, int argc, uint8_t order_mode) {
    SlpObjHash *hash = slp_vm_new_hash(vm);
    if (!hash) return SLP_NULL_VAL;
    hash->order_mode = order_mode;
    for (int i = 0; i < argc; i++) {
        if (SLP_IS_OBJ(args[i]) &&
            SLP_OBJ_TYPE(args[i]) == SLP_OBJ_KEY_VALUE) {
            SlpObjKeyValue *pair = SLP_AS_KEY_VALUE(args[i]);
            slp_obj_hash_set(
                vm->allocator, hash, pair->key,
                stdlib_dereference(pair->value));
        }
    }
    return SLP_OBJ_VAL(hash);
}

static SlpValue builtin_ohash(SlpVM *vm, SlpValue *args, int argc) {
    return builtin_ordered_hash(vm, args, argc, 1);
}

static SlpValue builtin_ohasha(SlpVM *vm, SlpValue *args, int argc) {
    return builtin_ordered_hash(vm, args, argc, 2);
}

static SlpValue builtin_setMissPolicy(
    SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_HASH ||
        SLP_AS_HASH(args[0])->order_mode == 0 ||
        !SLP_IS_OBJ(args[1]) ||
        SLP_OBJ_TYPE(args[1]) != SLP_OBJ_CLOSURE)
        return SLP_NULL_VAL;
    SLP_AS_HASH(args[0])->miss_policy = SLP_AS_CLOSURE(args[1]);
    return SLP_NULL_VAL;
}

static SlpValue builtin_setRemovalPolicy(
    SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_HASH ||
        SLP_AS_HASH(args[0])->order_mode == 0 ||
        !SLP_IS_OBJ(args[1]) ||
        SLP_OBJ_TYPE(args[1]) != SLP_OBJ_CLOSURE)
        return SLP_NULL_VAL;
    SLP_AS_HASH(args[0])->removal_policy =
        SLP_AS_CLOSURE(args[1]);
    return SLP_NULL_VAL;
}

static SlpValue bind_closure(SlpVM *vm, SlpValue *args, int argc,
                             bool clone_source) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_CLOSURE)
        return SLP_NULL_VAL;

    SlpObjClosure *source = SLP_AS_CLOSURE(args[0]);
    SlpObjClosure *closure =
        clone_source ? slp_vm_clone_closure(vm, source) : source;
    if (!closure) return SLP_NULL_VAL;
    if (!closure->scope) {
        /* let() can bind an as-yet uncalled closure. Clone once to use the
           VM's normal scope initialization without exposing GC internals. */
        SlpObjClosure *initialized = slp_vm_clone_closure(vm, closure);
        if (!initialized) return SLP_NULL_VAL;
        if (!clone_source) {
            closure->scope = initialized->scope;
            SlpObjString *this_name = slp_vm_copy_string(vm, "$this", 5);
            slp_obj_hash_set(vm->allocator, closure->scope,
                             SLP_OBJ_VAL(this_name), SLP_OBJ_VAL(closure));
        } else {
            closure = initialized;
        }
    }

    for (int i = 1; i < argc; i++) {
        if (!SLP_IS_OBJ(args[i]) ||
            SLP_OBJ_TYPE(args[i]) != SLP_OBJ_KEY_VALUE)
            continue;
        SlpObjKeyValue *kv = SLP_AS_KEY_VALUE(args[i]);
        SlpValue bound_value =
            stdlib_dereference(kv->value);
        if (SLP_IS_OBJ(kv->key) &&
            SLP_OBJ_TYPE(kv->key) == SLP_OBJ_STRING &&
            strcmp(SLP_AS_STRING(kv->key)->chars, "$this") == 0 &&
            SLP_IS_OBJ(bound_value) &&
            SLP_OBJ_TYPE(bound_value) == SLP_OBJ_CLOSURE) {
            SlpObjClosure *environment =
                SLP_AS_CLOSURE(bound_value);
            if (!environment->scope) {
                SlpObjClosure *initialized =
                    slp_vm_clone_closure(vm, environment);
                if (!initialized) return SLP_NULL_VAL;
                environment->scope = initialized->scope;
                SlpObjString *this_name =
                    slp_vm_copy_string(vm, "$this", 5);
                slp_obj_hash_set(vm->allocator, environment->scope,
                                 SLP_OBJ_VAL(this_name),
                                 SLP_OBJ_VAL(environment));
            }
            closure->scope = environment->scope;
        } else {
            slp_obj_hash_set(vm->allocator, closure->scope,
                             kv->key,
                             clone_source
                                 ? stdlib_dereference(kv->value)
                                 : kv->value);
        }
    }
    return SLP_OBJ_VAL(closure);
}

static SlpValue builtin_lambda(SlpVM *vm, SlpValue *args, int argc) {
    return bind_closure(vm, args, argc, true);
}

static SlpValue builtin_let(SlpVM *vm, SlpValue *args, int argc) {
    return bind_closure(vm, args, argc, false);
}

static SlpValue builtin_compile_closure(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    SlpObjString *source =
        slp_vm_stringify(vm, args[0]);
    if (!source) return SLP_NULL_VAL;

    SlpObjClosure *closure =
        slp_vm_compile_closure_source(
            vm, source->chars, "eval");
    if (!closure) return SLP_NULL_VAL;

    SlpValue bound_args[256];
    bound_args[0] = SLP_OBJ_VAL(closure);
    int bound_count = argc;
    if (bound_count > 256)
        bound_count = 256;
    for (int i = 1; i < bound_count; i++)
        bound_args[i] = args[i];
    return bind_closure(
        vm, bound_args, bound_count, false);
}

static SlpValue builtin_newInstance(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2)
        return SLP_NULL_VAL;
    SlpValue closure_value =
        stdlib_dereference(args[1]);
    if (!SLP_IS_OBJ(closure_value) ||
        SLP_OBJ_TYPE(closure_value) !=
            SLP_OBJ_CLOSURE)
        return SLP_NULL_VAL;

    SlpObjArray *interfaces =
        slp_vm_new_array(vm);
    if (!interfaces)
        return SLP_NULL_VAL;
    SlpValue interface_source =
        stdlib_dereference(args[0]);
    if (SLP_IS_OBJ(interface_source) &&
        SLP_OBJ_TYPE(interface_source) ==
            SLP_OBJ_CLASS) {
        slp_obj_array_push(
            vm->allocator, interfaces,
            interface_source);
    } else if (
        SLP_IS_OBJ(interface_source) &&
        SLP_OBJ_TYPE(interface_source) ==
            SLP_OBJ_ARRAY) {
        SlpObjArray *source =
            SLP_AS_ARRAY(interface_source);
        for (int i = 0;
             i < source->count; i++) {
            SlpValue item =
                stdlib_dereference(
                    source->elements[i]);
            if (!SLP_IS_OBJ(item) ||
                SLP_OBJ_TYPE(item) !=
                    SLP_OBJ_CLASS)
                return SLP_NULL_VAL;
            slp_obj_array_push(
                vm->allocator, interfaces,
                item);
        }
    } else {
        return SLP_NULL_VAL;
    }
    if (interfaces->count == 0)
        return SLP_NULL_VAL;

    char class_name[96];
    snprintf(
        class_name, sizeof(class_name),
        "com.sun.proxy.$Proxy%llu",
        (unsigned long long)
            vm->next_proxy_identity++);
    SlpObjJavaObject *proxy =
        slp_vm_new_java_object(
            vm, class_name,
            SLP_JAVA_PROXY);
    if (!proxy)
        return SLP_NULL_VAL;
    proxy->list = interfaces;
    proxy->value = closure_value;
    proxy->class_object->interfaces =
        interfaces;
    return SLP_OBJ_VAL(proxy);
}

static SlpValue builtin_setField(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2)
        return SLP_NULL_VAL;

    SlpValue target =
        stdlib_dereference(args[0]);
    SlpObjHash **fields = NULL;
    if (SLP_IS_OBJ(target) &&
        SLP_OBJ_TYPE(target) == SLP_OBJ_CLASS) {
        fields = &SLP_AS_CLASS(target)->fields;
    } else if (
        SLP_IS_OBJ(target) &&
        SLP_OBJ_TYPE(target) ==
            SLP_OBJ_JAVA_OBJECT) {
        fields =
            &SLP_AS_JAVA_OBJECT(target)->fields;
    } else {
        slp_vm_abort_warning(
            vm,
            "&setField: expected a Java object or class");
        return SLP_NULL_VAL;
    }

    if (!*fields) {
        *fields = slp_vm_new_hash(vm);
        if (!*fields)
            return SLP_NULL_VAL;
    }

    for (int i = 1; i < argc; i++) {
        SlpValue assignment =
            stdlib_dereference(args[i]);
        if (!SLP_IS_OBJ(assignment) ||
            SLP_OBJ_TYPE(assignment) !=
                SLP_OBJ_KEY_VALUE) {
            slp_vm_abort_warning(
                vm,
                "&setField: expected field => value assignments");
            return SLP_NULL_VAL;
        }
        SlpObjKeyValue *field =
            SLP_AS_KEY_VALUE(assignment);
        SlpValue name =
            stdlib_dereference(field->key);
        if (!SLP_IS_OBJ(name) ||
            SLP_OBJ_TYPE(name) !=
                SLP_OBJ_STRING) {
            slp_vm_abort_warning(
                vm,
                "&setField: field name must be a string");
            return SLP_NULL_VAL;
        }
        slp_vm_hash_set(
            vm, *fields, name,
            stdlib_dereference(field->value));
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_use(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || argc > 2)
        return SLP_NULL_VAL;

    if (argc == 2) {
        SlpObjString *source =
            slp_vm_stringify(
                vm,
                stdlib_dereference(args[0]));
        if (!source)
            return SLP_NULL_VAL;
        FILE *file =
            fopen(source->chars, "rb");
        if (!file) {
            char message[2304];
            snprintf(
                message, sizeof(message),
                "could not locate source '%s'",
                source->chars);
            slp_vm_flag_error(
                vm,
                slp_vm_new_error(
                    vm,
                    "java.io.FileNotFoundException",
                    message, message, NULL));
            return SLP_NULL_VAL;
        }
        fclose(file);
    } else {
        SlpValue loadable =
            stdlib_dereference(args[0]);
        if (!SLP_IS_OBJ(loadable) ||
            SLP_OBJ_TYPE(loadable) !=
                SLP_OBJ_CLASS) {
            SlpObjString *class_name =
                slp_vm_stringify(vm, loadable);
            char message[1200];
            snprintf(
                message, sizeof(message),
                "%s",
                class_name
                    ? class_name->chars
                    : "");
            slp_vm_flag_error(
                vm,
                slp_vm_new_error(
                    vm,
                    "java.lang.ClassNotFoundException",
                    message, message, NULL));
            return SLP_NULL_VAL;
        }
    }

    /*
     * The native runtime intentionally has no JVM or JAR loader. Report this
     * through Sleep's checkError channel so scripts can fall back cleanly;
     * silently accepting use() would leave the requested functions absent.
     */
    slp_vm_flag_error(
        vm,
        slp_vm_new_error(
            vm,
            "java.lang.UnsupportedOperationException",
            "dynamic Java Loadable extensions are unavailable "
            "in the portable runtime",
            "dynamic Java Loadable extensions are unavailable "
            "in the portable runtime",
            NULL));
    return SLP_NULL_VAL;
}

static SlpValue taint_value_recursive(
    SlpVM *vm, SlpValue value) {
    if (SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) ==
            SLP_OBJ_SCALAR_CELL) {
        SlpObjScalarCell *cell =
            SLP_AS_SCALAR_CELL(value);
        cell->value =
            taint_value_recursive(
                vm, cell->value);
        return cell->value;
    }
    if (slp_value_is_tainted(value))
        return value;

    if (SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) ==
            SLP_OBJ_ARRAY) {
        SlpObjArray *array =
            SLP_AS_ARRAY(value);
        for (int i = 0;
             i < array->count; i++)
            array->elements[i] =
                taint_value_recursive(
                    vm,
                    array->elements[i]);
        return value;
    }
    if (SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) ==
            SLP_OBJ_HASH) {
        SlpObjHash *hash =
            SLP_AS_HASH(value);
        for (int i = 0;
             i < hash->capacity; i++) {
            if (SLP_IS_NULL(
                    hash->entries[i].key) ||
                SLP_IS_NULL(
                    hash->entries[i].value))
                continue;
            hash->entries[i].value =
                taint_value_recursive(
                    vm,
                    hash->entries[i].value);
        }
        return value;
    }
    if (SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) ==
            SLP_OBJ_JAVA_OBJECT) {
        SlpObjJavaObject *object =
            SLP_AS_JAVA_OBJECT(value);
        if (object->list) {
            for (int i = 0;
                 i < object->list->count;
                 i++)
                object->list->elements[i] =
                    taint_value_recursive(
                        vm,
                        object->list
                            ->elements[i]);
            return value;
        }
        if (object->map) {
            SlpObjHash *hash =
                object->map;
            for (int i = 0;
                 i < hash->capacity; i++) {
                if (SLP_IS_NULL(
                        hash->entries[i].key) ||
                    SLP_IS_NULL(
                        hash->entries[i].value))
                    continue;
                hash->entries[i].value =
                    taint_value_recursive(
                        vm,
                        hash->entries[i].value);
            }
            return value;
        }
    }
    return slp_vm_taint_value(vm, value);
}

static SlpValue untaint_value_recursive(
    SlpValue value) {
    if (SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) ==
            SLP_OBJ_SCALAR_CELL) {
        SlpObjScalarCell *cell =
            SLP_AS_SCALAR_CELL(value);
        cell->value =
            untaint_value_recursive(
                cell->value);
        return cell->value;
    }

    value =
        slp_value_unwrap_taint(value);
    if (SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) ==
            SLP_OBJ_ARRAY) {
        SlpObjArray *array =
            SLP_AS_ARRAY(value);
        for (int i = 0;
             i < array->count; i++)
            array->elements[i] =
                untaint_value_recursive(
                    array->elements[i]);
    } else if (
        SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) ==
            SLP_OBJ_HASH) {
        SlpObjHash *hash =
            SLP_AS_HASH(value);
        for (int i = 0;
             i < hash->capacity; i++) {
            if (SLP_IS_NULL(
                    hash->entries[i].key) ||
                SLP_IS_NULL(
                    hash->entries[i].value))
                continue;
            hash->entries[i].value =
                untaint_value_recursive(
                    hash->entries[i].value);
        }
    } else if (
        SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) ==
            SLP_OBJ_JAVA_OBJECT) {
        SlpObjJavaObject *object =
            SLP_AS_JAVA_OBJECT(value);
        if (object->list) {
            for (int i = 0;
                 i < object->list->count;
                 i++)
                object->list->elements[i] =
                    untaint_value_recursive(
                        object->list
                            ->elements[i]);
        }
        if (object->map) {
            SlpObjHash *hash =
                object->map;
            for (int i = 0;
                 i < hash->capacity; i++) {
                if (SLP_IS_NULL(
                        hash->entries[i].key) ||
                    SLP_IS_NULL(
                        hash->entries[i].value))
                    continue;
                hash->entries[i].value =
                    untaint_value_recursive(
                        hash->entries[i].value);
            }
        }
    }
    return value;
}

static SlpValue builtin_taint(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1)
        return SLP_NULL_VAL;
    return taint_value_recursive(
        vm, args[0]);
}

static SlpValue builtin_untaint(
    SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1)
        return SLP_NULL_VAL;
    return untaint_value_recursive(
        args[0]);
}

/* -----------------------------------------------------------------------
 * Math Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_abs(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    double v = slp_value_as_number(args[0]);
    return SLP_NUM_VAL(v < 0 ? -v : v);
}

static SlpValue builtin_ceil(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(ceil(slp_value_as_number(args[0])));
}

static SlpValue builtin_floor(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(floor(slp_value_as_number(args[0])));
}

static SlpValue builtin_sqrt(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(sqrt(slp_value_as_number(args[0])));
}

static SlpValue builtin_sin(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(sin(slp_value_as_number(args[0])));
}

static SlpValue builtin_cos(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(cos(slp_value_as_number(args[0])));
}

static SlpValue builtin_tan(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(tan(slp_value_as_number(args[0])));
}

static SlpValue builtin_asin(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(asin(slp_value_as_number(args[0])));
}

static SlpValue builtin_acos(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(acos(slp_value_as_number(args[0])));
}

static SlpValue builtin_atan(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(atan(slp_value_as_number(args[0])));
}

static SlpValue builtin_atan2(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2 || !stdlib_is_number(args[0]) || !stdlib_is_number(args[1]))
        return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(atan2(slp_value_as_number(args[0]), slp_value_as_number(args[1])));
}

static SlpValue builtin_exp(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(exp(slp_value_as_number(args[0])));
}

static SlpValue builtin_log(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(log(slp_value_as_number(args[0])));
}

static SlpValue builtin_round(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    if (argc < 2 || !stdlib_is_number(args[1]))
        return SLP_NUM_VAL(round(slp_value_as_number(args[0])));
    double places = pow(10.0, (double)(int)slp_value_as_number(args[1]));
    return SLP_NUM_VAL(round(slp_value_as_number(args[0]) * places) / places);
}

static SlpValue builtin_double(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    if (stdlib_is_number(args[0])) return args[0];
    if (SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
        return SLP_NUM_VAL(atof(SLP_AS_STRING(args[0])->chars));
    }
    return SLP_NUM_VAL(0);
}

static SlpValue builtin_not(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !stdlib_is_number(args[0]))
        return SLP_NUM_VAL(-1);
    if (SLP_IS_OBJ(args[0]) &&
        SLP_OBJ_TYPE(args[0]) == SLP_OBJ_LONG) {
        SlpObjLong *result = slp_vm_new_long(
            vm, ~SLP_AS_LONG(args[0])->value);
        return result ? SLP_OBJ_VAL(result) : SLP_NULL_VAL;
    }
    return SLP_NUM_VAL(
        (double)(~(int32_t)slp_value_as_number(args[0])));
}

/* -----------------------------------------------------------------------
 * String Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_strlen(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    if (!SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NUM_VAL(0);
    return SLP_NUM_VAL((double)SLP_AS_STRING(args[0])->length);
}

static int normalize_string_index(int index, int length) {
    return index < 0 ? index + length : index;
}

static SlpValue sleep_substring(SlpVM *vm, SlpObjString *string,
                                int start, int end,
                                const char *function_name) {
    int length = (int)string->length;
    int original_start = start;
    int original_end = end;
    start = normalize_string_index(start, length);
    end = normalize_string_index(end, length);
    if (end > length) end = length;
    if (start > end) {
        size_t message_length =
            strlen(function_name) + string->length + 128;
        char *message = (char*)stdlib_alloc(vm, message_length);
        if (message) {
            snprintf(
                message, message_length,
                "%s: illegal substring('%s', %d -> %d, %d -> %d) indices",
                function_name, string->chars,
                original_start, start, original_end, end);
            slp_vm_abort_warning(vm, message);
            stdlib_free(vm, message);
        }
        return SLP_NULL_VAL;
    }
    if (start < 0 || start > length || end < 0)
        return SLP_NULL_VAL;
    return SLP_OBJ_VAL(slp_vm_copy_string(
        vm, string->chars + start, (uint32_t)(end - start)));
}

static SlpValue builtin_substr(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2) return SLP_NULL_VAL;
    if (!SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    int start = stdlib_is_number(args[1]) ? (int)slp_value_as_number(args[1]) : 0;
    int end = (argc >= 3 && stdlib_is_number(args[2]))
              ? (int)slp_value_as_number(args[2])
              : (int)str->length;
    return sleep_substring(vm, str, start, end, "&substr");
}

static SlpValue builtin_lc(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    char *buf = stdlib_alloc(vm, str->length + 1);
    for (uint32_t i = 0; i < str->length; i++) buf[i] = (char)tolower(str->chars[i]);
    buf[str->length] = '\0';
    SlpObjString *result = slp_vm_copy_string(vm, buf, str->length);
    stdlib_free(vm, buf);
    return SLP_OBJ_VAL(result);
}

static SlpValue builtin_uc(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    char *buf = stdlib_alloc(vm, str->length + 1);
    for (uint32_t i = 0; i < str->length; i++) buf[i] = (char)toupper(str->chars[i]);
    buf[str->length] = '\0';
    SlpObjString *result = slp_vm_copy_string(vm, buf, str->length);
    stdlib_free(vm, buf);
    return SLP_OBJ_VAL(result);
}

static SlpValue builtin_replace(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 3) return SLP_NULL_VAL;
    if (!SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING) return args[0];
    if (!SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING) return args[0];
    if (!SLP_IS_OBJ(args[2]) || SLP_OBJ_TYPE(args[2]) != SLP_OBJ_STRING) return args[0];
    SlpObjString *src = SLP_AS_STRING(args[0]);
    SlpObjString *old = SLP_AS_STRING(args[1]);
    SlpObjString *rep = SLP_AS_STRING(args[2]);
    if (old->length == 0) return args[0];

    int count = 0;
    const char *p = src->chars;
    while ((p = strstr(p, old->chars)) != NULL) { count++; p += old->length; }
    if (count == 0) return args[0];

    uint32_t new_len = src->length + (uint32_t)count * (rep->length - old->length);
    char *buf = stdlib_alloc(vm, new_len + 1);
    char *dst = buf;
    p = src->chars;
    const char *prev = p;
    while ((p = strstr(p, old->chars)) != NULL) {
        memcpy(dst, prev, p - prev);
        dst += p - prev;
        memcpy(dst, rep->chars, rep->length);
        dst += rep->length;
        p += old->length;
        prev = p;
    }
    memcpy(dst, prev, src->chars + src->length - prev);
    dst += src->chars + src->length - prev;
    *dst = '\0';

    SlpObjString *result = slp_vm_copy_string(vm, buf, new_len);
    stdlib_free(vm, buf);
    return SLP_OBJ_VAL(result);
}

static SlpValue builtin_split(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
                    !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING) {
        return SLP_OBJ_VAL(slp_vm_new_array(vm));
    }
    const char *pattern = SLP_AS_STRING(args[0])->chars;
    const char *str = SLP_AS_STRING(args[1])->chars;
    int limit = argc >= 3 && stdlib_is_number(args[2])
                    ? (int)slp_value_as_number(args[2])
                    : 0;
    SlpObjArray *arr = slp_vm_new_array(vm);
    
    if (strlen(pattern) == 0) {
        size_t len = strlen(str);
        for (size_t i = 0; i < len; i++) {
            slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(slp_vm_copy_string(vm, &str[i], 1)));
        }
        return SLP_OBJ_VAL(arr);
    }
    
    int string_length = (int)strlen(str);
    int cursor = 0;
    int search_from = 0;
    while ((limit <= 0 ||
            arr->count < limit - 1) &&
           search_from <= string_length) {
        int match_end = -1;
        int match_start =
            slp_vm_regex_find(
                vm, str, pattern,
                search_from, false,
                &match_end);
        if (match_start < 0)
            break;

        /*
         * Java Pattern.split does not emit a leading empty element for a
         * zero-width match at the start of the input.
         */
        if (!(match_start == 0 &&
              match_end == 0 &&
              cursor == 0)) {
            slp_obj_array_push(
                vm->allocator, arr,
                SLP_OBJ_VAL(
                    slp_vm_copy_string(
                        vm, str + cursor,
                        (uint32_t)(
                            match_start -
                            cursor))));
            cursor = match_end;
        }

        if (match_end > match_start) {
            search_from = match_end;
        } else {
            search_from = match_end + 1;
            if (search_from >
                string_length)
                break;
        }
    }
    slp_obj_array_push(
        vm->allocator, arr,
        SLP_OBJ_VAL(
            slp_vm_copy_string(
                vm, str + cursor,
                (uint32_t)(
                    string_length - cursor))));

    /* A zero limit is Java's default and discards trailing empty fields. */
    if (limit == 0) {
        while (arr->count > 0) {
            SlpValue last =
                arr->elements[arr->count - 1];
            if (!SLP_IS_OBJ(last) ||
                SLP_OBJ_TYPE(last) !=
                    SLP_OBJ_STRING ||
                SLP_AS_STRING(last)->length != 0)
                break;
            arr->count--;
        }
    }
    return SLP_OBJ_VAL(arr);
}

static bool collect_iterator_values(SlpVM *vm, SlpValue iterator,
                                    SlpObjArray *values) {
    if (!SLP_IS_OBJ(iterator)) {
        SlpObjString *text = slp_vm_stringify(vm, iterator);
        char message[256];
        snprintf(
            message, sizeof(message),
            "expected iterator (@array or &closure)--received: %s",
            text ? text->chars : "");
        slp_vm_abort_warning(vm, message);
        return false;
    }

    if (SLP_OBJ_TYPE(iterator) == SLP_OBJ_ARRAY) {
        SlpObjArray *array = SLP_AS_ARRAY(iterator);
        for (int i = 0; i < array->count; i++)
            slp_obj_array_push(vm->allocator, values, array->elements[i]);
        return true;
    }

    if (SLP_OBJ_TYPE(iterator) ==
            SLP_OBJ_JAVA_OBJECT &&
        SLP_AS_JAVA_OBJECT(iterator)->kind ==
            SLP_JAVA_ITERATOR &&
        SLP_AS_JAVA_OBJECT(iterator)->list) {
        SlpObjJavaObject *object =
            SLP_AS_JAVA_OBJECT(iterator);
        int index =
            (int)slp_value_as_number(
                object->value);
        if (index < 0) index = 0;
        for (; index < object->list->count;
             index++) {
            slp_obj_array_push(
                vm->allocator, values,
                object->list->elements[index]);
        }
        object->value =
            SLP_NUM_VAL((double)index);
        return true;
    }

    if (SLP_OBJ_TYPE(iterator) != SLP_OBJ_CLOSURE) {
        SlpObjString *text = slp_vm_stringify(vm, iterator);
        char message[256];
        if (SLP_OBJ_TYPE(iterator) == SLP_OBJ_STRING) {
            snprintf(
                message, sizeof(message),
                "expected iterator (@array or &closure)--received: '%s'",
                text ? text->chars : "");
        } else {
            snprintf(
                message, sizeof(message),
                "expected iterator (@array or &closure)--received: %s",
                text ? text->chars : "");
        }
        slp_vm_abort_warning(vm, message);
        return false;
    }

    SlpObjClosure *closure = SLP_AS_CLOSURE(iterator);
    for (;;) {
        slp_vm_push(vm, iterator);
        if (slp_vm_call(vm, 0, false) != SLP_OK) return false;

        SlpValue value = slp_vm_pop(vm);
        bool suspended = closure->coroutine_stack != NULL;
        if (SLP_IS_NULL(value)) return true;

        slp_obj_array_push(vm->allocator, values, value);
        if (!suspended) return true;
    }
}

static SlpValue builtin_join(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING) {
        return SLP_OBJ_VAL(slp_vm_copy_string(vm, "", 0));
    }

    SlpObjArray *arr = slp_vm_new_array(vm);
    if (!arr) return SLP_NULL_VAL;
    slp_vm_push(vm, SLP_OBJ_VAL(arr));
    bool collected = collect_iterator_values(vm, args[1], arr);
    slp_vm_pop(vm);
    if (!collected)
        return SLP_OBJ_VAL(slp_vm_copy_string(vm, "", 0));

    if (arr->count == 0) {
        return SLP_OBJ_VAL(slp_vm_copy_string(vm, "", 0));
    }

    SlpObjString *separator = SLP_AS_STRING(args[0]);
    size_t sep_len = separator->length;
    size_t total_len = 0;
    for (int i = 0; i < arr->count; i++) {
        if (i > 0) total_len += sep_len;
        SlpObjString *text = slp_vm_stringify(vm, arr->elements[i]);
        if (text) total_len += text->length;
    }

    char *buf = stdlib_alloc(vm, total_len + 1);
    if (!buf) return SLP_NULL_VAL;
    char *curr = buf;
    for (int i = 0; i < arr->count; i++) {
        if (i > 0) {
            memcpy(curr, separator->chars, sep_len);
            curr += sep_len;
        }
        SlpObjString *text = slp_vm_stringify(vm, arr->elements[i]);
        if (text) {
            memcpy(curr, text->chars, text->length);
            curr += text->length;
        }
    }
    *curr = '\0';

    SlpObjString *res = slp_vm_copy_string(vm, buf, (uint32_t)total_len);
    stdlib_free(vm, buf);
    return SLP_OBJ_VAL(res);
}

static SlpValue builtin_tr(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 3 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING ||
        !SLP_IS_OBJ(args[2]) || SLP_OBJ_TYPE(args[2]) != SLP_OBJ_STRING)
        return args[0];
    SlpObjString *str = SLP_AS_STRING(args[0]);
    SlpObjString *from = SLP_AS_STRING(args[1]);
    SlpObjString *to = SLP_AS_STRING(args[2]);
    char *buf = stdlib_alloc(vm, str->length + 1);
    for (uint32_t i = 0; i < str->length; i++) {
        char c = str->chars[i];
        for (uint32_t j = 0; j < from->length && j < to->length; j++) {
            if (c == from->chars[j]) { c = to->chars[j]; break; }
        }
        buf[i] = c;
    }
    buf[str->length] = '\0';
    SlpObjString *result = slp_vm_copy_string(vm, buf, str->length);
    stdlib_free(vm, buf);
    return SLP_OBJ_VAL(result);
}

static SlpValue builtin_replaceAt(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 3 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_OBJ(args[1]) ||
        SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING ||
        !stdlib_is_number(args[2]))
        return args[0];
    SlpObjString *str = SLP_AS_STRING(args[0]);
    SlpObjString *rep = SLP_AS_STRING(args[1]);
    int idx = normalize_string_index(
        (int)slp_value_as_number(args[2]), (int)str->length);
    int removed = argc >= 4 && stdlib_is_number(args[3])
                      ? (int)slp_value_as_number(args[3])
                      : (int)rep->length;
    if (idx < 0 || idx > (int)str->length || removed < 0)
        return args[0];
    if (idx + removed > (int)str->length)
        removed = (int)str->length - idx;

    uint32_t new_length =
        str->length - (uint32_t)removed + rep->length;
    char *buf = stdlib_alloc(vm, (size_t)new_length + 1);
    if (!buf) return SLP_NULL_VAL;
    memcpy(buf, str->chars, (size_t)idx);
    memcpy(buf + idx, rep->chars, rep->length);
    memcpy(buf + idx + rep->length, str->chars + idx + removed,
           str->length - (uint32_t)idx - (uint32_t)removed);
    buf[new_length] = '\0';
    SlpObjString *result = slp_vm_copy_string(vm, buf, new_length);
    stdlib_free(vm, buf);
    return SLP_OBJ_VAL(result);
}

static SlpValue builtin_lindexOf(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    SlpObjString *haystack = SLP_AS_STRING(args[0]);
    SlpObjString *needle = SLP_AS_STRING(args[1]);
    int start = argc >= 3 && stdlib_is_number(args[2])
                    ? normalize_string_index(
                          (int)slp_value_as_number(args[2]),
                          (int)haystack->length)
                    : (int)haystack->length;
    if (start > (int)haystack->length) start = (int)haystack->length;
    if (start < 0) return SLP_NULL_VAL;
    if (needle->length == 0) return SLP_NUM_VAL((double)start);
    int last_start = start;
    if (last_start + (int)needle->length > (int)haystack->length)
        last_start = (int)haystack->length - (int)needle->length;
    for (int i = last_start; i >= 0; i--) {
        if (memcmp(haystack->chars + i, needle->chars,
                   needle->length) == 0)
            return SLP_NUM_VAL((double)i);
    }
    return SLP_NULL_VAL;
}

/* -----------------------------------------------------------------------
 * Collection & Hash Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_local(SlpVM *vm, SlpValue *args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (SLP_IS_OBJ(args[i]) && SLP_OBJ_TYPE(args[i]) == SLP_OBJ_STRING)
            slp_vm_declare_local(vm, SLP_AS_STRING(args[i])->chars);
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_this(SlpVM *vm, SlpValue *args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (SLP_IS_OBJ(args[i]) && SLP_OBJ_TYPE(args[i]) == SLP_OBJ_STRING)
            slp_vm_declare_closure(vm, SLP_AS_STRING(args[i])->chars);
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_watch(
    SlpVM *vm, SlpValue *args, int argc) {
    bool success = true;
    for (int i = 0; i < argc; i++) {
        SlpObjString *declarations =
            slp_vm_stringify(vm, args[i]);
        if (!declarations ||
            !slp_vm_watch_variables(
                vm, declarations->chars))
            success = false;
    }
    if (!success)
        slp_vm_abort_warning(
            vm,
            "watch(): variables must already exist in a scope");
    return SLP_NULL_VAL;
}

static SlpValue builtin_int(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    if (!stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL((double)(int)slp_value_as_number(args[0]));
}

static SlpValue builtin_long(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    if (SLP_IS_OBJ(args[0]) &&
        SLP_OBJ_TYPE(args[0]) == SLP_OBJ_LONG)
        return args[0];
    if (stdlib_is_number(args[0]))
        return SLP_OBJ_VAL(
            slp_vm_new_long(
                vm, (int64_t)slp_value_as_number(args[0])));
    return SLP_NULL_VAL;
}

static SlpValue builtin_uint(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL((double)(unsigned int)slp_value_as_number(args[0]));
}

static SlpValue builtin_chr(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NULL_VAL;
    char buf[2] = { (char)(int)slp_value_as_number(args[0]), '\0' };
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, buf, 1));
}

static SlpValue builtin_asc(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NUM_VAL(0);
    SlpObjString *str = SLP_AS_STRING(args[0]);
    if (str->length == 0) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL((double)(unsigned char)str->chars[0]);
}

static SlpValue builtin_charAt(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !stdlib_is_number(args[1]))
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    int idx = normalize_string_index(
        (int)slp_value_as_number(args[1]), (int)str->length);
    if (idx < 0 || idx >= (int)str->length) return SLP_NULL_VAL;
    char buf[2] = { str->chars[idx], '\0' };
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, buf, 1));
}

static SlpValue builtin_left(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !stdlib_is_number(args[1]))
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    int n = (int)slp_value_as_number(args[1]);
    return sleep_substring(vm, str, 0, n, "&left");
}

static SlpValue builtin_right(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !stdlib_is_number(args[1]))
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    int n = (int)slp_value_as_number(args[1]);
    return sleep_substring(
        vm, str, -n, (int)str->length, "&right");
}

static SlpValue builtin_mid(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !stdlib_is_number(args[1]))
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    int start = (int)slp_value_as_number(args[1]);
    int len = argc >= 3 && stdlib_is_number(args[2])
                  ? (int)slp_value_as_number(args[2])
                  : (int)str->length - start;
    return sleep_substring(vm, str, start, start + len, "&mid");
}

static SlpValue builtin_find(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    SlpObjString *haystack = SLP_AS_STRING(args[0]);
    SlpObjString *pattern = SLP_AS_STRING(args[1]);
    int start = argc >= 3 && stdlib_is_number(args[2])
                    ? (int)slp_value_as_number(args[2])
                    : 0;
    start = normalize_string_index(start, (int)haystack->length);
    if (start < 0) start = 0;
    int match_start = slp_vm_regex_find(
        vm, haystack->chars, pattern->chars, start, false, NULL);
    return match_start < 0 ? SLP_NULL_VAL
                           : SLP_NUM_VAL((double)match_start);
}

static SlpValue builtin_indexOf(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_OBJ(args[1]) ||
        SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    SlpObjString *haystack = SLP_AS_STRING(args[0]);
    SlpObjString *needle = SLP_AS_STRING(args[1]);
    int from = argc >= 3 && stdlib_is_number(args[2])
                   ? (int)slp_value_as_number(args[2])
                   : 0;
    from = normalize_string_index(from, (int)haystack->length);
    if (needle->length == 0) return SLP_NUM_VAL((double)from);
    if (from < 0 || from >= (int)haystack->length) return SLP_NULL_VAL;
    const char *p = strstr(haystack->chars + from, needle->chars);
    if (!p) return SLP_NULL_VAL;
    return SLP_NUM_VAL((double)(p - haystack->chars));
}

static SlpValue builtin_formatNumber(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !stdlib_is_number(args[0]) || !SLP_IS_OBJ(args[1]) ||
        SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    double val = slp_value_as_number(args[0]);
    const char *fmt = SLP_AS_STRING(args[1])->chars;
    char buf[128];
    snprintf(buf, sizeof(buf), fmt, val);
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, buf, (uint32_t)strlen(buf)));
}

static SlpValue builtin_cast(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2) return SLP_NULL_VAL;
    SlpValue val = args[0];
    SlpValue type_value = args[1];

    /*
     * Sleep's one-character cast descriptors create Java primitive arrays.
     * Retain their object identity in the portable VM while preserving the
     * original scalar/array as a payload for scalar().
     */
    const char *array_class = NULL;
    char object_array_class[512];
    if (SLP_IS_OBJ(type_value) &&
        SLP_OBJ_TYPE(type_value) == SLP_OBJ_CLASS) {
        SlpObjClass *class_object =
            SLP_AS_CLASS(type_value);
        int written = snprintf(
            object_array_class,
            sizeof(object_array_class),
            "[L%s;", class_object->name->chars);
        if (written > 0 &&
            (size_t)written <
                sizeof(object_array_class))
            array_class = object_array_class;
    } else if (SLP_IS_OBJ(type_value) &&
               SLP_OBJ_TYPE(type_value) ==
                   SLP_OBJ_STRING) {
        SlpObjString *type =
            SLP_AS_STRING(type_value);
        if (type->length == 1) {
            switch (type->chars[0]) {
            case 'z': array_class = "[Z"; break;
            case 'c': array_class = "[C"; break;
            case 'b': array_class = "[B"; break;
            case 'h': array_class = "[S"; break;
            case 'i': array_class = "[I"; break;
            case 'l': array_class = "[J"; break;
            case 'f': array_class = "[F"; break;
            case 'd': array_class = "[D"; break;
            case 'o':
                array_class =
                    "[Ljava.lang.Object;";
                break;
            default: break;
            }
        }
    }

    if (array_class &&
        ((SLP_IS_OBJ(val) &&
          SLP_OBJ_TYPE(val) == SLP_OBJ_ARRAY) ||
         ((strcmp(array_class, "[B") == 0 ||
           strcmp(array_class, "[C") == 0) &&
          SLP_IS_OBJ(val) &&
          SLP_OBJ_TYPE(val) == SLP_OBJ_STRING))) {
        SlpObjJavaObject *array =
            slp_vm_new_java_object(
                vm, array_class, SLP_JAVA_GENERIC);
        if (!array) return SLP_NULL_VAL;
        array->value = val;
        return SLP_OBJ_VAL(array);
    }

    if (!SLP_IS_OBJ(type_value) ||
        SLP_OBJ_TYPE(type_value) != SLP_OBJ_STRING)
        return val;
    SlpObjString *type = SLP_AS_STRING(type_value);
    if (strcmp(type->chars, "int") == 0) {
        if (stdlib_is_number(val)) return SLP_NUM_VAL((double)(int)slp_value_as_number(val));
        if (SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING)
            return SLP_NUM_VAL((double)atoi(SLP_AS_STRING(val)->chars));
        return SLP_NUM_VAL(0);
    }
    if (strcmp(type->chars, "double") == 0 || strcmp(type->chars, "number") == 0) {
        if (stdlib_is_number(val)) return val;
        if (SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING)
            return SLP_NUM_VAL(atof(SLP_AS_STRING(val)->chars));
        return SLP_NUM_VAL(0);
    }
    if (strcmp(type->chars, "string") == 0) {
        if (SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING) return val;
        if (stdlib_is_number(val)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", slp_value_as_number(val));
            return SLP_OBJ_VAL(slp_vm_copy_string(vm, buf, (uint32_t)strlen(buf)));
        }
        if (SLP_IS_BOOL(val))
            return SLP_OBJ_VAL(slp_vm_copy_string(vm, SLP_AS_BOOL(val) ? "true" : "false", SLP_AS_BOOL(val) ? 4 : 5));
        if (SLP_IS_NULL(val))
            return SLP_OBJ_VAL(slp_vm_copy_string(vm, "$null", 5));
        return SLP_NULL_VAL;
    }
    return val;
}

static SlpValue builtin_casti(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2) return SLP_NULL_VAL;
    SlpObjString *descriptor =
        slp_vm_stringify(vm, args[1]);
    if (!descriptor || descriptor->length != 1) {
        slp_vm_abort_warning(
            vm,
            "&casti: invalid primitive cast identifier");
        return SLP_NULL_VAL;
    }

    const char *class_name = NULL;
    SlpValue payload = SLP_NULL_VAL;
    SlpValue source =
        stdlib_dereference(args[0]);
    switch (descriptor->chars[0]) {
    case 'z':
        class_name = "java.lang.Boolean";
        payload = SLP_BOOL_VAL(
            slp_value_as_number(source) != 0.0);
        break;
    case 'c': {
        class_name = "java.lang.Character";
        SlpObjString *text =
            slp_vm_stringify(vm, source);
        if (!text || text->length == 0) {
            slp_vm_abort_warning(
                vm,
                "&casti: cannot cast an empty value to char");
            return SLP_NULL_VAL;
        }
        SlpObjString *character =
            slp_vm_copy_string(
                vm, text->chars, 1);
        payload = character
            ? SLP_OBJ_VAL(character)
            : SLP_NULL_VAL;
        break;
    }
    case 'b':
        class_name = "java.lang.Byte";
        payload = SLP_NUM_VAL(
            (double)(int8_t)stdlib_as_int64(source));
        break;
    case 'h':
        class_name = "java.lang.Short";
        payload = SLP_NUM_VAL(
            (double)(int16_t)stdlib_as_int64(source));
        break;
    case 'i':
        class_name = "java.lang.Integer";
        payload = SLP_NUM_VAL(
            (double)(int32_t)stdlib_as_int64(source));
        break;
    case 'l': {
        class_name = "java.lang.Long";
        SlpObjLong *number =
            slp_vm_new_long(
                vm, stdlib_as_int64(source));
        payload = number
            ? SLP_OBJ_VAL(number)
            : SLP_NULL_VAL;
        break;
    }
    case 'f':
    case 'd': {
        class_name = descriptor->chars[0] == 'f'
            ? "java.lang.Float"
            : "java.lang.Double";
        double converted =
            slp_value_as_number(source);
        if (descriptor->chars[0] == 'f')
            converted = (double)(float)converted;
        SlpObjDouble *number =
            slp_vm_new_double(vm, converted);
        payload = number
            ? SLP_OBJ_VAL(number)
            : SLP_NULL_VAL;
        break;
    }
    case 'o':
        return source;
    default:
        slp_vm_abort_warning(
            vm,
            "&casti: invalid primitive cast identifier");
        return SLP_NULL_VAL;
    }

    SlpObjJavaObject *wrapper =
        slp_vm_new_java_object(
            vm, class_name, SLP_JAVA_GENERIC);
    if (!wrapper) return SLP_NULL_VAL;
    wrapper->value = payload;
    return SLP_OBJ_VAL(wrapper);
}

static SlpValue builtin_scalar(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NULL_VAL;
    SlpValue value = stdlib_dereference(args[0]);
    if (SLP_IS_OBJ(value) &&
        SLP_OBJ_TYPE(value) == SLP_OBJ_JAVA_OBJECT) {
        SlpObjJavaObject *object =
            SLP_AS_JAVA_OBJECT(value);
        if (!SLP_IS_NULL(object->value))
            return object->value;
    }
    return value;
}

static SlpValue builtin_inline(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 ||
        !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) !=
            SLP_OBJ_CLOSURE)
        return SLP_NULL_VAL;
    SlpObjClosure *closure =
        slp_vm_clone_closure(
            vm, SLP_AS_CLOSURE(args[0]));
    if (!closure) return SLP_NULL_VAL;
    closure->is_inline = true;
    slp_vm_push(
        vm, SLP_OBJ_VAL(closure));
    if (slp_vm_call(
            vm, 0, false) != SLP_OK)
        return SLP_NULL_VAL;
    slp_vm_pop(vm);
    return SLP_NULL_VAL;
}

static void system_property_set(
    SlpVM *vm, SlpObjHash *properties,
    const char *name, const char *value) {
    SlpObjString *key =
        slp_vm_copy_cstr(vm, name);
    SlpObjString *property =
        slp_vm_copy_cstr(vm, value ? value : "");
    if (key && property)
        slp_vm_hash_set(
            vm, properties,
            SLP_OBJ_VAL(key),
            SLP_OBJ_VAL(property));
}

static SlpValue builtin_systemProperties(
    SlpVM *vm, SlpValue *args, int argc) {
    (void)args;
    (void)argc;
    SlpObjHash *properties =
        slp_vm_new_hash(vm);
    if (!properties) return SLP_NULL_VAL;
    SlpValue result =
        SLP_OBJ_VAL(properties);
    slp_vm_push(vm, result);

    char working_directory[2048];
    const char *cwd =
        slp_platform_getcwd(
            working_directory,
            sizeof(working_directory))
            ? working_directory
            : "";
    char file_separator[2] = {
        slp_platform_path_separator(), '\0'};
#ifdef _WIN32
    const char *os_name = "Windows";
    const char *path_separator = ";";
#else
#if defined(__APPLE__)
    const char *os_name = "Mac OS X";
#else
    const char *os_name = "Linux";
#endif
    const char *path_separator = ":";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    const char *os_arch = "aarch64";
#elif defined(__x86_64__) || defined(_M_X64)
    const char *os_arch = "amd64";
#elif defined(__i386__) || defined(_M_IX86)
    const char *os_arch = "x86";
#else
    const char *os_arch = "unknown";
#endif

    system_property_set(
        vm, properties, "user.dir", cwd);
    system_property_set(
        vm, properties, "os.name", os_name);
    system_property_set(
        vm, properties, "os.arch", os_arch);
    system_property_set(
        vm, properties, "file.separator",
        file_separator);
    system_property_set(
        vm, properties, "path.separator",
        path_separator);
    system_property_set(
        vm, properties, "line.separator", "\n");
    system_property_set(
        vm, properties, "file.encoding", "UTF-8");

    slp_vm_pop(vm);
    return result;
}

static SlpValue builtin_sleep_ms(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NULL_VAL;
    double ms = slp_value_as_number(args[0]);
    slp_platform_sleep_ms((unsigned int)ms);
    return SLP_NULL_VAL;
}

static SlpValue builtin_srand(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc >= 1 && stdlib_is_number(args[0])) {
        srand((unsigned int)slp_value_as_number(args[0]));
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_degrees(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(slp_value_as_number(args[0]) * 180.0 / 3.14159265358979323846);
}

static SlpValue builtin_radians(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !stdlib_is_number(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(slp_value_as_number(args[0]) * 3.14159265358979323846 / 180.0);
}

static SlpValue builtin_min(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2 || !stdlib_is_number(args[0]) || !stdlib_is_number(args[1]))
        return SLP_NUM_VAL(0);
    double a = slp_value_as_number(args[0]), b = slp_value_as_number(args[1]);
    return SLP_NUM_VAL(a < b ? a : b);
}

static SlpValue builtin_max(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2 || !stdlib_is_number(args[0]) || !stdlib_is_number(args[1]))
        return SLP_NUM_VAL(0);
    double a = slp_value_as_number(args[0]), b = slp_value_as_number(args[1]);
    return SLP_NUM_VAL(a > b ? a : b);
}

/* -----------------------------------------------------------------------
 * Array Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_add(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0])) return SLP_NULL_VAL;
    if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_ARRAY) {
        SlpObjArray *array = SLP_AS_ARRAY(args[0]);
        int index = argc >= 3 && stdlib_is_number(args[2])
                        ? (int)slp_value_as_number(args[2])
                        : 0;
        if (index < 0) index += array->count + 1;
        if (index < 0 || index > array->count) {
            char message[128];
            snprintf(
                message, sizeof(message),
                "attempted an invalid index: Index: %d, Size: %d",
                index, array->count);
            slp_vm_abort_warning(vm, message);
            return SLP_NULL_VAL;
        }
        slp_obj_array_insert(vm->allocator, array, index, args[1]);
        return args[0];
    }
    if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_HASH) {
        SlpObjHash *hash = SLP_AS_HASH(args[0]);
        for (int i = 1; i < argc; i++) {
            if (SLP_IS_OBJ(args[i]) &&
                SLP_OBJ_TYPE(args[i]) == SLP_OBJ_KEY_VALUE) {
                SlpObjKeyValue *pair = SLP_AS_KEY_VALUE(args[i]);
                slp_vm_hash_set(vm, hash, pair->key, pair->value);
            }
        }
        return args[0];
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_copy(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0])) return SLP_NULL_VAL;
    if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_ARRAY) {
        SlpObjArray *src = SLP_AS_ARRAY(args[0]);
        SlpObjArray *dst = slp_vm_new_array(vm);
        for (int i = 0; i < src->count; i++)
            slp_obj_array_push(vm->allocator, dst, src->elements[i]);
        return SLP_OBJ_VAL(dst);
    }
    if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_HASH) {
        SlpObjHash *src = SLP_AS_HASH(args[0]);
        SlpObjHash *dst = slp_vm_new_hash(vm);
        dst->order_mode = src->order_mode;
        dst->miss_policy = src->miss_policy;
        dst->removal_policy = src->removal_policy;
        for (int ordinal = 0; ordinal < src->count; ordinal++) {
            int i = slp_obj_hash_ordered_index(src, ordinal);
            if (i >= 0)
                slp_obj_hash_set(vm->allocator, dst, src->entries[i].key,
                                 src->entries[i].value);
        }
        return SLP_OBJ_VAL(dst);
    }
    return args[0];
}

static SlpValue builtin_clear(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0])) return SLP_NULL_VAL;
    if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_ARRAY) {
        SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
        while (arr->count > 0)
            slp_obj_array_remove_at(arr, arr->count - 1);
    } else if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_HASH) {
        SlpObjHash *hash = SLP_AS_HASH(args[0]);
        for (int i = 0; i < hash->capacity; i++) {
            hash->entries[i].key = SLP_NULL_VAL;
            hash->entries[i].value = SLP_NULL_VAL;
            hash->entries[i].sequence = 0;
        }
        hash->count = 0;
    }
    return SLP_NULL_VAL;
}

static int cmp_nums_asc(const void *a, const void *b) {
    double da = slp_value_as_number(*(const SlpValue*)a);
    double db = slp_value_as_number(*(const SlpValue*)b);
    return (da > db) - (da < db);
}

static int cmp_nums_desc(const void *a, const void *b) {
    return -cmp_nums_asc(a, b);
}

typedef enum {
    SLP_SORT_ASCII,
    SLP_SORT_INTEGER,
    SLP_SORT_DOUBLE
} SlpSortMode;

static int compare_sort_values(SlpVM *vm, SlpValue left, SlpValue right,
                               SlpSortMode mode) {
    if (mode == SLP_SORT_ASCII) {
        SlpObjString *left_text = slp_vm_stringify(vm, left);
        SlpObjString *right_text = slp_vm_stringify(vm, right);
        if (!left_text || !right_text) return 0;
        return strcmp(left_text->chars, right_text->chars);
    }
    if (mode == SLP_SORT_INTEGER) {
        int64_t left_number =
            SLP_IS_OBJ(left) && SLP_OBJ_TYPE(left) == SLP_OBJ_LONG
                ? SLP_AS_LONG(left)->value
                : (int64_t)slp_value_as_number(left);
        int64_t right_number =
            SLP_IS_OBJ(right) && SLP_OBJ_TYPE(right) == SLP_OBJ_LONG
                ? SLP_AS_LONG(right)->value
                : (int64_t)slp_value_as_number(right);
        return (left_number > right_number) - (left_number < right_number);
    }
    double left_number = slp_value_as_number(left);
    double right_number = slp_value_as_number(right);
    return (left_number > right_number) - (left_number < right_number);
}

static SlpValue sort_array_values(SlpVM *vm, SlpValue value,
                                  SlpSortMode mode) {
    if (!SLP_IS_OBJ(value) || SLP_OBJ_TYPE(value) != SLP_OBJ_ARRAY)
        return value;
    SlpObjArray *array = SLP_AS_ARRAY(value);
    for (int i = 1; i < array->count; i++) {
        SlpValue current = array->elements[i];
        int position = i;
        while (position > 0 &&
               compare_sort_values(vm, array->elements[position - 1],
                                   current, mode) > 0) {
            array->elements[position] = array->elements[position - 1];
            position--;
        }
        array->elements[position] = current;
    }
    slp_obj_array_sync_view(vm->allocator, array);
    return value;
}

static SlpValue builtin_sort(SlpVM *vm, SlpValue *args, int argc) {
    if (argc != 2 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_CLOSURE ||
        !SLP_IS_OBJ(args[1]) ||
        SLP_OBJ_TYPE(args[1]) != SLP_OBJ_ARRAY)
        return SLP_OBJ_VAL(slp_vm_new_array(vm));

    SlpObjClosure *comparison = SLP_AS_CLOSURE(args[0]);
    SlpObjArray *array = SLP_AS_ARRAY(args[1]);
    for (int i = 1; i < array->count; i++) {
        SlpValue current = array->elements[i];
        int position = i;
        while (position > 0) {
            slp_vm_push(vm, SLP_OBJ_VAL(comparison));
            slp_vm_push(vm, array->elements[position - 1]);
            slp_vm_push(vm, current);
            if (slp_vm_call(vm, 2, false) != SLP_OK)
                return SLP_NULL_VAL;
            SlpValue result = slp_vm_pop(vm);
            if (slp_value_as_number(result) <= 0) break;
            array->elements[position] = array->elements[position - 1];
            position--;
        }
        array->elements[position] = current;
    }
    slp_obj_array_sync_view(vm->allocator, array);
    return args[1];
}

static SlpValue builtin_sorta(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_OBJ_VAL(slp_vm_new_array(vm));
    return sort_array_values(vm, args[0], SLP_SORT_ASCII);
}

static SlpValue builtin_sortd(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_OBJ_VAL(slp_vm_new_array(vm));
    return sort_array_values(vm, args[0], SLP_SORT_DOUBLE);
}

static SlpValue builtin_sortn(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_OBJ_VAL(slp_vm_new_array(vm));
    return sort_array_values(vm, args[0], SLP_SORT_INTEGER);
}

static SlpValue builtin_reverse(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY)
        return args[0];
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    for (int i = 0; i < arr->count / 2; i++) {
        SlpValue tmp = arr->elements[i];
        arr->elements[i] = arr->elements[arr->count - 1 - i];
        arr->elements[arr->count - 1 - i] = tmp;
    }
    slp_obj_array_sync_view(vm->allocator, arr);
    return args[0];
}

static SlpValue builtin_shift(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY)
        return SLP_NULL_VAL;
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    if (arr->count == 0) return SLP_NULL_VAL;
    return slp_obj_array_remove_at(arr, 0);
}

static void flatten_value(SlpVM *vm, SlpValue value, SlpObjArray *output) {
    if (SLP_IS_OBJ(value) && SLP_OBJ_TYPE(value) == SLP_OBJ_ARRAY) {
        SlpObjArray *array = SLP_AS_ARRAY(value);
        for (int i = 0; i < array->count; i++)
            flatten_value(vm, array->elements[i], output);
        return;
    }
    slp_obj_array_push(vm->allocator, output, value);
}

static SlpValue builtin_flatten(SlpVM *vm, SlpValue *args, int argc) {
    SlpObjArray *dst = slp_vm_new_array(vm);
    if (!dst) return SLP_NULL_VAL;
    if (argc < 1) return SLP_OBJ_VAL(dst);

    SlpObjArray *values = slp_vm_new_array(vm);
    if (!values) return SLP_NULL_VAL;
    slp_vm_push(vm, SLP_OBJ_VAL(dst));
    slp_vm_push(vm, SLP_OBJ_VAL(values));
    bool collected = collect_iterator_values(vm, args[0], values);
    slp_vm_pop(vm);
    if (collected) {
        for (int i = 0; i < values->count; i++)
            flatten_value(vm, values->elements[i], dst);
    }
    slp_vm_pop(vm);
    return SLP_OBJ_VAL(dst);
}

static SlpValue builtin_concat(SlpVM *vm, SlpValue *args, int argc) {
    SlpObjArray *result = slp_vm_new_array(vm);
    if (!result) return SLP_NULL_VAL;
    for (int i = 0; i < argc; i++) {
        if (SLP_IS_OBJ(args[i]) &&
            SLP_OBJ_TYPE(args[i]) == SLP_OBJ_ARRAY) {
            SlpObjArray *array = SLP_AS_ARRAY(args[i]);
            for (int j = 0; j < array->count; j++)
                slp_obj_array_push(vm->allocator, result,
                                   array->elements[j]);
        } else {
            slp_obj_array_push(vm->allocator, result, args[i]);
        }
    }
    return SLP_OBJ_VAL(result);
}

typedef struct {
    SlpValue source;
    int index;
    bool done;
} SlpStdlibIterator;

static bool stdlib_iterator_next(SlpVM *vm, SlpStdlibIterator *iterator,
                                 SlpValue *value) {
    if (iterator->done || !SLP_IS_OBJ(iterator->source))
        return false;
    if (SLP_OBJ_TYPE(iterator->source) == SLP_OBJ_ARRAY) {
        SlpObjArray *array = SLP_AS_ARRAY(iterator->source);
        if (iterator->index >= array->count) {
            iterator->done = true;
            return false;
        }
        *value = array->elements[iterator->index++];
        return true;
    }
    if (SLP_OBJ_TYPE(iterator->source) == SLP_OBJ_CLOSURE) {
        slp_vm_push(vm, iterator->source);
        if (slp_vm_call(vm, 0, false) != SLP_OK) {
            iterator->done = true;
            return false;
        }
        *value = slp_vm_pop(vm);
        if (SLP_IS_NULL(*value))
            return false;
        iterator->index++;
        return true;
    }
    if (SLP_OBJ_TYPE(iterator->source) ==
            SLP_OBJ_JAVA_OBJECT &&
        SLP_AS_JAVA_OBJECT(iterator->source)
                ->kind ==
            SLP_JAVA_ITERATOR &&
        SLP_AS_JAVA_OBJECT(iterator->source)
            ->list) {
        SlpObjJavaObject *object =
            SLP_AS_JAVA_OBJECT(
                iterator->source);
        int index =
            (int)slp_value_as_number(
                object->value);
        if (index < 0 ||
            index >= object->list->count) {
            iterator->done = true;
            return false;
        }
        *value =
            object->list->elements[index];
        object->value =
            SLP_NUM_VAL(
                (double)(index + 1));
        iterator->index++;
        return true;
    }
    iterator->done = true;
    return false;
}

static bool stdlib_iterator_init(
    SlpVM *vm, SlpValue source, SlpStdlibIterator *iterator) {
    if (SLP_IS_OBJ(source) &&
        (SLP_OBJ_TYPE(source) == SLP_OBJ_ARRAY ||
         SLP_OBJ_TYPE(source) == SLP_OBJ_CLOSURE ||
         (SLP_OBJ_TYPE(source) ==
              SLP_OBJ_JAVA_OBJECT &&
          SLP_AS_JAVA_OBJECT(source)->kind ==
              SLP_JAVA_ITERATOR &&
          SLP_AS_JAVA_OBJECT(source)->list))) {
        iterator->source = source;
        iterator->index = 0;
        iterator->done = false;
        return true;
    }

    SlpObjString *text = slp_vm_stringify(vm, source);
    char message[256];
    if (SLP_IS_OBJ(source) &&
        SLP_OBJ_TYPE(source) == SLP_OBJ_STRING) {
        snprintf(
            message, sizeof(message),
            "expected iterator (@array or &closure)--received: '%s'",
            text ? text->chars : "");
    } else {
        snprintf(
            message, sizeof(message),
            "expected iterator (@array or &closure)--received: %s",
            text ? text->chars : "");
    }
    slp_vm_abort_warning(vm, message);
    return false;
}

static SlpValue builtin_sum(SlpVM *vm, SlpValue *args, int argc) {
    double result = 0.0;
    if (argc > 0) {
        SlpStdlibIterator primary = {args[0], 0, false};
        SlpStdlibIterator auxiliaries[255];
        int auxiliary_count = argc - 1;
        for (int i = 0; i < auxiliary_count; i++) {
            auxiliaries[i].source = args[i + 1];
            auxiliaries[i].index = 0;
            auxiliaries[i].done = false;
        }

        SlpValue primary_value;
        while (stdlib_iterator_next(vm, &primary, &primary_value)) {
            double product = slp_value_as_number(primary_value);
            for (int i = 0; i < auxiliary_count; i++) {
                SlpValue auxiliary_value;
                if (!stdlib_iterator_next(
                        vm, &auxiliaries[i], &auxiliary_value)) {
                    product = 0.0;
                    break;
                }
                product *= slp_value_as_number(auxiliary_value);
            }
            result += product;
        }
    }
    SlpObjDouble *number = slp_vm_new_double(vm, result);
    return number ? SLP_OBJ_VAL(number) : SLP_NULL_VAL;
}

static SlpValue builtin_removeAt(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]))
        return SLP_NULL_VAL;
    if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_ARRAY) {
        SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
        for (int i = 1; i < argc; i++) {
            if (!stdlib_is_number(args[i])) continue;
            int idx = (int)slp_value_as_number(args[i]);
            if (idx < 0) idx += arr->count;
            if (idx >= 0 && idx < arr->count) {
                slp_obj_array_remove_at(arr, idx);
            } else {
                char message[128];
                snprintf(
                    message, sizeof(message),
                    "attempted an invalid index: Index: %d, Size: %d",
                    idx, arr->count);
                slp_vm_abort_warning(vm, message);
                return SLP_NULL_VAL;
            }
        }
    } else if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_HASH) {
        SlpObjHash *hash = SLP_AS_HASH(args[0]);
        for (int i = 1; i < argc; i++)
            slp_vm_hash_delete(vm, hash, args[i]);
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_putAll(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]))
        return SLP_NULL_VAL;

    if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_ARRAY) {
        SlpObjArray *destination = SLP_AS_ARRAY(args[0]);
        SlpObjArray *values = slp_vm_new_array(vm);
        if (!values) return SLP_NULL_VAL;
        slp_vm_push(vm, SLP_OBJ_VAL(values));
        bool collected =
            collect_iterator_values(vm, args[1], values);
        if (collected) {
            for (int i = 0; i < values->count; i++)
                slp_obj_array_push(
                    vm->allocator, destination, values->elements[i]);
        }
        slp_vm_pop(vm);
        return collected ? args[0] : SLP_NULL_VAL;
    }

    if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_HASH) {
        SlpObjHash *destination = SLP_AS_HASH(args[0]);
        SlpStdlibIterator keys;
        SlpStdlibIterator values;
        if (!stdlib_iterator_init(vm, args[1], &keys))
            return SLP_NULL_VAL;
        if (argc >= 3 &&
            !stdlib_iterator_init(vm, args[2], &values))
            return SLP_NULL_VAL;

        SlpValue key;
        while (stdlib_iterator_next(vm, &keys, &key)) {
            SlpValue value = SLP_NULL_VAL;
            if (argc < 3)
                (void)stdlib_iterator_next(vm, &keys, &value);
            else
                (void)stdlib_iterator_next(vm, &values, &value);

            if (SLP_IS_NULL(value))
                slp_vm_hash_delete(vm, destination, key);
            else
                slp_vm_hash_set(vm, destination, key, value);
            if (vm->halted)
                return SLP_NULL_VAL;
        }
        return vm->halted ? SLP_NULL_VAL : args[0];
    }

    return SLP_NULL_VAL;
}

static SlpValue builtin_contains(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2) return SLP_BOOL_VAL(false);
    SlpValue item = args[0];
    SlpValue container = args[1];
    if (SLP_IS_OBJ(container)) {
        if (SLP_OBJ_TYPE(container) == SLP_OBJ_ARRAY) {
            SlpObjArray *arr = SLP_AS_ARRAY(container);
            for (int i = 0; i < arr->count; i++) {
                if (slp_value_equals(arr->elements[i], item))
                    return SLP_BOOL_VAL(true);
            }
        } else if (SLP_OBJ_TYPE(container) == SLP_OBJ_HASH) {
            return SLP_BOOL_VAL(
                slp_vm_hash_contains(vm, SLP_AS_HASH(container), item));
        }
    }
    return SLP_BOOL_VAL(false);
}

static SlpValue builtin_values(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_HASH) {
        return SLP_OBJ_VAL(slp_vm_new_array(vm));
    }
    SlpObjHash *hash = (SlpObjHash*)SLP_AS_OBJ(args[0]);
    SlpObjArray *arr = slp_vm_new_array(vm);
    if (argc >= 2) {
        SlpObjArray *keys = slp_vm_new_array(vm);
        if (!arr || !keys) return SLP_NULL_VAL;
        slp_vm_push(vm, SLP_OBJ_VAL(arr));
        slp_vm_push(vm, SLP_OBJ_VAL(keys));
        bool collected =
            collect_iterator_values(vm, args[1], keys);
        if (collected) {
            for (int i = 0; i < keys->count; i++)
                slp_obj_array_push(
                    vm->allocator, arr,
                    slp_vm_hash_get(vm, hash, keys->elements[i]));
        }
        slp_vm_pop(vm);
        slp_vm_pop(vm);
        return collected ? SLP_OBJ_VAL(arr) : SLP_NULL_VAL;
    }
    for (int ordinal = 0; ordinal < hash->count; ordinal++) {
        int i = slp_obj_hash_ordered_index(hash, ordinal);
        if (i >= 0 && !SLP_IS_NULL(hash->entries[i].value)) {
            slp_obj_array_push(vm->allocator, arr, hash->entries[i].value);
        }
    }
    return SLP_OBJ_VAL(arr);
}

static SlpValue builtin_sublist(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY ||
                    !stdlib_is_number(args[1])) {
        return SLP_OBJ_VAL(slp_vm_new_array(vm));
    }
    SlpObjArray *arr = (SlpObjArray*)SLP_AS_OBJ(args[0]);
    int original_start = (int)slp_value_as_number(args[1]);
    int original_end = arr->count;
    if (argc >= 3 && stdlib_is_number(args[2])) {
        original_end = (int)slp_value_as_number(args[2]);
    }

    int start =
        original_start < 0 ? original_start + arr->count : original_start;
    int end = original_end < 0
                  ? original_end + arr->count
                  : original_end;
    if (end > arr->count) end = arr->count;
    if (start > end) {
        SlpObjString *array_text =
            slp_vm_stringify(vm, args[0]);
        if (array_text) {
            size_t message_length = array_text->length + 128;
            char *message =
                (char*)stdlib_alloc(vm, message_length);
            if (message) {
                snprintf(
                    message, message_length,
                    "illegal subarray(%s, %d -> %d, %d -> %d)",
                    array_text->chars, original_start, start,
                    original_end, end);
                slp_vm_abort_warning(vm, message);
                stdlib_free(vm, message);
            }
        }
        return SLP_NULL_VAL;
    }
    if (start < 0 || start > arr->count || end < 0)
        return SLP_NULL_VAL;
    
    SlpObjArray *res = slp_vm_new_array(vm);
    for (int i = start; i < end; i++) {
        slp_obj_array_push(vm->allocator, res, arr->elements[i]);
    }
    res->view_source = arr;
    res->view_offset = start;
    res->view_source_version = arr->mutation_version;
    return SLP_OBJ_VAL(res);
}

/* -----------------------------------------------------------------------
 * System & Utility Builtins
 * ----------------------------------------------------------------------- */

static bool print_to_handle(
    SlpVM *vm, SlpValue *args, int argc, bool newline) {
    if (argc < 2)
        return false;
    if (!SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) !=
            SLP_OBJ_IO_HANDLE) {
        SlpObjString *description =
            slp_vm_stringify(vm, args[0]);
        const char *description_text =
            SLP_IS_NULL(args[0])
                ? "$null"
                : (description
                       ? description->chars
                       : "");
        size_t capacity =
            strlen(
                "expected I/O handle argument, received: ") +
            strlen(description_text) +
            (SLP_IS_OBJ(args[0]) &&
             SLP_OBJ_TYPE(args[0]) ==
                 SLP_OBJ_STRING
                 ? 2u
                 : 0u) +
            1;
        char *message =
            (char*)stdlib_alloc(vm, capacity);
        if (message) {
            bool quote =
                SLP_IS_OBJ(args[0]) &&
                SLP_OBJ_TYPE(args[0]) ==
                    SLP_OBJ_STRING;
            snprintf(
                message, capacity,
                quote
                    ? "expected I/O handle argument, "
                      "received: '%s'"
                    : "expected I/O handle argument, "
                      "received: %s",
                description_text);
            slp_vm_abort_warning(vm, message);
            stdlib_free(vm, message);
        }
        return true;
    }
    SlpObjIOHandle *handle = SLP_AS_IO_HANDLE(args[0]);
    if (handle->is_memory &&
        handle->memory_readable)
        return true;
    SlpObjString *text = slp_vm_stringify(vm, args[1]);
    if (text && handle->is_console) {
        slp_vm_write(vm, text->chars);
        if (newline)
            slp_vm_write(vm, "\n");
    } else if (text && handle->file) {
        fwrite(text->chars, 1, text->length, handle->file);
        if (newline)
            fwrite("\n", 1, 1, handle->file);
        fflush(handle->file);
    } else if (
        text && handle->socket_fd != -1) {
#ifdef _WIN32
        send(
            handle->socket_fd, text->chars,
            (int)text->length, 0);
        if (newline)
            send(
                handle->socket_fd, "\n", 1, 0);
#else
        if (handle->is_pipeline) {
            write(
                handle->socket_fd,
                text->chars, text->length);
            if (newline)
                write(
                    handle->socket_fd,
                    "\n", 1);
        } else {
            send(
                handle->socket_fd,
                text->chars, text->length, 0);
            if (newline)
                send(
                    handle->socket_fd,
                    "\n", 1, 0);
        }
#endif
    }
    return true;
}

static SlpValue builtin_print(SlpVM *vm, SlpValue *args, int argc) {
    if (print_to_handle(vm, args, argc, false))
        return SLP_NULL_VAL;
    if (argc >= 1) {
        SlpObjString *text = slp_vm_stringify(vm, args[0]);
        if (text) slp_vm_write(vm, text->chars);
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_println(SlpVM *vm, SlpValue *args, int argc) {
    if (print_to_handle(vm, args, argc, true))
        return SLP_NULL_VAL;
    builtin_print(vm, args, argc);
    if (!vm->abort_requested)
        slp_vm_write(vm, "\n");
    return SLP_NULL_VAL;
}

static SlpValue builtin_printAll(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY)
        return SLP_NULL_VAL;
    SlpObjArray *array = SLP_AS_ARRAY(args[0]);
    for (int i = 0; i < array->count; i++) {
        SlpObjString *text = slp_vm_stringify(vm, array->elements[i]);
        if (text) slp_vm_write(vm, text->chars);
        slp_vm_write(vm, "\n");
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_ticks(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
    return SLP_NUM_VAL((double)clock());
}

static SlpValue builtin_rand(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    double limit = 1.0;
    if (argc >= 1 && stdlib_is_number(args[0])) {
        limit = slp_value_as_number(args[0]);
    }
    double r = (double)rand() / (double)RAND_MAX;
    return SLP_NUM_VAL(r * limit);
}

static SlpValue builtin_tstamp(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    /* Convert 100ns units to ms and offset to Unix Epoch */
    double ms = (double)(uli.QuadPart - 116444736000000000ULL) / 10000.0;
    return SLP_NUM_VAL(ms);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    double ms = (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
    return SLP_NUM_VAL(ms);
#endif
}

/* -----------------------------------------------------------------------
 * Utility Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_typeOf(SlpVM *vm, SlpValue *args, int argc) {
    const char *type_name = "sleep.engine.types.NullValue";
    if (argc > 0) {
        SlpValue value =
            slp_value_unwrap_taint(args[0]);
        if (SLP_IS_BOOL(value)) {
            type_name = SLP_AS_BOOL(value)
                ? "sleep.engine.types.IntValue"
                : "sleep.engine.types.NullValue";
        } else if (SLP_IS_NUM(value)) {
            type_name = "sleep.engine.types.IntValue";
        } else if (SLP_IS_OBJ(value)) {
            switch (SLP_OBJ_TYPE(value)) {
            case SLP_OBJ_STRING:
                type_name = "sleep.engine.types.StringValue";
                break;
            case SLP_OBJ_LONG:
                type_name = "sleep.engine.types.LongValue";
                break;
            case SLP_OBJ_DOUBLE:
                type_name = "sleep.engine.types.DoubleValue";
                break;
            case SLP_OBJ_ARRAY:
                type_name = SLP_AS_ARRAY(value)->read_only
                    ? "sleep.runtime.CollectionWrapper"
                    : "sleep.engine.types.ListContainer";
                break;
            case SLP_OBJ_HASH:
                type_name = SLP_AS_HASH(value)->order_mode == 0
                    ? "sleep.engine.types.HashContainer"
                    : "sleep.engine.types.OrderedHashContainer";
                break;
            default:
                type_name = "sleep.engine.types.ObjectValue";
                break;
            }
        }
    }
    SlpObjClass *type = slp_vm_new_class(vm, type_name);
    return type ? SLP_OBJ_VAL(type) : SLP_NULL_VAL;
}

static SlpValue builtin_getStackTrace(
    SlpVM *vm, SlpValue *args, int argc) {
    (void)args;
    (void)argc;
    if (vm->last_stack_trace)
        return SLP_OBJ_VAL(vm->last_stack_trace);
    SlpObjArray *empty = slp_vm_new_array(vm);
    return empty ? SLP_OBJ_VAL(empty) : SLP_NULL_VAL;
}

static SlpValue builtin_profile(
    SlpVM *vm, SlpValue *args, int argc) {
    (void)args;
    (void)argc;
    SlpObjArray *result =
        slp_vm_new_array(vm);
    if (!result) return SLP_NULL_VAL;
    SlpObjArray *statistics =
        slp_vm_profile_statistics(vm);
    if (statistics) {
        for (int index = 0;
             index < statistics->count;
             index++)
            slp_obj_array_push(
                vm->allocator, result,
                statistics->elements[index]);
    }
    result->read_only = true;
    return SLP_OBJ_VAL(result);
}

static SlpValue builtin_semaphore(
    SlpVM *vm, SlpValue *args, int argc) {
    int64_t initial =
        argc > 0
            ? stdlib_as_int64(args[0])
            : 1;
    if (initial < 0) initial = 0;
    SlpObjJavaObject *semaphore =
        slp_vm_new_java_object(
            vm,
            "sleep.runtime.Semaphore",
            SLP_JAVA_SEMAPHORE);
    if (!semaphore) return SLP_NULL_VAL;
    SlpObjLong *count =
        slp_vm_new_long(vm, initial);
    semaphore->value = count
        ? SLP_OBJ_VAL(count)
        : SLP_NULL_VAL;
    return SLP_OBJ_VAL(semaphore);
}

static SlpValue builtin_acquire(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 ||
        !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) !=
            SLP_OBJ_JAVA_OBJECT ||
        SLP_AS_JAVA_OBJECT(args[0])->kind !=
            SLP_JAVA_SEMAPHORE)
        return SLP_NULL_VAL;
    SlpObjJavaObject *semaphore =
        SLP_AS_JAVA_OBJECT(args[0]);
    int64_t count =
        SLP_IS_OBJ(semaphore->value) &&
                SLP_OBJ_TYPE(
                    semaphore->value) ==
                    SLP_OBJ_LONG
            ? SLP_AS_LONG(
                  semaphore->value)->value
            : 0;
    if (count <= 0) {
        slp_vm_flag_error(
            vm,
            slp_vm_new_error(
                vm,
                "java.lang.IllegalStateException",
                "semaphore would block in "
                "the cooperative runtime",
                "semaphore would block in "
                "the cooperative runtime",
                NULL));
        return SLP_NULL_VAL;
    }
    SlpObjLong *updated =
        slp_vm_new_long(vm, count - 1);
    semaphore->value = updated
        ? SLP_OBJ_VAL(updated)
        : SLP_NULL_VAL;
    return SLP_NULL_VAL;
}

static SlpValue builtin_release(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 ||
        !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) !=
            SLP_OBJ_JAVA_OBJECT ||
        SLP_AS_JAVA_OBJECT(args[0])->kind !=
            SLP_JAVA_SEMAPHORE)
        return SLP_NULL_VAL;
    SlpObjJavaObject *semaphore =
        SLP_AS_JAVA_OBJECT(args[0]);
    int64_t count =
        SLP_IS_OBJ(semaphore->value) &&
                SLP_OBJ_TYPE(
                    semaphore->value) ==
                    SLP_OBJ_LONG
            ? SLP_AS_LONG(
                  semaphore->value)->value
            : 0;
    SlpObjLong *updated =
        slp_vm_new_long(vm, count + 1);
    semaphore->value = updated
        ? SLP_OBJ_VAL(updated)
        : SLP_NULL_VAL;
    return SLP_NULL_VAL;
}

static SlpValue builtin_global(SlpVM *vm, SlpValue *args, int argc) {
    /* Sleep accepts whitespace-separated declaration names. Accept multiple
       string arguments too, matching common Aggressor Script usage. */
    for (int i = 0; i < argc; i++) {
        if (!SLP_IS_OBJ(args[i]) || SLP_OBJ_TYPE(args[i]) != SLP_OBJ_STRING) {
            continue;
        }

        slp_vm_declare_global(vm, SLP_AS_STRING(args[i])->chars);
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_byteAt(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !stdlib_is_number(args[1]))
        return SLP_NUM_VAL(0);
    SlpObjString *str = SLP_AS_STRING(args[0]);
    int idx = (int)slp_value_as_number(args[1]);
    if (idx < 0 || idx >= (int)str->length) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL((double)(unsigned char)str->chars[idx]);
}

/* -----------------------------------------------------------------------
 * Regex & Capture Subgroups
 * ----------------------------------------------------------------------- */

static SlpValue builtin_matched(SlpVM *vm, SlpValue *args, int argc) {
    (void)args; (void)argc;
    SlpObjArray *matches = slp_vm_regex_matches(vm);
    if (matches) {
        return SLP_OBJ_VAL(matches);
    }
    SlpObjArray *empty_arr = slp_vm_new_array(vm);
    return SLP_OBJ_VAL(empty_arr);
}

static SlpValue builtin_matches(SlpVM *vm, SlpValue *args, int argc) {
    SlpObjArray *result = slp_vm_new_array(vm);
    if (!result || argc < 2)
        return result ? SLP_OBJ_VAL(result) : SLP_NULL_VAL;

    SlpObjString *text = slp_vm_stringify(vm, args[0]);
    SlpObjString *pattern = slp_vm_stringify(vm, args[1]);
    if (!text || !pattern)
        return SLP_OBJ_VAL(result);

    int first_match =
        argc >= 3 ? (int)slp_value_as_number(args[2]) : -1;
    int last_match =
        argc >= 4 ? (int)slp_value_as_number(args[3]) : first_match;
    int ordinal = 0;
    int offset = 0;
    int text_length = (int)text->length;

    while (offset <= text_length) {
        int match_end = -1;
        int match_start = slp_vm_regex_find(
            vm, text->chars, pattern->chars,
            offset, false, &match_end);
        if (match_start < 0)
            break;

        if (first_match < 0 ||
            (ordinal >= first_match &&
             ordinal <= last_match)) {
            SlpObjArray *captures =
                slp_vm_regex_matches(vm);
            if (captures) {
                for (int i = 0; i < captures->count; i++) {
                    slp_obj_array_push(
                        vm->allocator, result,
                        captures->elements[i]);
                }
            }
        }
        if (first_match >= 0 && ordinal >= last_match)
            break;

        ordinal++;
        if (match_end > offset)
            offset = match_end;
        else
            offset = match_start + 1;
    }
    return SLP_OBJ_VAL(result);
}

/* -----------------------------------------------------------------------
 * Binary Packing & Unpacking Helper Functions & Structs
 * ----------------------------------------------------------------------- */

static bool is_host_big_endian(void) {
    union {
        uint32_t i;
        char c[4];
    } bint = {0x01020304};
    return bint.c[0] == 1;
}

typedef struct {
    char *data;
    size_t cap;
    size_t len;
} PackBuffer;

static void buf_push_byte(SlpAllocator *alloc, PackBuffer *buf, uint8_t b) {
    if (buf->len >= buf->cap) {
        size_t new_cap = buf->cap == 0 ? 128 : buf->cap * 2;
        buf->data = (char*)alloc->reallocate(buf->data, new_cap, alloc->user_data);
        buf->cap = new_cap;
    }
    buf->data[buf->len++] = (char)b;
}

static void buf_push_bytes(SlpAllocator *alloc, PackBuffer *buf, const uint8_t *bytes, size_t size) {
    for (size_t i = 0; i < size; i++) {
        buf_push_byte(alloc, buf, bytes[i]);
    }
}

static void write_short(SlpAllocator *alloc, PackBuffer *buf, uint16_t val, bool big_endian) {
    uint8_t bytes[2];
    if (big_endian) {
        bytes[0] = (uint8_t)((val >> 8) & 0xFF);
        bytes[1] = (uint8_t)(val & 0xFF);
    } else {
        bytes[0] = (uint8_t)(val & 0xFF);
        bytes[1] = (uint8_t)((val >> 8) & 0xFF);
    }
    buf_push_bytes(alloc, buf, bytes, 2);
}

static void write_int(SlpAllocator *alloc, PackBuffer *buf, uint32_t val, bool big_endian) {
    uint8_t bytes[4];
    if (big_endian) {
        bytes[0] = (uint8_t)((val >> 24) & 0xFF);
        bytes[1] = (uint8_t)((val >> 16) & 0xFF);
        bytes[2] = (uint8_t)((val >> 8) & 0xFF);
        bytes[3] = (uint8_t)(val & 0xFF);
    } else {
        bytes[0] = (uint8_t)(val & 0xFF);
        bytes[1] = (uint8_t)((val >> 8) & 0xFF);
        bytes[2] = (uint8_t)((val >> 16) & 0xFF);
        bytes[3] = (uint8_t)((val >> 24) & 0xFF);
    }
    buf_push_bytes(alloc, buf, bytes, 4);
}

static void write_long(SlpAllocator *alloc, PackBuffer *buf, uint64_t val, bool big_endian) {
    uint8_t bytes[8];
    if (big_endian) {
        bytes[0] = (uint8_t)((val >> 56) & 0xFF);
        bytes[1] = (uint8_t)((val >> 48) & 0xFF);
        bytes[2] = (uint8_t)((val >> 40) & 0xFF);
        bytes[3] = (uint8_t)((val >> 32) & 0xFF);
        bytes[4] = (uint8_t)((val >> 24) & 0xFF);
        bytes[5] = (uint8_t)((val >> 16) & 0xFF);
        bytes[6] = (uint8_t)((val >> 8) & 0xFF);
        bytes[7] = (uint8_t)(val & 0xFF);
    } else {
        bytes[0] = (uint8_t)(val & 0xFF);
        bytes[1] = (uint8_t)((val >> 8) & 0xFF);
        bytes[2] = (uint8_t)((val >> 16) & 0xFF);
        bytes[3] = (uint8_t)((val >> 24) & 0xFF);
        bytes[4] = (uint8_t)((val >> 32) & 0xFF);
        bytes[5] = (uint8_t)((val >> 40) & 0xFF);
        bytes[6] = (uint8_t)((val >> 48) & 0xFF);
        bytes[7] = (uint8_t)((val >> 56) & 0xFF);
    }
    buf_push_bytes(alloc, buf, bytes, 8);
}

static void write_float(SlpAllocator *alloc, PackBuffer *buf, float val, bool big_endian) {
    union {
        float f;
        uint32_t i;
    } u;
    u.f = val;
    write_int(alloc, buf, u.i, big_endian);
}

static void write_double(SlpAllocator *alloc, PackBuffer *buf, double val, bool big_endian) {
    union {
        double d;
        uint64_t i;
    } u;
    u.d = val;
    write_long(alloc, buf, u.i, big_endian);
}

typedef struct {
    SlpValue *args;
    int argc;
    int arg_idx;
    SlpObjArray *array_arg;
    int array_idx;
} PackArgsState;

static SlpValue get_next_arg(PackArgsState *state) {
    if (state->array_arg) {
        if (state->array_idx < state->array_arg->count) {
            return state->array_arg->elements[state->array_idx++];
        }
        return SLP_NULL_VAL;
    }
    if (state->arg_idx < state->argc) {
        return state->args[state->arg_idx++];
    }
    return SLP_NULL_VAL;
}

static uint32_t read_int_bytes(
    const uint8_t *bytes,
    bool big_endian);

static bool pack_object_token(
    SlpVM *vm, PackBuffer *buffer,
    SlpValue value) {
    if (!vm->packed_objects) {
        vm->packed_objects =
            slp_vm_new_array(vm);
        if (!vm->packed_objects)
            return false;
    }
    if (vm->packed_objects->count < 0)
        return false;

    uint32_t index =
        (uint32_t)vm->packed_objects->count;
    SlpValue snapshot =
        clone_serializable(vm, value);
    slp_obj_array_push(
        vm->allocator,
        vm->packed_objects,
        snapshot);

    static const uint8_t magic[4] = {
        'S', 'L', 'P', 'O'
    };
    buf_push_bytes(
        vm->allocator, buffer,
        magic, sizeof(magic));
    write_int(
        vm->allocator, buffer,
        index, true);
    return true;
}

static bool unpack_object_token(
    SlpVM *vm, const uint8_t *data,
    int data_len, int *offset,
    SlpValue *value) {
    static const uint8_t magic[4] = {
        'S', 'L', 'P', 'O'
    };
    if (!offset || !value ||
        *offset < 0 ||
        *offset + 8 > data_len ||
        memcmp(
            data + *offset,
            magic, sizeof(magic)) != 0)
        return false;

    uint32_t index =
        read_int_bytes(
            data + *offset + 4, true);
    *offset += 8;
    if (!vm->packed_objects ||
        index >=
            (uint32_t)vm->packed_objects->count)
        return false;
    *value = clone_serializable(
        vm,
        vm->packed_objects
            ->elements[index]);
    return true;
}

static uint16_t read_short_bytes(const uint8_t *bytes, bool big_endian) {
    if (big_endian) {
        return (uint16_t)((bytes[0] << 8) | bytes[1]);
    } else {
        return (uint16_t)(bytes[0] | (bytes[1] << 8));
    }
}

static uint32_t read_int_bytes(const uint8_t *bytes, bool big_endian) {
    if (big_endian) {
        return ((uint32_t)bytes[0] << 24) |
               ((uint32_t)bytes[1] << 16) |
               ((uint32_t)bytes[2] << 8)  |
               (uint32_t)bytes[3];
    } else {
        return (uint32_t)bytes[0] |
               ((uint32_t)bytes[1] << 8)  |
               ((uint32_t)bytes[2] << 16) |
               ((uint32_t)bytes[3] << 24);
    }
}

static uint64_t read_long_bytes(const uint8_t *bytes, bool big_endian) {
    if (big_endian) {
        return ((uint64_t)bytes[0] << 56) |
               ((uint64_t)bytes[1] << 48) |
               ((uint64_t)bytes[2] << 40) |
               ((uint64_t)bytes[3] << 32) |
               ((uint64_t)bytes[4] << 24) |
               ((uint64_t)bytes[5] << 16) |
               ((uint64_t)bytes[6] << 8)  |
               (uint64_t)bytes[7];
    } else {
        return (uint64_t)bytes[0] |
               ((uint64_t)bytes[1] << 8)  |
               ((uint64_t)bytes[2] << 16) |
               ((uint64_t)bytes[3] << 24) |
               ((uint64_t)bytes[4] << 32) |
               ((uint64_t)bytes[5] << 40) |
               ((uint64_t)bytes[6] << 48) |
               ((uint64_t)bytes[7] << 56);
    }
}

static float read_float_bytes(const uint8_t *bytes, bool big_endian) {
    union {
        uint32_t i;
        float f;
    } u;
    u.i = read_int_bytes(bytes, big_endian);
    return u.f;
}

static double read_double_bytes_from_u(uint64_t val) {
    union {
        uint64_t i;
        double d;
    } u;
    u.i = val;
    return u.d;
}

static double read_double_bytes(const uint8_t *bytes, bool big_endian) {
    union {
        uint64_t i;
        double d;
    } u;
    u.d = read_double_bytes_from_u(read_long_bytes(bytes, big_endian));
    return u.d;
}

/* -----------------------------------------------------------------------
 * Pack & Unpack Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_pack(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    const char *format = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    
    PackArgsState state;
    state.args = args;
    state.argc = argc;
    state.arg_idx = 1;
    state.array_arg = NULL;
    state.array_idx = 0;
    if (argc == 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_ARRAY) {
        state.array_arg = SLP_AS_ARRAY(args[1]);
    }

    PackBuffer buf;
    buf.data = NULL;
    buf.cap = 0;
    buf.len = 0;

    bool pending_big_endian = true;
    bool host_be = is_host_big_endian();

    int len = (int)strlen(format);
    int x = 0;
    while (x < len) {
        char c = format[x];
        if (c == '+') {
            pending_big_endian = true;
            x++;
            continue;
        } else if (c == '-') {
            pending_big_endian = false;
            x++;
            continue;
        } else if (c == '!') {
            pending_big_endian = host_be;
            x++;
            continue;
        }

        if (isalpha((unsigned char)c)) {
            bool field_big_endian =
                pending_big_endian;
            pending_big_endian = true;
            x++;
            int count = 1;
            if (x < len && format[x] == '*') {
                count = -1;
                x++;
            } else if (x < len && isdigit((unsigned char)format[x])) {
                count = 0;
                while (x < len && isdigit((unsigned char)format[x])) {
                    count = count * 10 + (format[x] - '0');
                    x++;
                }
            }
            /*
             * Sleep's DataPattern attaches +, -, and ! to the field that
             * precedes the modifier (for example I-). Continue accepting a
             * leading modifier as a portable extension, but never leak one
             * field's byte order into the next field.
             */
            while (x < len &&
                   (format[x] == '+' ||
                    format[x] == '-' ||
                    format[x] == '!')) {
                if (format[x] == '+')
                    field_big_endian = true;
                else if (format[x] == '-')
                    field_big_endian = false;
                else
                    field_big_endian = host_be;
                x++;
            }

            if (c == 'z' || c == 'Z') {
                SlpValue val = get_next_arg(&state);
                const char *str = "";
                int str_len = 0;
                if (SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING) {
                    str = SLP_AS_STRING(val)->chars;
                    str_len = (int)SLP_AS_STRING(val)->length;
                }
                
                int limit = count;
                if (count == -1) {
                    limit = str_len;
                }
                
                for (int i = 0; i < limit; i++) {
                    if (i < str_len) {
                        buf_push_byte(vm->allocator, &buf, (uint8_t)str[i]);
                    } else {
                        buf_push_byte(vm->allocator, &buf, 0);
                    }
                }
                
                if (c == 'z' || (c == 'Z' && count == -1)) {
                    buf_push_byte(vm->allocator, &buf, 0);
                }
            } else if (c == 'h' || c == 'H') {
                SlpValue val = get_next_arg(&state);
                const char *hex = "";
                int hex_len = 0;
                if (SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING) {
                    hex = SLP_AS_STRING(val)->chars;
                    hex_len = (int)SLP_AS_STRING(val)->length;
                }

                if ((hex_len % 2) != 0) {
                    size_t message_size =
                        (size_t)hex_len + 96;
                    char *message =
                        (char*)SLP_MALLOC(
                            vm->allocator,
                            message_size);
                    if (message) {
                        snprintf(
                            message, message_size,
                            "can not pack '%s' as hex string, "
                            "number of characters must be even",
                            hex);
                        slp_vm_abort_warning(
                            vm, message);
                        SLP_FREE(
                            vm->allocator, message);
                    } else {
                        slp_vm_abort_warning(
                            vm,
                            "can not pack value as hex string, "
                            "number of characters must be even");
                    }
                    if (buf.data)
                        stdlib_free(vm, buf.data);
                    return SLP_NULL_VAL;
                }
                
                for (int i = 0; i + 1 < hex_len; i += 2) {
                    char h1 = hex[i];
                    char h2 = hex[i+1];
                    if (c == 'h') {
                        h1 = hex[i+1];
                        h2 = hex[i];
                    }
                    char byte_str[3] = {h1, h2, '\0'};
                    uint8_t byte_val = (uint8_t)strtol(byte_str, NULL, 16);
                    buf_push_byte(vm->allocator, &buf, byte_val);
                }
            } else {
                int repeat = count;
                if (count == -1) {
                    repeat = state.array_arg ? (state.array_arg->count - state.array_idx) : (state.argc - state.arg_idx);
                }
                
                for (int r = 0; r < repeat; r++) {
                    SlpValue val = SLP_NULL_VAL;
                    if (c != 'x') {
                        val = get_next_arg(&state);
                    }
                    
                    switch (c) {
                        case 'x':
                            buf_push_byte(vm->allocator, &buf, 0);
                            break;
                        case 'C': {
                            const char *str = SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING ? SLP_AS_STRING(val)->chars : "";
                            buf_push_byte(vm->allocator, &buf, (uint8_t)str[0]);
                            break;
                        }
                        case 'c': {
                            const char *str = SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING ? SLP_AS_STRING(val)->chars : "";
                            write_short(vm->allocator, &buf, (uint16_t)str[0], field_big_endian);
                            break;
                        }
                        case 'b':
                        case 'B':
                            buf_push_byte(vm->allocator, &buf, (uint8_t)slp_value_as_number(val));
                            break;
                        case 's':
                        case 'S':
                            write_short(vm->allocator, &buf, (uint16_t)slp_value_as_number(val), field_big_endian);
                            break;
                        case 'i':
                            write_int(vm->allocator, &buf, (uint32_t)(int32_t)slp_value_as_number(val), field_big_endian);
                            break;
                        case 'I':
                            write_int(vm->allocator, &buf, (uint32_t)stdlib_as_int64(val), field_big_endian);
                            break;
                        case 'f':
                            write_float(vm->allocator, &buf, (float)slp_value_as_number(val), field_big_endian);
                            break;
                        case 'd':
                            write_double(vm->allocator, &buf, slp_value_as_number(val), field_big_endian);
                            break;
                        case 'l':
                            write_long(vm->allocator, &buf, (uint64_t)stdlib_as_int64(val), field_big_endian);
                            break;
                        case 'o':
                            if (!pack_object_token(
                                    vm, &buf, val)) {
                                if (buf.data)
                                    stdlib_free(
                                        vm, buf.data);
                                return SLP_NULL_VAL;
                            }
                            break;
                        default:
                            break;
                    }
                }
            }
        } else {
            x++;
        }
    }

    SlpObjString *res = slp_vm_intern_string(vm, buf.data ? buf.data : "", (uint32_t)buf.len);
    if (buf.data) vm->allocator->reallocate(buf.data, 0, vm->allocator->user_data);
    return SLP_OBJ_VAL(res);
}

static SlpValue builtin_unpack(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2) return SLP_NULL_VAL;
    const char *format = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    
    if (!SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING) {
        return SLP_NULL_VAL;
    }
    const uint8_t *data = (const uint8_t *)SLP_AS_STRING(args[1])->chars;
    int data_len = (int)SLP_AS_STRING(args[1])->length;
    int offset = 0;

    SlpObjArray *arr = slp_vm_new_array(vm);
    if (!arr) return SLP_NULL_VAL;

    bool pending_big_endian = true;
    bool host_be = is_host_big_endian();

    int len = (int)strlen(format);
    int x = 0;
    while (x < len && offset < data_len) {
        char c = format[x];
        if (c == '+') {
            pending_big_endian = true;
            x++;
            continue;
        } else if (c == '-') {
            pending_big_endian = false;
            x++;
            continue;
        } else if (c == '!') {
            pending_big_endian = host_be;
            x++;
            continue;
        }

        if (isalpha((unsigned char)c)) {
            bool field_big_endian =
                pending_big_endian;
            pending_big_endian = true;
            x++;
            int count = 1;
            if (x < len && format[x] == '*') {
                count = -1;
                x++;
            } else if (x < len && isdigit((unsigned char)format[x])) {
                count = 0;
                while (x < len && isdigit((unsigned char)format[x])) {
                    count = count * 10 + (format[x] - '0');
                    x++;
                }
            }
            while (x < len &&
                   (format[x] == '+' ||
                    format[x] == '-' ||
                    format[x] == '!')) {
                if (format[x] == '+')
                    field_big_endian = true;
                else if (format[x] == '-')
                    field_big_endian = false;
                else
                    field_big_endian = host_be;
                x++;
            }

            if (c == 'z' || c == 'Z') {
                int start = offset;
                if (count == -1) {
                    while (offset < data_len && data[offset] != '\0') {
                        offset++;
                    }
                    SlpObjString *str = slp_vm_copy_string(vm, (const char *)(data + start), (uint32_t)(offset - start));
                    slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(str));
                    if (offset < data_len && data[offset] == '\0') {
                        offset++;
                    }
                } else {
                    int limit = count;
                    int actual_len = 0;
                    while (actual_len < limit && (offset + actual_len) < data_len && data[offset + actual_len] != '\0') {
                        actual_len++;
                    }
                    SlpObjString *str = slp_vm_copy_string(vm, (const char *)(data + offset), (uint32_t)actual_len);
                    slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(str));
                    offset += limit;
                }
            } else if (c == 'h' || c == 'H') {
                int limit = count;
                if (count == -1) {
                    limit = data_len - offset;
                }
                char *hex_buf = (char*)vm->allocator->reallocate(NULL, (size_t)limit * 2 + 1, vm->allocator->user_data);
                int hex_idx = 0;
                for (int i = 0; i < limit && offset < data_len; i++) {
                    uint8_t val = data[offset++];
                    uint8_t early = (uint8_t)((val & 0xF0) >> 4);
                    uint8_t later = (uint8_t)(val & 0x0F);
                    if (c == 'h') {
                        snprintf(hex_buf + hex_idx, 3, "%x%x", later, early);
                    } else {
                        snprintf(hex_buf + hex_idx, 3, "%x%x", early, later);
                    }
                    hex_idx += 2;
                }
                hex_buf[hex_idx] = '\0';
                SlpObjString *str = slp_vm_copy_string(vm, hex_buf, (uint32_t)hex_idx);
                vm->allocator->reallocate(hex_buf, 0, vm->allocator->user_data);
                slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(str));
            } else {
                int repeat = count;
                if (count == -1) {
                    int unit_size = 1;
                    switch (c) {
                        case 'c': case 's': case 'S': unit_size = 2; break;
                        case 'i': case 'I': case 'f': unit_size = 4; break;
                        case 'd': case 'l': case 'o': unit_size = 8; break;
                        default: unit_size = 1; break;
                    }
                    repeat = (data_len - offset) / unit_size;
                }

                for (int r = 0; r < repeat && offset < data_len; r++) {
                    switch (c) {
                        case 'x':
                            offset++;
                            break;
                        case 'C': {
                            char str[2] = {(char)data[offset++], '\0'};
                            SlpObjString *ch = slp_vm_copy_string(vm, str, 1);
                            slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(ch));
                            break;
                        }
                        case 'c': {
                            if (offset + 2 <= data_len) {
                                uint16_t val = read_short_bytes(data + offset, field_big_endian);
                                char str[2] = {(char)val, '\0'};
                                SlpObjString *ch = slp_vm_copy_string(vm, str, 1);
                                slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(ch));
                                offset += 2;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'b': {
                            int8_t val = (int8_t)data[offset++];
                            slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL((double)val));
                            break;
                        }
                        case 'B': {
                            uint8_t val = data[offset++];
                            slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL((double)val));
                            break;
                        }
                        case 's': {
                            if (offset + 2 <= data_len) {
                                int16_t val = (int16_t)read_short_bytes(data + offset, field_big_endian);
                                slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL((double)val));
                                offset += 2;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'S': {
                            if (offset + 2 <= data_len) {
                                uint16_t val = read_short_bytes(data + offset, field_big_endian);
                                slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL((double)val));
                                offset += 2;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'i': {
                            if (offset + 4 <= data_len) {
                                int32_t val = (int32_t)read_int_bytes(data + offset, field_big_endian);
                                slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL((double)val));
                                offset += 4;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'I': {
                            if (offset + 4 <= data_len) {
                                uint32_t val = read_int_bytes(data + offset, field_big_endian);
                                SlpObjLong *number =
                                    slp_vm_new_long(
                                        vm, (int64_t)val);
                                slp_obj_array_push(
                                    vm->allocator, arr,
                                    number
                                        ? SLP_OBJ_VAL(number)
                                        : SLP_NULL_VAL);
                                offset += 4;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'f': {
                            if (offset + 4 <= data_len) {
                                float val = read_float_bytes(data + offset, field_big_endian);
                                slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL((double)val));
                                offset += 4;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'd': {
                            if (offset + 8 <= data_len) {
                                double val = read_double_bytes(data + offset, field_big_endian);
                                slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL(val));
                                offset += 8;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'l': {
                            if (offset + 8 <= data_len) {
                                int64_t val = (int64_t)read_long_bytes(data + offset, field_big_endian);
                                SlpObjLong *number =
                                    slp_vm_new_long(vm, val);
                                slp_obj_array_push(
                                    vm->allocator, arr,
                                    number
                                        ? SLP_OBJ_VAL(number)
                                        : SLP_NULL_VAL);
                                offset += 8;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'o': {
                            SlpValue value =
                                SLP_NULL_VAL;
                            if (unpack_object_token(
                                    vm, data,
                                    data_len, &offset,
                                    &value)) {
                                slp_obj_array_push(
                                    vm->allocator,
                                    arr, value);
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        } else {
            x++;
        }
    }

    return SLP_OBJ_VAL(arr);
}

/* -----------------------------------------------------------------------
 * Filesystem Bridge Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_ls(SlpVM *vm, SlpValue *args, int argc) {
    SlpObjArray *arr = slp_vm_new_array(vm);
    if (!arr) return SLP_NULL_VAL;
    const char *dir_path = ".";
    if (argc >= 1 && SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
        dir_path = SLP_AS_STRING(args[0])->chars;
    }

    SlpPlatformDir *d = slp_platform_opendir(dir_path);
    if (d) {
        const char *name;
        char sep = slp_platform_path_separator();
        while ((name = slp_platform_readdir(d)) != NULL) {
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s%c%s", dir_path, sep, name);
                SlpObjString *s = slp_vm_copy_cstr(vm, full_path);
                slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(s));
            }
        }
        slp_platform_closedir(d);
    }
    arr->read_only = true;
    return SLP_OBJ_VAL(arr);
}

static SlpValue builtin_listRoots(
    SlpVM *vm, SlpValue *args, int argc) {
    (void)args;
    (void)argc;
    SlpObjArray *roots =
        slp_vm_new_array(vm);
    if (!roots) return SLP_NULL_VAL;
#ifdef _WIN32
    char drives[512];
    DWORD length =
        GetLogicalDriveStringsA(
            (DWORD)sizeof(drives), drives);
    if (length > 0 &&
        length < sizeof(drives)) {
        for (const char *drive = drives;
             *drive;
             drive += strlen(drive) + 1) {
            slp_obj_array_push(
                vm->allocator, roots,
                SLP_OBJ_VAL(
                    slp_vm_copy_cstr(
                        vm, drive)));
        }
    }
#else
    slp_obj_array_push(
        vm->allocator, roots,
        SLP_OBJ_VAL(
            slp_vm_copy_cstr(vm, "/")));
#endif
    roots->read_only = true;
    return SLP_OBJ_VAL(roots);
}

static SlpValue builtin_createNewFile(
    SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NULL_VAL;
    SlpObjString *path =
        slp_vm_stringify(vm, args[0]);
    if (!path) return SLP_NULL_VAL;
    return slp_platform_create_new_file(
               path->chars) == 0
        ? SLP_NUM_VAL(1)
        : SLP_NULL_VAL;
}

static SlpValue builtin_mkdir(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_BOOL_VAL(false);
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    int res = slp_platform_mkdir(path);
    return SLP_BOOL_VAL(res == 0);
}

static SlpValue builtin_deleteFile(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_BOOL_VAL(false);
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    int res = remove(path);
    return SLP_BOOL_VAL(res == 0);
}

static SlpValue builtin_rename(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2) return SLP_BOOL_VAL(false);
    const char *old_path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    const char *new_path = SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[1])->chars : "";
    int res = rename(old_path, new_path);
    return SLP_BOOL_VAL(res == 0);
}

static SlpValue builtin_lof(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    return SLP_NUM_VAL(slp_platform_file_size(path));
}

static SlpValue builtin_lastModified(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    return SLP_NUM_VAL(slp_platform_last_modified(path));
}

static SlpValue builtin_setLastModified(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2) return SLP_NULL_VAL;
    SlpObjString *path =
        slp_vm_stringify(vm, args[0]);
    if (!path) return SLP_NULL_VAL;
    return slp_platform_set_last_modified(
               path->chars,
               stdlib_as_int64(args[1])) == 0
        ? SLP_NUM_VAL(1)
        : SLP_NULL_VAL;
}

static SlpValue builtin_setReadOnly(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    SlpObjString *path =
        slp_vm_stringify(vm, args[0]);
    if (!path) return SLP_NULL_VAL;
    return slp_platform_set_read_only(
               path->chars) == 0
        ? SLP_NUM_VAL(1)
        : SLP_NULL_VAL;
}

static SlpValue builtin_cwd(SlpVM *vm, SlpValue *args, int argc) {
    (void)args; (void)argc;
    char path[1024];
    if (!slp_platform_getcwd(path, sizeof(path))) {
        path[0] = '\0';
    }
    SlpObjString *s = slp_vm_copy_cstr(vm, path);
    return SLP_OBJ_VAL(s);
}

static SlpValue builtin_chdir(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_BOOL_VAL(false);
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    int res = slp_platform_chdir(path);
    return SLP_BOOL_VAL(res == 0);
}

static SlpValue builtin_getFileName(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    const char *filename = strrchr(path, '/');
    if (slp_platform_path_separator() == '\\') {
        const char *filename_win = strrchr(path, '\\');
        if (filename_win && (!filename || filename_win > filename)) {
            filename = filename_win;
        }
    }
    if (filename) {
        filename++;
    } else {
        filename = path;
    }
    SlpObjString *s = slp_vm_copy_cstr(vm, filename);
    return SLP_OBJ_VAL(s);
}

static SlpValue builtin_getFileParent(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    const char *last_sep = strrchr(path, '/');
    if (slp_platform_path_separator() == '\\') {
        const char *last_sep_win = strrchr(path, '\\');
        if (last_sep_win && (!last_sep || last_sep_win > last_sep)) {
            last_sep = last_sep_win;
        }
    }
    if (last_sep && last_sep != path) {
        size_t len = (size_t)(last_sep - path);
        SlpObjString *s = slp_vm_copy_string(vm, path, (uint32_t)len);
        return SLP_OBJ_VAL(s);
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_getFileProper(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    if (!(SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING)) {
        return SLP_NULL_VAL;
    }
    const char *start = SLP_AS_STRING(args[0])->chars;
    char resolved[1024];
    strncpy(resolved, start, sizeof(resolved) - 1);
    resolved[sizeof(resolved) - 1] = '\0';

    char sep = (char)slp_platform_path_separator();

    for (int i = 1; i < argc; i++) {
        if (SLP_IS_OBJ(args[i]) && SLP_OBJ_TYPE(args[i]) == SLP_OBJ_STRING) {
            const char *child = SLP_AS_STRING(args[i])->chars;
            size_t len = strlen(resolved);
            if (len > 0 && resolved[len - 1] != '/' && resolved[len - 1] != '\\') {
                size_t child_len = strlen(child);
                if (len + 1 + child_len < sizeof(resolved)) {
                    resolved[len] = sep;
                    strncpy(resolved + len + 1, child, sizeof(resolved) - len - 1);
                }
            } else {
                size_t child_len = strlen(child);
                if (len + child_len < sizeof(resolved)) {
                    strncpy(resolved + len, child, sizeof(resolved) - len);
                }
            }
        }
    }
    return SLP_OBJ_VAL(slp_vm_copy_cstr(vm, resolved));
}

/* -----------------------------------------------------------------------
 * Date & Time Bridge Builtins
 * ----------------------------------------------------------------------- */

static void map_java_pattern_to_c(const char *java_pat, char *c_pat, size_t c_pat_max) {
    size_t i = 0;
    size_t j = 0;
    size_t len = strlen(java_pat);
    while (i < len && j + 2 < c_pat_max) {
        if (strncmp(java_pat + i, "yyyy", 4) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'Y'; i += 4;
        } else if (strncmp(java_pat + i, "yy", 2) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'y'; i += 2;
        } else if (strncmp(java_pat + i, "MM", 2) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'm'; i += 2;
        } else if (strncmp(java_pat + i, "dd", 2) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'd'; i += 2;
        } else if (strncmp(java_pat + i, "HH", 2) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'H'; i += 2;
        } else if (strncmp(java_pat + i, "hh", 2) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'I'; i += 2;
        } else if (strncmp(java_pat + i, "mm", 2) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'M'; i += 2;
        } else if (strncmp(java_pat + i, "ss", 2) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'S'; i += 2;
        } else if (java_pat[i] == 'a') {
            c_pat[j++] = '%'; c_pat[j++] = 'p'; i++;
        } else if (java_pat[i] == 'z') {
            c_pat[j++] = '%'; c_pat[j++] = 'Z'; i++;
        } else if (java_pat[i] == 'Z') {
            c_pat[j++] = '%'; c_pat[j++] = 'z'; i++;
        } else {
            c_pat[j++] = java_pat[i++];
        }
    }
    c_pat[j] = '\0';
}

static time_t parse_date_custom(const char *date_str, const char *java_pattern) {
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(struct tm));
    tm_val.tm_mday = 1;
    tm_val.tm_year = 70;

    char c_pattern[128];
    map_java_pattern_to_c(java_pattern, c_pattern, sizeof(c_pattern));
    char *res = slp_platform_strptime(date_str, c_pattern, &tm_val);
    if (res) {
        return mktime(&tm_val);
    }

    int year = 1970, month = 1, day = 1, hour = 0, min = 0, sec = 0;
    if (sscanf(date_str, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) >= 3 ||
        sscanf(date_str, "%d/%d/%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) >= 3) {
        tm_val.tm_year = year - 1900;
        tm_val.tm_mon = month - 1;
        tm_val.tm_mday = day;
        tm_val.tm_hour = hour;
        tm_val.tm_min = min;
        tm_val.tm_sec = sec;
        return mktime(&tm_val);
    }
    return 0;
}

static SlpValue builtin_formatDate(SlpVM *vm, SlpValue *args, int argc) {
    long long epoch_ms = 0;
    const char *java_pattern = "";
    
    if (argc == 1) {
#ifdef _WIN32
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER uli;
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        epoch_ms = (long long)((uli.QuadPart - 116444736000000000ULL) / 10000ULL);
#else
        struct timeval tv;
        gettimeofday(&tv, NULL);
        epoch_ms = (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
        if (SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
            java_pattern = SLP_AS_STRING(args[0])->chars;
        }
    } else if (argc >= 2) {
        epoch_ms = (long long)slp_value_as_number(args[0]);
        if (SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING) {
            java_pattern = SLP_AS_STRING(args[1])->chars;
        }
    } else {
        return SLP_NULL_VAL;
    }

    time_t raw_time = (time_t)(epoch_ms / 1000);
    struct tm *timeinfo = localtime(&raw_time);
    
    char c_pattern[128];
    map_java_pattern_to_c(java_pattern, c_pattern, sizeof(c_pattern));

    char out_buf[256];
    size_t written = strftime(out_buf, sizeof(out_buf), c_pattern, timeinfo);
    
    SlpObjString *s = slp_vm_copy_string(vm, out_buf, (uint32_t)written);
    return SLP_OBJ_VAL(s);
}

static SlpValue builtin_parseDate(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2) return SLP_NUM_VAL(0);
    const char *java_pattern = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    const char *date_str = SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[1])->chars : "";
    
    time_t t = parse_date_custom(date_str, java_pattern);
    return SLP_NUM_VAL((double)t * 1000.0);
}

/* -----------------------------------------------------------------------
 * Standalone Cryptographic Helpers (CRC32, MD5, SHA-256)
 * ----------------------------------------------------------------------- */

static uint32_t calculate_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
    }
    return ~crc;
}

#define S11 7
#define S12 12
#define S13 17
#define S14 22
#define S21 5
#define S22 9
#define S23 14
#define S24 20
#define S31 4
#define S32 11
#define S33 16
#define S34 23
#define S41 6
#define S42 10
#define S43 15
#define S44 21

typedef struct {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t buffer[64];
} MD5_CTX;

static uint8_t MD5_PADDING[64] = {
  0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#define MD5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~z)))

#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))

#define FF(a, b, c, d, x, s, ac) { \
 (a) += MD5_F ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
}
#define GG(a, b, c, d, x, s, ac) { \
 (a) += MD5_G ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
}
#define HH(a, b, c, d, x, s, ac) { \
 (a) += MD5_H ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
}
#define II(a, b, c, d, x, s, ac) { \
 (a) += MD5_I ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
}

static void MD5_Encode(uint8_t *output, const uint32_t *input, uint32_t len) {
    uint32_t i, j;
    for (i = 0, j = 0; j < len; i++, j += 4) {
        output[j] = (uint8_t)(input[i] & 0xFF);
        output[j+1] = (uint8_t)((input[i] >> 8) & 0xFF);
        output[j+2] = (uint8_t)((input[i] >> 16) & 0xFF);
        output[j+3] = (uint8_t)((input[i] >> 24) & 0xFF);
    }
}

static void MD5_Decode(uint32_t *output, const uint8_t *input, uint32_t len) {
    uint32_t i, j;
    for (i = 0, j = 0; j < len; i++, j += 4)
        output[i] = ((uint32_t)input[j]) | (((uint32_t)input[j+1]) << 8) |
                    (((uint32_t)input[j+2]) << 16) | (((uint32_t)input[j+3]) << 24);
}

static void MD5_Transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
    MD5_Decode(x, block, 64);
    /* Round 1 */
    FF(a, b, c, d, x[ 0], S11, 0xd76aa478);
    FF(d, a, b, c, x[ 1], S12, 0xe8c7b756);
    FF(c, d, a, b, x[ 2], S13, 0x242070db);
    FF(b, c, d, a, x[ 3], S14, 0xc1bdceee);
    FF(a, b, c, d, x[ 4], S11, 0xf57c0faf);
    FF(d, a, b, c, x[ 5], S12, 0x4787c62a);
    FF(c, d, a, b, x[ 6], S13, 0xa8304613);
    FF(b, c, d, a, x[ 7], S14, 0xfd469501);
    FF(a, b, c, d, x[ 8], S11, 0x698098d8);
    FF(d, a, b, c, x[ 9], S12, 0x8b44f7af);
    FF(c, d, a, b, x[10], S13, 0xffff5bb1);
    FF(b, c, d, a, x[11], S14, 0x895cd7be);
    FF(a, b, c, d, x[12], S11, 0x6b901122);
    FF(d, a, b, c, x[13], S12, 0xfd987193);
    FF(c, d, a, b, x[14], S13, 0xa679438e);
    FF(b, c, d, a, x[15], S14, 0x49b40821);
    /* Round 2 */
    GG(a, b, c, d, x[ 1], S21, 0xf61e2562);
    GG(d, a, b, c, x[ 6], S22, 0xc040b340);
    GG(c, d, a, b, x[11], S23, 0x265e5a51);
    GG(b, c, d, a, x[ 0], S24, 0xe9b6c7aa);
    GG(a, b, c, d, x[ 5], S21, 0xd62f105d);
    GG(d, a, b, c, x[10], S22,  0x2441453);
    GG(c, d, a, b, x[15], S23, 0xd8a1e681);
    GG(b, c, d, a, x[ 4], S24, 0xe7d3fbc8);
    GG(a, b, c, d, x[ 9], S21, 0x21e1cde6);
    GG(d, a, b, c, x[14], S22, 0xc33707d6);
    GG(c, d, a, b, x[ 3], S23, 0xf4d50d87);
    GG(b, c, d, a, x[ 8], S24, 0x455a14ed);
    GG(a, b, c, d, x[13], S21, 0xa9e3e905);
    GG(d, a, b, c, x[ 2], S22, 0xfcefa3f8);
    GG(c, d, a, b, x[ 7], S23, 0x676f02d9);
    GG(b, c, d, a, x[12], S24, 0x8d2a4c8a);
    /* Round 3 */
    HH(a, b, c, d, x[ 5], S31, 0xfffa3942);
    HH(d, a, b, c, x[ 8], S32, 0x8771f681);
    HH(c, d, a, b, x[11], S33, 0x6d9d6122);
    HH(b, c, d, a, x[14], S34, 0xfde5380c);
    HH(a, b, c, d, x[ 1], S31, 0xa4beea44);
    HH(d, a, b, c, x[ 4], S32, 0x4bdecfa9);
    HH(c, d, a, b, x[ 7], S33, 0xf6bb4b60);
    HH(b, c, d, a, x[10], S34, 0xbebfbc70);
    HH(a, b, c, d, x[13], S31, 0x289b7ec6);
    HH(d, a, b, c, x[ 0], S32, 0xeaa127fa);
    HH(c, d, a, b, x[ 3], S33, 0xd4ef3085);
    HH(b, c, d, a, x[ 6], S34,  0x4881d05);
    HH(a, b, c, d, x[ 9], S31, 0xd9d4d039);
    HH(d, a, b, c, x[12], S32, 0xe6db99e5);
    HH(c, d, a, b, x[15], S33, 0x1fa27cf8);
    HH(b, c, d, a, x[ 2], S34, 0xc4ac5665);
    /* Round 4 */
    II(a, b, c, d, x[ 0], S41, 0xf4292244);
    II(d, a, b, c, x[ 7], S42, 0x432aff97);
    II(c, d, a, b, x[14], S43, 0xab9423a7);
    II(b, c, d, a, x[ 5], S44, 0xfc93a039);
    II(a, b, c, d, x[12], S41, 0x655b59c3);
    II(d, a, b, c, x[ 3], S42, 0x8f0ccc92);
    II(c, d, a, b, x[10], S43, 0xffeff47d);
    II(b, c, d, a, x[ 1], S44, 0x85845dd1);
    II(a, b, c, d, x[ 8], S41, 0x6fa87e4f);
    II(d, a, b, c, x[15], S42, 0xfe2ce6e0);
    II(c, d, a, b, x[ 6], S43, 0xa3014314);
    II(b, c, d, a, x[13], S44, 0x4e0811a1);
    II(a, b, c, d, x[ 4], S41, 0xf7537e82);
    II(d, a, b, c, x[11], S42, 0xbd3af235);
    II(c, d, a, b, x[ 2], S43, 0x2ad7d2bb);
    II(b, c, d, a, x[ 9], S44, 0xeb86d391);
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void MD5_Init(MD5_CTX *context) {
    context->count[0] = context->count[1] = 0;
    context->state[0] = 0x67452301;
    context->state[1] = 0xefcdab89;
    context->state[2] = 0x98badcfe;
    context->state[3] = 0x10325476;
}

static void MD5_Update(MD5_CTX *context, const uint8_t *input, uint32_t inputLen) {
    uint32_t i, idx, partLen;
    idx = (context->count[0] >> 3) & 0x3F;
    if ((context->count[0] += (inputLen << 3)) < (inputLen << 3))
        context->count[1]++;
    context->count[1] += (inputLen >> 29);
    partLen = 64 - idx;
    if (inputLen >= partLen) {
        memcpy(&context->buffer[idx], input, partLen);
        MD5_Transform(context->state, context->buffer);
        for (i = partLen; i + 63 < inputLen; i += 64)
            MD5_Transform(context->state, &input[i]);
        idx = 0;
    } else {
        i = 0;
    }
    memcpy(&context->buffer[idx], &input[i], inputLen - i);
}

static void MD5_Final(uint8_t digest[16], MD5_CTX *context) {
    uint8_t bits[8];
    uint32_t idx, padLen;
    MD5_Encode(bits, context->count, 8);
    idx = (context->count[0] >> 3) & 0x3F;
    padLen = (idx < 56) ? (56 - idx) : (120 - idx);
    MD5_Update(context, MD5_PADDING, padLen);
    MD5_Update(context, bits, 8);
    MD5_Encode(digest, context->state, 16);
    memset(context, 0, sizeof(*context));
}

// SHA-256 implementation
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} SHA256_CTX;

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32-(n))))
#define Ch(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define Sigma0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define Sigma1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define sigma0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define sigma1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static uint32_t SHA_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void SHA256_Transform(uint32_t state[8], const uint8_t buffer[64]) {
    uint32_t w[64], a, b, c, d, e, f, g, h, i, t1, t2;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)buffer[i*4] << 24) |
               ((uint32_t)buffer[i*4+1] << 16) |
               ((uint32_t)buffer[i*4+2] << 8) |
               (uint32_t)buffer[i*4+3];
    }
    for (i = 16; i < 64; i++) {
        w[i] = sigma1(w[i-2]) + w[i-7] + sigma0(w[i-15]) + w[i-16];
    }
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];
    for (i = 0; i < 64; i++) {
        t1 = h + Sigma1(e) + Ch(e, f, g) + SHA_K[i] + w[i];
        t2 = Sigma0(a) + Maj(a, b, c);
        h = g; g = f; f = e;
        e = d + t1;
        d = c; c = b; b = a;
        a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void SHA256_Init(SHA256_CTX *context) {
    context->state[0] = 0x6a09e667; context->state[1] = 0xbb67ae85;
    context->state[2] = 0x3c6ef372; context->state[3] = 0xa54ff53a;
    context->state[4] = 0x510e527f; context->state[5] = 0x9b05688c;
    context->state[6] = 0x1f83d9ab; context->state[7] = 0x5be0cd19;
    context->count = 0;
}

static void SHA256_Update(SHA256_CTX *context, const uint8_t *input, uint32_t len) {
    uint32_t i, idx, partLen;
    idx = (uint32_t)(context->count & 63);
    context->count += len;
    partLen = 64 - idx;
    if (len >= partLen) {
        memcpy(&context->buffer[idx], input, partLen);
        SHA256_Transform(context->state, context->buffer);
        for (i = partLen; i + 63 < len; i += 64)
            SHA256_Transform(context->state, &input[i]);
        idx = 0;
    } else {
        i = 0;
    }
    memcpy(&context->buffer[idx], &input[i], len - i);
}

static void SHA256_Final(uint8_t digest[32], SHA256_CTX *context) {
    uint8_t bits[8];
    uint32_t idx, padLen;
    uint64_t count_bits = context->count << 3;
    for (int i = 0; i < 8; i++) {
        bits[i] = (uint8_t)((count_bits >> (56 - i * 8)) & 255);
    }
    idx = (uint32_t)(context->count & 63);
    padLen = (idx < 56) ? (56 - idx) : (120 - idx);
    SHA256_Update(context, MD5_PADDING, padLen);
    SHA256_Update(context, bits, 8);
    for (int i = 0; i < 8; i++) {
        digest[i*4]   = (uint8_t)((context->state[i] >> 24) & 255);
        digest[i*4+1] = (uint8_t)((context->state[i] >> 16) & 255);
        digest[i*4+2] = (uint8_t)((context->state[i] >> 8) & 255);
        digest[i*4+3] = (uint8_t)(context->state[i] & 255);
    }
    memset(context, 0, sizeof(*context));
}

/* -----------------------------------------------------------------------
 * Cryptographic Digest & Checksum Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_checksum(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING) {
        return SLP_NUM_VAL(0);
    }
    SlpObjString *str = SLP_AS_STRING(args[0]);
    uint32_t crc = calculate_crc32((const uint8_t*)str->chars, str->length);
    return SLP_NUM_VAL((double)crc);
}

static SlpValue builtin_digest(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING) {
        return SLP_NULL_VAL;
    }
    SlpObjString *str = SLP_AS_STRING(args[0]);
    const char *algo = "MD5";
    if (argc >= 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING) {
        algo = SLP_AS_STRING(args[1])->chars;
    }
    
    if (strcmp(algo, "SHA-256") == 0 || strcmp(algo, "SHA256") == 0) {
        SHA256_CTX ctx;
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, (const uint8_t*)str->chars, str->length);
        uint8_t hash[32];
        SHA256_Final(hash, &ctx);
        
        char hex[65];
        for (int i = 0; i < 32; i++) {
            snprintf(hex + i*2, 3, "%02x", hash[i]);
        }
        hex[64] = '\0';
        SlpObjString *res = slp_vm_copy_string(vm, hex, 64);
        return SLP_OBJ_VAL(res);
    } else {
        MD5_CTX ctx;
        MD5_Init(&ctx);
        MD5_Update(&ctx, (const uint8_t*)str->chars, str->length);
        uint8_t hash[16];
        MD5_Final(hash, &ctx);
        
        char hex[33];
        for (int i = 0; i < 16; i++) {
            snprintf(hex + i*2, 3, "%02x", hash[i]);
        }
        hex[32] = '\0';
        SlpObjString *res = slp_vm_copy_string(vm, hex, 32);
        return SLP_OBJ_VAL(res);
    }
}

/* -----------------------------------------------------------------------
 * Functional Programming Builtins (map, filter, reduce)
 * ----------------------------------------------------------------------- */

static SlpValue builtin_map(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_CLOSURE) {
        return SLP_NULL_VAL;
    }
    SlpObjClosure *closure = SLP_AS_CLOSURE(args[0]);
    SlpObjArray *arr = slp_vm_new_array(vm);
    SlpObjArray *res = slp_vm_new_array(vm);
    if (!arr || !res) return SLP_NULL_VAL;
    slp_vm_push(vm, SLP_OBJ_VAL(arr));
    slp_vm_push(vm, SLP_OBJ_VAL(res));
    if (!collect_iterator_values(vm, args[1], arr)) {
        slp_vm_pop(vm);
        slp_vm_pop(vm);
        return SLP_NULL_VAL;
    }

    for (int i = 0; i < arr->count; i++) {
        SlpValue elem = arr->elements[i];
        slp_vm_push(vm, SLP_OBJ_VAL(closure));
        slp_vm_push(vm, elem);
        if (slp_vm_call(vm, 1, false) == SLP_OK) {
            SlpValue val = slp_vm_pop(vm);
            slp_obj_array_push(vm->allocator, res, val);
        } else {
            slp_vm_pop(vm);
            slp_vm_pop(vm);
            return SLP_NULL_VAL;
        }
    }
    slp_vm_pop(vm);
    slp_vm_pop(vm);
    return SLP_OBJ_VAL(res);
}

static SlpValue builtin_filter(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_CLOSURE) {
        return SLP_NULL_VAL;
    }
    SlpObjClosure *closure = SLP_AS_CLOSURE(args[0]);
    SlpObjArray *arr = slp_vm_new_array(vm);
    SlpObjArray *res = slp_vm_new_array(vm);
    if (!arr || !res) return SLP_NULL_VAL;
    slp_vm_push(vm, SLP_OBJ_VAL(arr));
    slp_vm_push(vm, SLP_OBJ_VAL(res));
    if (!collect_iterator_values(vm, args[1], arr)) {
        slp_vm_pop(vm);
        slp_vm_pop(vm);
        return SLP_NULL_VAL;
    }

    for (int i = 0; i < arr->count; i++) {
        SlpValue elem = arr->elements[i];
        slp_vm_push(vm, SLP_OBJ_VAL(closure));
        slp_vm_push(vm, elem);
        if (slp_vm_call(vm, 1, false) == SLP_OK) {
            SlpValue val = slp_vm_pop(vm);
            if (!SLP_IS_NULL(val))
                slp_obj_array_push(vm->allocator, res, val);
        } else {
            slp_vm_pop(vm);
            slp_vm_pop(vm);
            return SLP_NULL_VAL;
        }
    }
    slp_vm_pop(vm);
    slp_vm_pop(vm);
    return SLP_OBJ_VAL(res);
}

static SlpValue builtin_reduce(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_CLOSURE) {
        return SLP_NULL_VAL;
    }
    SlpObjClosure *closure = SLP_AS_CLOSURE(args[0]);
    SlpObjArray *arr = slp_vm_new_array(vm);
    if (!arr) return SLP_NULL_VAL;
    slp_vm_push(vm, SLP_OBJ_VAL(arr));
    if (!collect_iterator_values(vm, args[1], arr) || arr->count == 0) {
        slp_vm_pop(vm);
        return SLP_NULL_VAL;
    }

    SlpValue accum = arr->elements[0];
    for (int i = 1; i < arr->count; i++) {
        SlpValue next_val = arr->elements[i];
        slp_vm_push(vm, SLP_OBJ_VAL(closure));
        slp_vm_push(vm, next_val); // b
        slp_vm_push(vm, accum);    // a
        if (slp_vm_call(vm, 2, false) == SLP_OK) {
            accum = slp_vm_pop(vm);
        } else {
            slp_vm_pop(vm);
            return SLP_NULL_VAL;
        }
    }
    slp_vm_pop(vm);
    return accum;
}

/* -----------------------------------------------------------------------
 * Splicing & Subarray Utilities
 * ----------------------------------------------------------------------- */

static SlpValue builtin_splice(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY ||
        !SLP_IS_OBJ(args[1]) ||
        SLP_OBJ_TYPE(args[1]) != SLP_OBJ_ARRAY) {
        return SLP_NULL_VAL;
    }
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    SlpObjArray *replacement = SLP_AS_ARRAY(args[1]);
    int idx = argc >= 3 && stdlib_is_number(args[2])
                  ? (int)slp_value_as_number(args[2])
                  : 0;
    int len = argc >= 4 && stdlib_is_number(args[3])
                  ? (int)slp_value_as_number(args[3])
                  : replacement->count;
    if (idx < 0) idx = arr->count + idx;
    if (idx < 0) idx = 0;
    if (idx > arr->count) idx = arr->count;
    if (len < 0) len = 0;
    if (idx + len > arr->count) len = arr->count - idx;
    
    int replacement_count = replacement->count;
    SlpValue *replacement_values = NULL;
    if (replacement_count > 0) {
        replacement_values = (SlpValue*)stdlib_alloc(
            vm, sizeof(SlpValue) * (size_t)replacement_count);
        if (!replacement_values) return SLP_NULL_VAL;
        memcpy(replacement_values, replacement->elements,
               sizeof(SlpValue) * (size_t)replacement_count);
    }

    for (int i = 0; i < len; i++)
        slp_obj_array_remove_at(arr, idx);
    for (int i = 0; i < replacement_count; i++)
        slp_obj_array_insert(vm->allocator, arr, idx + i,
                             replacement_values[i]);
    if (replacement_values) stdlib_free(vm, replacement_values);
    return args[0];
}

static SlpValue builtin_subarray(SlpVM *vm, SlpValue *args, int argc) {
    return builtin_sublist(vm, args, argc);
}

/* -----------------------------------------------------------------------
 * Bulk Collection Operations
 * ----------------------------------------------------------------------- */

static SlpValue builtin_addAll(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_ARRAY) {
        return SLP_BOOL_VAL(false);
    }
    SlpObjArray *a = SLP_AS_ARRAY(args[0]);
    SlpObjArray *b = SLP_AS_ARRAY(args[1]);
    for (int i = 0; i < b->count; i++) {
        bool exists = false;
        for (int j = 0; j < a->count; j++) {
            if (slp_value_identity_equals(a->elements[j], b->elements[i])) {
                exists = true;
                break;
            }
        }
        if (!exists)
            slp_obj_array_push(vm->allocator, a, b->elements[i]);
    }
    return args[0];
}

static SlpValue builtin_removeAll(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_ARRAY) {
        return SLP_BOOL_VAL(false);
    }
    SlpObjArray *a = SLP_AS_ARRAY(args[0]);
    SlpObjArray *b = SLP_AS_ARRAY(args[1]);
    for (int i = 0; i < a->count; ) {
        bool found = false;
        for (int j = 0; j < b->count; j++) {
            if (slp_value_identity_equals(a->elements[i], b->elements[j])) {
                found = true;
                break;
            }
        }
        if (found) {
            slp_obj_array_remove_at(a, i);
        } else {
            i++;
        }
    }
    return args[0];
}

static SlpValue builtin_retainAll(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_ARRAY) {
        return SLP_BOOL_VAL(false);
    }
    SlpObjArray *a = SLP_AS_ARRAY(args[0]);
    SlpObjArray *b = SLP_AS_ARRAY(args[1]);
    for (int i = 0; i < a->count; ) {
        bool found = false;
        for (int j = 0; j < b->count; j++) {
            if (slp_value_identity_equals(a->elements[i], b->elements[j])) {
                found = true;
                break;
            }
        }
        if (!found) {
            slp_obj_array_remove_at(a, i);
        } else {
            i++;
        }
    }
    return args[0];
}

/* -----------------------------------------------------------------------
 * Queue / Stack & Search Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_pushl(SlpVM *vm, SlpValue *args, int argc) {
    slp_vm_push_local_scope(vm, args, argc);
    return SLP_NULL_VAL;
}

static SlpValue builtin_popl(SlpVM *vm, SlpValue *args, int argc) {
    slp_vm_pop_local_scope(vm, args, argc);
    return SLP_NULL_VAL;
}

static SlpValue builtin_search(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_CLOSURE) {
        return SLP_NULL_VAL;
    }
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    SlpObjClosure *closure = SLP_AS_CLOSURE(args[1]);
    int start = argc >= 3 ? (int)slp_value_as_number(args[2]) : 0;
    if (start < 0) start += arr->count;
    if (start < 0) start = 0;
    if (start > arr->count) start = arr->count;
    for (int i = start; i < arr->count; i++) {
        slp_vm_push(vm, SLP_OBJ_VAL(closure));
        slp_vm_push(vm, arr->elements[i]);
        slp_vm_push(vm, SLP_NUM_VAL((double)i));
        if (slp_vm_call(vm, 2, false) == SLP_OK) {
            SlpValue val = slp_vm_pop(vm);
            if (!slp_value_is_falsy(val)) {
                return val;
            }
        } else {
            return SLP_NULL_VAL;
        }
    }
    return SLP_NULL_VAL;
}

/* -----------------------------------------------------------------------
 * Number Parsing & Ordered Hash Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_parseNumber(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING) {
        return SLP_NUM_VAL(0);
    }
    const char *s = SLP_AS_STRING(args[0])->chars;
    while (*s && isspace((unsigned char)*s)) s++;
    
    long long val = 0;
    if (strncmp(s, "0x", 2) == 0 || strncmp(s, "0X", 2) == 0) {
        val = strtoll(s, NULL, 16);
    } else if (s[0] == '0' && isdigit((unsigned char)s[1])) {
        val = strtoll(s, NULL, 8);
    } else {
        if (strchr(s, '.') || strchr(s, 'e') || strchr(s, 'E')) {
            double d = strtod(s, NULL);
            return SLP_NUM_VAL(d);
        }
        val = strtoll(s, NULL, 10);
    }
    return SLP_NUM_VAL((double)val);
}

/* -----------------------------------------------------------------------
 * Bidirectional Standard and Network I/O Bridge Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_allocate(SlpVM *vm, SlpValue *args, int argc) {
    (void)args;
    (void)argc;
    FILE *file = tmpfile();
    if (!file) return SLP_NULL_VAL;

    SlpObjIOHandle *handle = slp_obj_io_handle_new(vm->allocator);
    if (!handle) {
        fclose(file);
        return SLP_NULL_VAL;
    }
    handle->file = file;
    handle->is_memory = true;
    handle->obj.next = vm->objects;
    vm->objects = &handle->obj;
    return SLP_OBJ_VAL(handle);
}

static bool memory_object_push(
    SlpVM *vm, SlpObjIOHandle *handle, SlpValue value) {
    if (handle->object_count == handle->object_capacity) {
        int capacity =
            handle->object_capacity < 8 ? 8 : handle->object_capacity * 2;
        SlpValue *values = (SlpValue*)vm->allocator->reallocate(
            handle->object_values,
            sizeof(SlpValue) * (size_t)capacity,
            vm->allocator->user_data);
        if (!values) return false;
        handle->object_values = values;
        handle->object_capacity = capacity;
    }
    handle->object_values[handle->object_count++] = value;
    return true;
}

static SlpValue builtin_writeObject(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE)
        return SLP_NULL_VAL;
    SlpObjIOHandle *handle = SLP_AS_IO_HANDLE(args[0]);
    if (!handle->is_memory || handle->memory_readable)
        return SLP_NULL_VAL;

    for (int i = 1; i < argc; i++) {
        if (!memory_object_push(
                vm, handle, clone_serializable(vm, args[i])))
            return SLP_NULL_VAL;
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_readObject(
    SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE)
        return SLP_NULL_VAL;
    SlpObjIOHandle *handle = SLP_AS_IO_HANDLE(args[0]);
    if (!handle->is_memory || !handle->memory_readable ||
        handle->object_read_index >= handle->object_count) {
        handle->is_eof = true;
        return SLP_NULL_VAL;
    }
    return handle->object_values[handle->object_read_index++];
}

static SlpValue builtin_readAsObject(
    SlpVM *vm, SlpValue *args, int argc) {
    SlpValue value =
        builtin_readObject(vm, args, argc);
    if (SLP_IS_NULL(value))
        return SLP_NULL_VAL;
    SlpObjJavaObject *scalar =
        slp_vm_new_java_object(
            vm, "sleep.runtime.Scalar",
            SLP_JAVA_GENERIC);
    if (!scalar) return SLP_NULL_VAL;
    scalar->value = value;
    return SLP_OBJ_VAL(scalar);
}

static SlpValue builtin_openf(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    
    const char *mode = "rb";
    if (path[0] == '>') {
        mode = "wb";
        path++;
    } else if (path[0] == '<') {
        mode = "rb";
        path++;
    }
    
    FILE *f = fopen(path, mode);
    if (!f) {
        int open_error = errno;
        char resolved[2048];
        if (stdlib_path_is_absolute(path)) {
            snprintf(
                resolved, sizeof(resolved),
                "%s", path);
        } else {
            char working_directory[1024];
            if (slp_platform_getcwd(
                    working_directory,
                    sizeof(working_directory))) {
                snprintf(
                    resolved, sizeof(resolved),
                    "%s%c%s",
                    working_directory,
                    (char)slp_platform_path_separator(),
                    path);
            } else {
                snprintf(
                    resolved, sizeof(resolved),
                    "%s", path);
            }
        }
        char message[2304];
        snprintf(
            message, sizeof(message),
            "%s (%s)", resolved,
            strerror(open_error));
        slp_vm_flag_error(
            vm,
            slp_vm_new_error(
                vm,
                "java.io.FileNotFoundException",
                message, message, NULL));
        return SLP_NULL_VAL;
    }
    
    SlpObjIOHandle *handle = slp_obj_io_handle_new(vm->allocator);
    if (!handle) {
        fclose(f);
        return SLP_NULL_VAL;
    }
    handle->file = f;
    handle->obj.next = vm->objects;
    vm->objects = &handle->obj;
    
    return SLP_OBJ_VAL(handle);
}

static bool fork_copy_environment(
    SlpVM *vm, SlpObjHash *source,
    SlpObjHash *destination) {
    for (int index = 0;
         index < source->capacity; index++) {
        if (SLP_IS_NULL(
                source->entries[index].key))
            continue;
        SlpValue key =
            source->entries[index].key;
        SlpValue value =
            source->entries[index].value;
        if (!SLP_IS_OBJ(key) ||
            SLP_OBJ_TYPE(key) !=
                SLP_OBJ_STRING)
            continue;
        SlpObjString *name =
            SLP_AS_STRING(key);
        if (name->length > 0 &&
            (name->chars[0] == '$' ||
             name->chars[0] == '@' ||
             name->chars[0] == '%'))
            continue;
        if (SLP_IS_OBJ(value) &&
            SLP_OBJ_TYPE(value) ==
                SLP_OBJ_CLOSURE) {
            SlpObjClosure *copy =
                slp_vm_clone_closure(
                    vm,
                    SLP_AS_CLOSURE(value));
            if (!copy) return false;
            value = SLP_OBJ_VAL(copy);
        }
        if (!slp_vm_hash_set(
                vm, destination,
                key, value))
            return false;
    }
    return true;
}

static SlpValue builtin_fork(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    SlpValue closure_value =
        stdlib_dereference(args[0]);
    if (!SLP_IS_OBJ(closure_value) ||
        SLP_OBJ_TYPE(closure_value) !=
            SLP_OBJ_CLOSURE)
        return SLP_NULL_VAL;

    SlpObjHash *parent_globals =
        vm->globals;
    SlpObjHash *child_globals =
        slp_vm_new_hash(vm);
    SlpObjIOHandle *handle =
        slp_obj_io_handle_new(
            vm->allocator);
    if (!child_globals || !handle)
        return SLP_NULL_VAL;
    handle->file = tmpfile();
    if (!handle->file)
        return SLP_NULL_VAL;
    handle->is_memory = true;
    handle->obj.next = vm->objects;
    vm->objects = &handle->obj;

    SlpValue parent_root =
        SLP_OBJ_VAL(parent_globals);
    SlpValue child_root =
        SLP_OBJ_VAL(child_globals);
    SlpValue handle_root =
        SLP_OBJ_VAL(handle);
    slp_vm_push(vm, parent_root);
    slp_vm_push(vm, child_root);
    slp_vm_push(vm, handle_root);

    if (!fork_copy_environment(
            vm, parent_globals,
            child_globals)) {
        slp_vm_pop(vm);
        slp_vm_pop(vm);
        slp_vm_pop(vm);
        return SLP_NULL_VAL;
    }
    SlpObjString *source_name =
        slp_vm_copy_cstr(vm, "$source");
    if (source_name)
        slp_vm_hash_set(
            vm, child_globals,
            SLP_OBJ_VAL(source_name),
            handle_root);
    for (int index = 1;
         index < argc; index++) {
        SlpValue argument =
            stdlib_dereference(args[index]);
        if (!SLP_IS_OBJ(argument) ||
            SLP_OBJ_TYPE(argument) !=
                SLP_OBJ_KEY_VALUE)
            continue;
        SlpObjKeyValue *pair =
            SLP_AS_KEY_VALUE(argument);
        slp_vm_hash_set(
            vm, child_globals,
            pair->key, pair->value);
    }

    SlpObjClosure *child =
        slp_vm_clone_closure(
            vm,
            SLP_AS_CLOSURE(
                closure_value));
    if (!child) {
        slp_vm_pop(vm);
        slp_vm_pop(vm);
        slp_vm_pop(vm);
        return SLP_NULL_VAL;
    }
    vm->globals = child_globals;
    slp_vm_push(
        vm, SLP_OBJ_VAL(child));
    SlpResult call_result =
        slp_vm_call(vm, 0, false);
    SlpValue token =
        call_result == SLP_OK
            ? slp_vm_pop(vm)
            : SLP_NULL_VAL;
    vm->globals = parent_globals;
    handle->token = token;
    handle->token_ready = true;
    if (handle->file &&
        !handle->memory_readable) {
        fflush(handle->file);
        rewind(handle->file);
        handle->memory_readable = true;
        handle->memory_text_buffered = false;
        handle->memory_mark = 0;
        handle->is_eof = false;
    }

    slp_vm_pop(vm);
    slp_vm_pop(vm);
    slp_vm_pop(vm);
    return handle_root;
}

static SlpValue builtin_wait(
    SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 ||
        !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) !=
            SLP_OBJ_IO_HANDLE)
        return SLP_NULL_VAL;
    SlpObjIOHandle *handle =
        SLP_AS_IO_HANDLE(args[0]);
    return handle->token_ready
        ? handle->token
        : SLP_NULL_VAL;
}

static bool stdlib_path_is_absolute(const char *path) {
    if (!path || !path[0]) return false;
    return path[0] == '/' || path[0] == '\\' ||
           (isalpha((unsigned char)path[0]) &&
            path[1] == ':');
}

static char *stdlib_resolve_include_path(
    SlpVM *vm, const char *requested) {
    if (!requested) return NULL;
    const char *base = vm->source_path;
    const char *last_separator = NULL;
    if (!stdlib_path_is_absolute(requested) &&
        base && base[0] != '<') {
        for (const char *cursor = base;
             *cursor; cursor++) {
            if (*cursor == '/' || *cursor == '\\')
                last_separator = cursor;
        }
    }

    size_t prefix_length =
        last_separator
            ? (size_t)(last_separator - base + 1)
            : 0;
    size_t requested_length =
        strlen(requested);
    if (prefix_length >
        SIZE_MAX - requested_length - 1)
        return NULL;
    char *path = (char*)stdlib_alloc(
        vm, prefix_length + requested_length + 1);
    if (!path) return NULL;
    if (prefix_length > 0)
        memcpy(path, base, prefix_length);
    memcpy(
        path + prefix_length, requested,
        requested_length + 1);
    return path;
}

static SlpValue builtin_include(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || argc > 1)
        return SLP_NULL_VAL;
    SlpObjString *requested =
        slp_vm_stringify(vm, args[0]);
    if (!requested) return SLP_NULL_VAL;
    char *path = stdlib_resolve_include_path(
        vm, requested->chars);
    if (!path) return SLP_NULL_VAL;

    FILE *file = fopen(path, "rb");
    if (!file) {
        int include_error = errno;
        size_t message_size =
            strlen(path) +
            strlen(strerror(include_error)) +
            8;
        char *message = (char*)stdlib_alloc(
            vm, message_size);
        if (message) {
            snprintf(
                message, message_size,
                "%s (%s)", path,
                strerror(include_error));
            slp_vm_flag_error(
                vm,
                slp_vm_new_error(
                    vm,
                    "java.io.FileNotFoundException",
                    message, message, NULL));
            stdlib_free(vm, message);
        }
        stdlib_free(vm, path);
        return SLP_NULL_VAL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        stdlib_free(vm, path);
        return SLP_NULL_VAL;
    }
    long file_length = ftell(file);
    if (file_length < 0 ||
        (unsigned long)file_length >
            (unsigned long)(SIZE_MAX - 1) ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        stdlib_free(vm, path);
        return SLP_NULL_VAL;
    }

    char *source = (char*)stdlib_alloc(
        vm, (size_t)file_length + 1);
    if (!source) {
        fclose(file);
        stdlib_free(vm, path);
        return SLP_NULL_VAL;
    }
    size_t read_count = fread(
        source, 1, (size_t)file_length, file);
    fclose(file);
    source[read_count] = '\0';

    SlpObjString *include_path =
        slp_vm_copy_cstr(vm, path);
    if (include_path) {
        slp_vm_set_scoped_value(
            vm, "$__INCLUDE__",
            SLP_OBJ_VAL(include_path));
    }
    SlpResult eval_result =
        slp_vm_eval_inline(
            vm, source, path, NULL);
    stdlib_free(vm, source);
    stdlib_free(vm, path);
    if (eval_result != SLP_OK)
        return SLP_NULL_VAL;
    return SLP_NULL_VAL;
}

static SlpValue builtin_eval(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    SlpObjString *source =
        slp_vm_stringify(vm, args[0]);
    if (!source) return SLP_NULL_VAL;
    SlpValue result = SLP_NULL_VAL;
    if (slp_vm_eval_inline(
            vm, source->chars, "eval",
            &result) != SLP_OK)
        return SLP_NULL_VAL;
    return result;
}

static SlpValue builtin_check_error(
    SlpVM *vm, SlpValue *args, int argc) {
    SlpValue error = slp_vm_check_error(vm);
    if (argc > 0 &&
        SLP_IS_OBJ(args[0]) &&
        SLP_OBJ_TYPE(args[0]) ==
            SLP_OBJ_SCALAR_CELL) {
        slp_vm_assign_reference(
            vm, args[0], error);
    }
    return error;
}

static SlpValue builtin_exit(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc > 0 &&
        !slp_value_is_falsy(args[0])) {
        SlpObjString *message =
            slp_vm_stringify(vm, args[0]);
        if (message && message->length > 0)
            slp_vm_warning(
                vm, message->chars);
    }
    /*
     * A null FLOW_CONTROL_THROW in Sleep terminates the whole interpreter,
     * crossing closure and try/catch boundaries. Reuse the VM's established
     * flow-exit unwind path used by the `done` statement.
     */
    vm->abort_requested = true;
    vm->flow_exit_requested = true;
    return SLP_NULL_VAL;
}

static SlpValue builtin_connect(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2) return SLP_NULL_VAL;
    const char *host = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "127.0.0.1";
    int port = (int)slp_value_as_number(args[1]);
    
    int sock = -1;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
    sock = (int)socket(AF_INET, SOCK_STREAM, 0);
#else
    sock = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (sock < 0) return SLP_NULL_VAL;
    
    struct hostent *server = gethostbyname(host);
    if (!server) {
        slp_platform_close_socket(sock);
        return SLP_NULL_VAL;
    }
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    char *addr_ptr;
    memcpy(&addr_ptr, server->h_addr_list, sizeof(char *));
    memcpy(&serv_addr.sin_addr.s_addr, addr_ptr, (size_t)server->h_length);
    serv_addr.sin_port = htons((uint16_t)port);
    
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        slp_platform_close_socket(sock);
        return SLP_NULL_VAL;
    }
    
    SlpObjIOHandle *handle = slp_obj_io_handle_new(vm->allocator);
    if (!handle) {
        slp_platform_close_socket(sock);
        return SLP_NULL_VAL;
    }
    handle->socket_fd = sock;
    handle->is_socket = true;
    
    handle->obj.next = vm->objects;
    vm->objects = &handle->obj;
    
    return SLP_OBJ_VAL(handle);
}

static SlpValue builtin_listen(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    int port = (int)slp_value_as_number(args[0]);
    
    int sock = -1;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
    sock = (int)socket(AF_INET, SOCK_STREAM, 0);
#else
    sock = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (sock < 0) return SLP_NULL_VAL;
    
    int opt = 1;
#ifdef _WIN32
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons((uint16_t)port);
    
    if (bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        slp_platform_close_socket(sock);
        return SLP_NULL_VAL;
    }
    
    if (listen(sock, 3) < 0) {
        slp_platform_close_socket(sock);
        return SLP_NULL_VAL;
    }
    
    SlpObjIOHandle *handle = slp_obj_io_handle_new(vm->allocator);
    if (!handle) {
        slp_platform_close_socket(sock);
        return SLP_NULL_VAL;
    }
    handle->socket_fd = sock;
    handle->is_socket = true;
    
    handle->obj.next = vm->objects;
    vm->objects = &handle->obj;
    
    return SLP_OBJ_VAL(handle);
}

static SlpValue builtin_exec(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    const char *cmd = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";

    SlpPlatformProcess *proc = slp_platform_exec(cmd, vm->allocator);
    if (!proc) return SLP_NULL_VAL;

    SlpObjIOHandle *handle = slp_obj_io_handle_new(vm->allocator);
    if (!handle) {
        slp_platform_close_process(proc);
        slp_platform_free_process(proc, vm->allocator);
        return SLP_NULL_VAL;
    }

    handle->file = proc->read_stream;
    handle->socket_fd = proc->write_fd;
    handle->pid = proc->pid;
    handle->is_pipeline = true;

    slp_platform_free_process(proc, vm->allocator);

    handle->obj.next = vm->objects;
    vm->objects = &handle->obj;

    return SLP_OBJ_VAL(handle);
}

static SlpValue builtin_readln(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE) {
        return SLP_NULL_VAL;
    }
    SlpObjIOHandle *h = SLP_AS_IO_HANDLE(args[0]);
    if (h->is_eof) return SLP_NULL_VAL;
    
    char buf[2048];
    if (h->file) {
        if (fgets(buf, sizeof(buf), h->file)) {
            if (h->is_memory)
                h->memory_text_buffered = true;
            size_t len = strlen(buf);
            if (len > 0 && buf[len-1] == '\n') {
                buf[len-1] = '\0';
                len--;
            }
            if (len > 0 && buf[len-1] == '\r') {
                buf[len-1] = '\0';
                len--;
            }
            SlpObjString *s = slp_vm_copy_string(vm, buf, (uint32_t)len);
            return SLP_OBJ_VAL(s);
        } else {
            h->is_eof = true;
            return SLP_NULL_VAL;
        }
    } else if (h->is_socket && h->socket_fd != -1) {
        size_t idx = 0;
        char c;
        while (idx < sizeof(buf) - 1) {
            ssize_t n = recv(h->socket_fd, &c, 1, 0);
            if (n <= 0) {
                h->is_eof = true;
                break;
            }
            if (c == '\n') break;
            if (c != '\r') buf[idx++] = c;
        }
        if (idx == 0 && h->is_eof) return SLP_NULL_VAL;
        buf[idx] = '\0';
        SlpObjString *s = slp_vm_copy_string(vm, buf, (uint32_t)idx);
        return SLP_OBJ_VAL(s);
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_readAll(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE) {
        return SLP_NULL_VAL;
    }
    SlpObjArray *lines = slp_vm_new_array(vm);
    if (!lines) return SLP_NULL_VAL;
    while (true) {
        SlpValue line = builtin_readln(vm, args, 1);
        if (SLP_IS_NULL(line)) break;
        slp_obj_array_push(vm->allocator, lines, line);
    }
    return SLP_OBJ_VAL(lines);
}

static SlpValue builtin_readc(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE) {
        return SLP_NULL_VAL;
    }
    SlpObjIOHandle *h = SLP_AS_IO_HANDLE(args[0]);
    if (h->is_eof) return SLP_NULL_VAL;
    
    unsigned char bytes[4] = {0, 0, 0, 0};
    int byte_count = 1;
    if (h->file) {
        int val = fgetc(h->file);
        if (val == EOF) {
            h->is_eof = true;
            return SLP_NULL_VAL;
        }
        bytes[0] = (unsigned char)val;
    } else if (h->is_socket && h->socket_fd != -1) {
        ssize_t n = recv(
            h->socket_fd, (char*)bytes, 1, 0);
        if (n <= 0) {
            h->is_eof = true;
            return SLP_NULL_VAL;
        }
    } else {
        return SLP_NULL_VAL;
    }

    if (h->text_encoding == 1 &&
        bytes[0] >= 0x80) {
        unsigned char latin1 = bytes[0];
        bytes[0] =
            (unsigned char)(0xc0u |
                (latin1 >> 6));
        bytes[1] =
            (unsigned char)(0x80u |
                (latin1 & 0x3fu));
        byte_count = 2;
    } else if (h->text_encoding == 0 &&
               bytes[0] >= 0xc2) {
        if (bytes[0] < 0xe0)
            byte_count = 2;
        else if (bytes[0] < 0xf0)
            byte_count = 3;
        else if (bytes[0] < 0xf5)
            byte_count = 4;
        for (int index = 1;
             index < byte_count; index++) {
            int val = EOF;
            if (h->file)
                val = fgetc(h->file);
            else if (
                h->is_socket &&
                h->socket_fd != -1) {
                int received = (int)recv(
                    h->socket_fd,
                    (char*)&bytes[index],
                    1, 0);
                if (received == 1)
                    continue;
            }
            if (val == EOF) {
                h->is_eof = true;
                byte_count = index;
                break;
            }
            bytes[index] =
                (unsigned char)val;
        }
    }
    SlpObjString *s = slp_vm_copy_string(
        vm, (const char*)bytes,
        (uint32_t)byte_count);
    return SLP_OBJ_VAL(s);
}

static bool encoding_name_equals(
    const char *left, const char *right) {
    if (!left || !right) return false;
    while (*left && *right) {
        unsigned char a =
            (unsigned char)*left++;
        unsigned char b =
            (unsigned char)*right++;
        if (a == '_') a = '-';
        if (b == '_') b = '-';
        if (tolower(a) != tolower(b))
            return false;
    }
    return *left == '\0' &&
           *right == '\0';
}

static SlpValue builtin_setEncoding(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 ||
        !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) !=
            SLP_OBJ_IO_HANDLE)
        return SLP_NULL_VAL;
    SlpObjString *name =
        slp_vm_stringify(vm, args[1]);
    if (!name) return SLP_NULL_VAL;
    SlpObjIOHandle *handle =
        SLP_AS_IO_HANDLE(args[0]);
    if (encoding_name_equals(
            name->chars, "UTF-8") ||
        encoding_name_equals(
            name->chars, "UTF8")) {
        handle->text_encoding = 0;
    } else if (
        encoding_name_equals(
            name->chars, "ISO-8859-1") ||
        encoding_name_equals(
            name->chars, "ISO8859-1") ||
        encoding_name_equals(
            name->chars, "LATIN1")) {
        handle->text_encoding = 1;
    } else if (
        encoding_name_equals(
            name->chars, "US-ASCII") ||
        encoding_name_equals(
            name->chars, "ASCII")) {
        handle->text_encoding = 2;
    } else {
        size_t capacity =
            strlen(
                "&setEncoding: specified a "
                "non-existent encoding ''") +
            name->length + 1;
        char *message =
            (char*)stdlib_alloc(
                vm, capacity);
        if (message) {
            snprintf(
                message, capacity,
                "&setEncoding: specified a "
                "non-existent encoding '%s'",
                name->chars);
            slp_vm_abort_warning(vm, message);
            stdlib_free(vm, message);
        }
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_getConsole(
    SlpVM *vm, SlpValue *args, int argc) {
    (void)args;
    (void)argc;
    SlpObjIOHandle *handle =
        slp_vm_get_console_handle(vm);
    return handle
        ? SLP_OBJ_VAL(handle)
        : SLP_NULL_VAL;
}

static SlpValue builtin_printEOF(
    SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 ||
        !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) !=
            SLP_OBJ_IO_HANDLE)
        return SLP_NULL_VAL;
    SlpObjIOHandle *handle =
        SLP_AS_IO_HANDLE(args[0]);
    if (handle->is_console) {
        fflush(stdout);
        return SLP_NULL_VAL;
    }
    if (handle->is_pipeline &&
        handle->socket_fd != -1) {
#ifdef _WIN32
        slp_platform_close_socket(
            handle->socket_fd);
#else
        close(handle->socket_fd);
#endif
        handle->socket_fd = -1;
    } else if (
        handle->is_socket &&
        handle->socket_fd != -1) {
        slp_platform_shutdown_socket_write(
            handle->socket_fd);
    } else if (handle->file) {
        fclose(handle->file);
        handle->file = NULL;
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_readb(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE) {
        return SLP_NULL_VAL;
    }
    SlpObjIOHandle *h = SLP_AS_IO_HANDLE(args[0]);
    int to_read = (int)slp_value_as_number(args[1]);
    if (h->is_eof) return SLP_NULL_VAL;
    
    if (to_read <= 0) {
        size_t capacity = 4096;
        size_t total = 0;
        uint8_t *buf = SLP_MALLOC(vm->allocator, capacity);
        if (!buf) return SLP_NULL_VAL;
        
        while (1) {
            if (total == capacity) {
                size_t new_cap = capacity * 2;
                uint8_t *new_buf = SLP_REALLOC(vm->allocator, buf, new_cap);
                if (!new_buf) {
                    SLP_FREE(vm->allocator, buf);
                    return SLP_NULL_VAL;
                }
                buf = new_buf;
                capacity = new_cap;
            }
            int actual = 0;
            if (h->file) {
                actual = (int)fread(buf + total, 1, capacity - total, h->file);
                if (actual == 0) {
                    h->is_eof = true;
                    break;
                }
            } else if (h->is_socket && h->socket_fd != -1) {
                actual = (int)recv(h->socket_fd, (char*)buf + total, capacity - total, 0);
                if (actual <= 0) {
                    h->is_eof = true;
                    break;
                }
            } else {
                break;
            }
            total += actual;
        }
        
        if (total == 0) {
            SLP_FREE(vm->allocator, buf);
            return SLP_NULL_VAL;
        }
        
        SlpObjString *s = slp_vm_copy_string(vm, (const char*)buf, (uint32_t)total);
        SLP_FREE(vm->allocator, buf);
        return SLP_OBJ_VAL(s);
    }
    
    uint8_t *buf = SLP_MALLOC(vm->allocator, (size_t)to_read);
    if (!buf) return SLP_NULL_VAL;
    
    int actual = 0;
    if (h->file) {
        actual = (int)fread(buf, 1, (size_t)to_read, h->file);
        if (actual == 0) h->is_eof = true;
    } else if (h->is_socket && h->socket_fd != -1) {
        actual = (int)recv(h->socket_fd, (char*)buf, (size_t)to_read, 0);
        if (actual <= 0) {
            actual = 0;
            h->is_eof = true;
        }
    }
    
    SlpObjString *s = slp_vm_copy_string(vm, (const char*)buf, (uint32_t)actual);
    SLP_FREE(vm->allocator, buf);
    return SLP_OBJ_VAL(s);
}

static SlpValue builtin_consume(
    SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 ||
        !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) !=
            SLP_OBJ_IO_HANDLE)
        return SLP_NULL_VAL;
    SlpObjIOHandle *handle =
        SLP_AS_IO_HANDLE(args[0]);
    int64_t remaining =
        stdlib_as_int64(args[1]);
    if (remaining <= 0)
        return SLP_NULL_VAL;
    int chunk_size =
        argc >= 3
            ? (int)slp_value_as_number(args[2])
            : 32 * 1024;
    if (chunk_size < 1)
        chunk_size = 32 * 1024;
    char *buffer =
        (char*)stdlib_alloc(
            vm, (size_t)chunk_size);
    if (!buffer) return SLP_NULL_VAL;

    int64_t consumed = 0;
    while (remaining > 0) {
        size_t requested =
            remaining < chunk_size
                ? (size_t)remaining
                : (size_t)chunk_size;
        int actual = 0;
        if (handle->file) {
            actual = (int)fread(
                buffer, 1, requested,
                handle->file);
        } else if (
            handle->socket_fd != -1) {
#ifdef _WIN32
            actual = recv(
                handle->socket_fd,
                buffer, (int)requested, 0);
#else
            actual = (int)recv(
                handle->socket_fd,
                buffer, requested, 0);
#endif
        }
        if (actual <= 0) {
            handle->is_eof = true;
            break;
        }
        consumed += actual;
        remaining -= actual;
    }
    stdlib_free(vm, buffer);
    return consumed > 0
        ? SLP_NUM_VAL((double)consumed)
        : SLP_NULL_VAL;
}

static SlpValue builtin_writeb(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING) {
        return SLP_BOOL_VAL(false);
    }
    SlpObjIOHandle *h = SLP_AS_IO_HANDLE(args[0]);
    SlpObjString *str = SLP_AS_STRING(args[1]);
    
    int written = 0;
    if (h->is_console) {
        slp_vm_write(vm, str->chars);
        written = (int)str->length;
    } else if (h->is_pipeline && h->socket_fd != -1) {
#ifdef _WIN32
        written = 0;
#else
        written = (int)write(h->socket_fd, str->chars, str->length);
#endif
    } else if (h->file) {
        written = (int)fwrite(str->chars, 1, str->length, h->file);
        fflush(h->file);
    } else if (h->is_socket && h->socket_fd != -1) {
#ifdef _WIN32
        written = (int)send(h->socket_fd, str->chars, (int)str->length, 0);
#else
        written = (int)send(h->socket_fd, str->chars, str->length, 0);
#endif
    }
    return SLP_BOOL_VAL(written > 0);
}

static SlpValue builtin_closef(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE) {
        return SLP_BOOL_VAL(false);
    }
    SlpObjIOHandle *h = SLP_AS_IO_HANDLE(args[0]);
    if (h->is_console)
        return SLP_BOOL_VAL(true);
    if (h->is_memory && h->file) {
        if (!h->memory_readable) {
            fflush(h->file);
            rewind(h->file);
            h->memory_readable = true;
            h->memory_text_buffered = false;
            h->memory_mark = 0;
            h->object_read_index = 0;
            h->is_eof = false;
        } else {
            fclose(h->file);
            h->file = NULL;
            h->is_eof = true;
        }
        return SLP_BOOL_VAL(true);
    }
    if (h->file) {
        slp_platform_close_file(h->file, h->is_pipeline);
        h->file = NULL;
    }
    if (h->socket_fd != -1) {
        slp_platform_close_socket(h->socket_fd);
        h->socket_fd = -1;
    }
    if (h->pid != -1) {
        int status;
        slp_platform_waitpid(h->pid, &status, 0);
        h->pid = -1;
    }
    return SLP_BOOL_VAL(true);
}

static int estimate_pack_size(const char *format) {
    int total = 0;
    int len = (int)strlen(format);
    int x = 0;
    while (x < len) {
        char c = format[x];
        if (c == '+' || c == '-' || c == '!') {
            x++;
            continue;
        }
        if (isalpha((unsigned char)c)) {
            x++;
            int count = 1;
            if (x < len && format[x] == '*') {
                count = 1024;
                x++;
            } else if (x < len && isdigit((unsigned char)format[x])) {
                count = 0;
                while (x < len && isdigit((unsigned char)format[x])) {
                    count = count * 10 + (format[x] - '0');
                    x++;
                }
            }
            int unit = 1;
            switch (c) {
                case 'c': case 's': case 'S': unit = 2; break;
                case 'i': case 'I': case 'f': unit = 4; break;
                case 'd': case 'l': unit = 8; break;
                default: unit = 1; break;
            }
            total += count * unit;
        } else {
            x++;
        }
    }
    return total;
}

static SlpValue builtin_bread(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING) {
        return SLP_NULL_VAL;
    }
    const char *format = SLP_AS_STRING(args[1])->chars;
    int size = estimate_pack_size(format);
    if (size <= 0) size = 1024;
    
    SlpValue read_args[2] = {args[0], SLP_NUM_VAL((double)size)};
    SlpValue data = builtin_readb(vm, read_args, 2);
    if (SLP_IS_NULL(data)) return SLP_NULL_VAL;
    
    SlpValue unpack_args[2] = {args[1], data};
    return builtin_unpack(vm, unpack_args, 2);
}

static SlpValue builtin_bwrite(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE) {
        return SLP_BOOL_VAL(false);
    }
    SlpValue packed = builtin_pack(vm, args + 1, argc - 1);
    if (SLP_IS_NULL(packed)) return SLP_BOOL_VAL(false);
    
    SlpValue write_args[2] = {args[0], packed};
    return builtin_writeb(vm, write_args, 2);
}

static SlpValue builtin_mark(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE)
        return SLP_NULL_VAL;
    SlpObjIOHandle *handle = SLP_AS_IO_HANDLE(args[0]);
    if (!handle->file || !handle->is_memory ||
        !handle->memory_readable)
        return SLP_NULL_VAL;
    long position = ftell(handle->file);
    if (position >= 0)
        handle->memory_mark = position;
    return SLP_NULL_VAL;
}

static SlpValue builtin_reset(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) ||
        SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE)
        return SLP_NULL_VAL;
    SlpObjIOHandle *handle = SLP_AS_IO_HANDLE(args[0]);
    if (!handle->file || !handle->is_memory ||
        !handle->memory_readable)
        return SLP_NULL_VAL;
    if (fseek(handle->file, handle->memory_mark, SEEK_SET) == 0) {
        clearerr(handle->file);
        handle->is_eof = false;
        handle->memory_text_buffered = false;
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_available(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE) {
        return SLP_NULL_VAL;
    }
    SlpObjIOHandle *h = SLP_AS_IO_HANDLE(args[0]);
    if (h->is_memory && !h->memory_readable)
        return SLP_NULL_VAL;
    if (h->is_eof || h->memory_text_buffered)
        return SLP_NUM_VAL(0);
    if (h->file) {
        long current = ftell(h->file);
        if (current < 0) return SLP_NULL_VAL;
        if (fseek(h->file, 0, SEEK_END) != 0)
            return SLP_NULL_VAL;
        long end = ftell(h->file);
        fseek(h->file, current, SEEK_SET);
        if (end < current) return SLP_NUM_VAL(0);
        return SLP_NUM_VAL((double)(end - current));
    }
    if (h->is_socket && h->socket_fd != -1)
        return SLP_NUM_VAL(1);
    return SLP_NUM_VAL(0);
}

static SlpValue builtin_sizeof(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    SlpValue arg = args[0];
    if (SLP_IS_OBJ(arg)) {
        SlpObj *obj = SLP_AS_OBJ(arg);
        if (obj->type == SLP_OBJ_STRING)
            return SLP_NUM_VAL((double)((SlpObjString*)obj)->length);
        if (obj->type == SLP_OBJ_ARRAY)
            return SLP_NUM_VAL((double)((SlpObjArray*)obj)->count);
        if (obj->type == SLP_OBJ_HASH)
            return SLP_NUM_VAL(
                (double)slp_obj_hash_visible_count((SlpObjHash*)obj));
    }
    return SLP_NUM_VAL(0);
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void slp_stdlib_init(SlpVM *vm) {
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }

    slp_vm_register_native(vm, "iff",       builtin_iff);
    slp_vm_register_native(vm, "function",  builtin_function);
    slp_vm_register_native(vm, "setf",      builtin_setf);
    slp_vm_register_native(vm, "warn",      builtin_warn);
    slp_vm_register_native(vm, "ohash",     builtin_ohash);
    slp_vm_register_native(vm, "ohasha",    builtin_ohasha);
    slp_vm_register_native(vm, "setMissPolicy", builtin_setMissPolicy);
    slp_vm_register_native(
        vm, "setRemovalPolicy", builtin_setRemovalPolicy);
    slp_vm_register_native(vm, "lambda",    builtin_lambda);
    slp_vm_register_native(vm, "let",       builtin_let);
    slp_vm_register_native(
        vm, "compile_closure",
        builtin_compile_closure);
    slp_vm_register_native(
        vm, "newInstance",
        builtin_newInstance);
    slp_vm_register_native(
        vm, "setField",
        builtin_setField);
    slp_vm_register_native(
        vm, "use",
        builtin_use);
    slp_vm_register_native_raw(
        vm, "taint",
        builtin_taint);
    slp_vm_register_native_raw(
        vm, "untaint",
        builtin_untaint);

    slp_vm_register_native(vm, "abs",       builtin_abs);
    slp_vm_register_native(vm, "ceil",      builtin_ceil);
    slp_vm_register_native(vm, "floor",     builtin_floor);
    slp_vm_register_native(vm, "sqrt",      builtin_sqrt);
    slp_vm_register_native(vm, "sin",       builtin_sin);
    slp_vm_register_native(vm, "cos",       builtin_cos);
    slp_vm_register_native(vm, "tan",       builtin_tan);
    slp_vm_register_native(vm, "asin",      builtin_asin);
    slp_vm_register_native(vm, "acos",      builtin_acos);
    slp_vm_register_native(vm, "atan",      builtin_atan);
    slp_vm_register_native(vm, "atan2",     builtin_atan2);
    slp_vm_register_native(vm, "exp",       builtin_exp);
    slp_vm_register_native(vm, "log",       builtin_log);
    slp_vm_register_native(vm, "round",     builtin_round);
    slp_vm_register_native(vm, "double",    builtin_double);
    slp_vm_register_native(vm, "not",       builtin_not);
    slp_vm_register_native(vm, "degrees",   builtin_degrees);
    slp_vm_register_native(vm, "radians",   builtin_radians);
    slp_vm_register_native(vm, "min",       builtin_min);
    slp_vm_register_native(vm, "max",       builtin_max);

    slp_vm_register_native(vm, "strlen",    builtin_strlen);
    slp_vm_register_native(vm, "substr",    builtin_substr);
    slp_vm_register_native(vm, "lc",        builtin_lc);
    slp_vm_register_native(vm, "uc",        builtin_uc);
    slp_vm_register_native(vm, "replace",   builtin_replace);
    slp_vm_register_native(vm, "strrep",    builtin_replace);
    slp_vm_register_native(vm, "split",     builtin_split);
    slp_vm_register_native(vm, "join",      builtin_join);
    slp_vm_register_native(vm, "chr",       builtin_chr);
    slp_vm_register_native(vm, "asc",       builtin_asc);
    slp_vm_register_native(vm, "charAt",    builtin_charAt);
    slp_vm_register_native(vm, "left",      builtin_left);
    slp_vm_register_native(vm, "right",     builtin_right);
    slp_vm_register_native(vm, "mid",       builtin_mid);
    slp_vm_register_native(vm, "find",      builtin_find);
    slp_vm_register_native(vm, "indexOf",   builtin_indexOf);
    slp_vm_register_native(vm, "formatNumber", builtin_formatNumber);
    slp_vm_register_native(vm, "tr",        builtin_tr);
    slp_vm_register_native(vm, "replaceAt", builtin_replaceAt);
    slp_vm_register_native(vm, "lindexOf",  builtin_lindexOf);

    slp_vm_register_native(vm, "add",       builtin_add);
    slp_vm_register_native(vm, "copy",      builtin_copy);
    slp_vm_register_native(vm, "clear",     builtin_clear);
    slp_vm_register_native(vm, "sort",      builtin_sort);
    slp_vm_register_native(vm, "sorta",     builtin_sorta);
    slp_vm_register_native(vm, "sortd",     builtin_sortd);
    slp_vm_register_native(vm, "sortn",     builtin_sortn);
    slp_vm_register_native(vm, "reverse",   builtin_reverse);
    slp_vm_register_native(vm, "shift",     builtin_shift);
    slp_vm_register_native(vm, "flatten",   builtin_flatten);
    slp_vm_register_native(vm, "concat",    builtin_concat);
    slp_vm_register_native(vm, "sum",       builtin_sum);
    slp_vm_register_native(vm, "removeAt",  builtin_removeAt);
    slp_vm_register_native(vm, "putAll",    builtin_putAll);
    slp_vm_register_native(vm, "contains",  builtin_contains);

    slp_vm_register_native(vm, "int",       builtin_int);
    slp_vm_register_native(vm, "long",      builtin_long);
    slp_vm_register_native(vm, "uint",      builtin_uint);
    slp_vm_register_native(vm, "local",     builtin_local);
    slp_vm_register_native(vm, "this",      builtin_this);
    slp_vm_register_native(vm, "watch",     builtin_watch);
    slp_vm_register_native(vm, "values",    builtin_values);
    slp_vm_register_native(vm, "sublist",   builtin_sublist);
    slp_vm_register_native(vm, "cast",      builtin_cast);
    slp_vm_register_native(vm, "casti",     builtin_casti);
    slp_vm_register_native(vm, "scalar",    builtin_scalar);
    slp_vm_register_native(vm, "inline",    builtin_inline);

    slp_vm_register_native(vm, "typeOf",    builtin_typeOf);
    slp_vm_register_native(vm, "getStackTrace", builtin_getStackTrace);
    slp_vm_register_native(vm, "profile",   builtin_profile);
    slp_vm_register_native(vm, "semaphore", builtin_semaphore);
    slp_vm_register_native(vm, "acquire",   builtin_acquire);
    slp_vm_register_native(vm, "release",   builtin_release);
    slp_vm_register_native(
        vm, "systemProperties",
        builtin_systemProperties);
    slp_vm_register_native(vm, "global",    builtin_global);
    slp_vm_register_native(vm, "byteAt",    builtin_byteAt);

    slp_vm_register_native(vm, "print",     builtin_print);
    slp_vm_register_native(vm, "println",   builtin_println);
    slp_vm_register_native(vm, "printf",    builtin_println);
    slp_vm_register_native(vm, "printAll",  builtin_printAll);
    slp_vm_register_native(vm, "ticks",     builtin_ticks);
    slp_vm_register_native(vm, "rand",      builtin_rand);
    slp_vm_register_native(vm, "srand",     builtin_srand);
    slp_vm_register_native(vm, "tstamp",    builtin_tstamp);
    slp_vm_register_native(vm, "sleep",     builtin_sleep_ms);

    /* Expanded bridge builtins */
    slp_vm_register_native(vm, "matched",         builtin_matched);
    slp_vm_register_native(vm, "matches",         builtin_matches);
    slp_vm_register_native(vm, "pack",            builtin_pack);
    slp_vm_register_native(vm, "unpack",          builtin_unpack);
    slp_vm_register_native(vm, "ls",              builtin_ls);
    slp_vm_register_native(vm, "listRoots",       builtin_listRoots);
    slp_vm_register_native(vm, "createNewFile",   builtin_createNewFile);
    slp_vm_register_native(vm, "mkdir",           builtin_mkdir);
    slp_vm_register_native(vm, "deleteFile",      builtin_deleteFile);
    slp_vm_register_native(vm, "rename",          builtin_rename);
    slp_vm_register_native(vm, "lof",             builtin_lof);
    slp_vm_register_native(vm, "lastModified",    builtin_lastModified);
    slp_vm_register_native(vm, "setLastModified", builtin_setLastModified);
    slp_vm_register_native(vm, "setReadOnly",     builtin_setReadOnly);
    slp_vm_register_native(vm, "cwd",             builtin_cwd);
    slp_vm_register_native(
        vm, "getCurrentDirectory", builtin_cwd);
    slp_vm_register_native(vm, "chdir",           builtin_chdir);
    slp_vm_register_native(vm, "getFileName",     builtin_getFileName);
    slp_vm_register_native(vm, "getFileParent",   builtin_getFileParent);
    slp_vm_register_native(vm, "getFileProper",   builtin_getFileProper);
    slp_vm_register_native(vm, "formatDate",      builtin_formatDate);
    slp_vm_register_native(vm, "parseDate",       builtin_parseDate);

    /* Digests & Cryptography */
    slp_vm_register_native(vm, "checksum",        builtin_checksum);
    slp_vm_register_native(vm, "digest",          builtin_digest);

    /* Functional programming */
    slp_vm_register_native(vm, "map",             builtin_map);
    slp_vm_register_native(vm, "filter",          builtin_filter);
    slp_vm_register_native(vm, "reduce",          builtin_reduce);

    /* Array / list utilities */
    slp_vm_register_native(vm, "splice",          builtin_splice);
    slp_vm_register_native(vm, "subarray",        builtin_subarray);
    slp_vm_register_native(vm, "addAll",          builtin_addAll);
    slp_vm_register_native(vm, "removeAll",        builtin_removeAll);
    slp_vm_register_native(vm, "retainAll",        builtin_retainAll);
    slp_vm_register_native(vm, "pushl",            builtin_pushl);
    slp_vm_register_native(vm, "popl",             builtin_popl);
    slp_vm_register_native(vm, "search",           builtin_search);

    /* Number parsing */
    slp_vm_register_native(vm, "parseNumber",     builtin_parseNumber);

    /* Standard and network I/O, Subprocesses */
    slp_vm_register_native(vm, "allocate",        builtin_allocate);
    slp_vm_register_native(vm, "writeObject",     builtin_writeObject);
    slp_vm_register_native(vm, "writeAsObject",   builtin_writeObject);
    slp_vm_register_native(vm, "readObject",      builtin_readObject);
    slp_vm_register_native(vm, "readAsObject",    builtin_readAsObject);
    slp_vm_register_native(vm, "include",         builtin_include);
    slp_vm_register_native(vm, "eval",            builtin_eval);
    slp_vm_register_native(vm, "expr",            builtin_eval);
    slp_vm_register_native_raw(
        vm, "checkError", builtin_check_error);
    slp_vm_register_native_raw(
        vm, "fork", builtin_fork);
    slp_vm_register_native(vm, "wait",            builtin_wait);
    slp_vm_register_native(vm, "exit",            builtin_exit);
    slp_vm_register_native(vm, "openf",           builtin_openf);
    slp_vm_register_native(vm, "connect",         builtin_connect);
    slp_vm_register_native(vm, "listen",          builtin_listen);
    slp_vm_register_native(vm, "exec",            builtin_exec);
    slp_vm_register_native(vm, "readln",          builtin_readln);
    slp_vm_register_native(vm, "readAll",         builtin_readAll);
    slp_vm_register_native(vm, "readc",           builtin_readc);
    slp_vm_register_native(vm, "readb",           builtin_readb);
    slp_vm_register_native(vm, "setEncoding",     builtin_setEncoding);
    slp_vm_register_native(vm, "getConsole",      builtin_getConsole);
    slp_vm_register_native(vm, "printEOF",        builtin_printEOF);
    slp_vm_register_native(vm, "consume",         builtin_consume);
    slp_vm_register_native(vm, "skip",            builtin_consume);
    slp_vm_register_native(vm, "writeb",          builtin_writeb);
    slp_vm_register_native(vm, "closef",          builtin_closef);
    slp_vm_register_native(vm, "bread",           builtin_bread);
    slp_vm_register_native(vm, "bwrite",          builtin_bwrite);
    slp_vm_register_native(vm, "mark",            builtin_mark);
    slp_vm_register_native(vm, "reset",           builtin_reset);
    slp_vm_register_native(vm, "available",       builtin_available);
    slp_vm_register_native(vm, "sizeof",          builtin_sizeof);
}
