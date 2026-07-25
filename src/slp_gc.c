#include "slp_gc.h"
#include "slp_vm.h"
#include "slp_platform.h"
#include "slp_embed_internal.h"
#include <stdio.h>

void slp_gc_init(SlpVM *vm) {
    vm->bytes_allocated = 0;
    vm->next_gc_threshold = 1024 * 1024;
    vm->gc_gray_stack = NULL;
    vm->gc_gray_count = 0;
    vm->gc_gray_capacity = 0;
}

static void gc_gray_stack_push(SlpVM *vm, SlpObj *obj) {
    if (vm->gc_gray_count + 1 > vm->gc_gray_capacity) {
        int new_cap = vm->gc_gray_capacity == 0 ? 64 : vm->gc_gray_capacity * 2;
        vm->gc_gray_stack = (SlpObj**)SLP_REALLOC(vm->allocator, vm->gc_gray_stack, sizeof(SlpObj*) * new_cap);
        vm->gc_gray_capacity = new_cap;
    }
    vm->gc_gray_stack[vm->gc_gray_count++] = obj;
}

static void blacken_object(SlpVM *vm, SlpObj *obj) {
    switch (obj->type) {
    case SLP_OBJ_STRING:
        break;
    case SLP_OBJ_CLASS:
        slp_gc_mark_obj(
            vm, (SlpObj*)((SlpObjClass*)obj)->name);
        if (((SlpObjClass*)obj)->interfaces)
            slp_gc_mark_obj(
                vm,
                (SlpObj*)((SlpObjClass*)obj)
                    ->interfaces);
        if (((SlpObjClass*)obj)->fields)
            slp_gc_mark_obj(
                vm,
                (SlpObj*)((SlpObjClass*)obj)
                    ->fields);
        break;
    case SLP_OBJ_JAVA_OBJECT: {
        SlpObjJavaObject *java_object =
            (SlpObjJavaObject*)obj;
        slp_gc_mark_obj(
            vm, (SlpObj*)java_object->class_object);
        if (java_object->list)
            slp_gc_mark_obj(vm, (SlpObj*)java_object->list);
        if (java_object->map)
            slp_gc_mark_obj(vm, (SlpObj*)java_object->map);
        if (java_object->fields)
            slp_gc_mark_obj(
                vm, (SlpObj*)java_object->fields);
        slp_gc_mark_value(vm, java_object->value);
        break;
    }
    case SLP_OBJ_LONG:
        break;
    case SLP_OBJ_DOUBLE:
        break;
    case SLP_OBJ_ARRAY: {
        SlpObjArray *arr = (SlpObjArray*)obj;
        for (int i = 0; i < arr->count; i++)
            slp_gc_mark_value(vm, arr->elements[i]);
        if (arr->view_source)
            slp_gc_mark_obj(vm, (SlpObj*)arr->view_source);
        break;
    }
    case SLP_OBJ_HASH: {
        SlpObjHash *hash = (SlpObjHash*)obj;
        for (int i = 0; i < hash->capacity; i++) {
            if (!SLP_IS_NULL(hash->entries[i].key)) {
                slp_gc_mark_value(vm, hash->entries[i].key);
                slp_gc_mark_value(vm, hash->entries[i].value);
            }
        }
        if (hash->miss_policy)
            slp_gc_mark_obj(vm, (SlpObj*)hash->miss_policy);
        if (hash->removal_policy)
            slp_gc_mark_obj(vm, (SlpObj*)hash->removal_policy);
        break;
    }
    case SLP_OBJ_FUNCTION: {
        SlpObjFunction *fn = (SlpObjFunction*)obj;
        if (fn->name) slp_gc_mark_obj(vm, (SlpObj*)fn->name);
        if (fn->source_name)
            slp_gc_mark_obj(vm, (SlpObj*)fn->source_name);
        if (fn->chunk) {
            for (int i = 0; i < fn->chunk->constant_count; i++)
                slp_gc_mark_value(vm, fn->chunk->constants[i]);
        }
        break;
    }
    case SLP_OBJ_CLOSURE: {
        SlpObjClosure *closure = (SlpObjClosure*)obj;
        slp_gc_mark_obj(vm, (SlpObj*)closure->function);
        if (closure->scope)
            slp_gc_mark_obj(vm, (SlpObj*)closure->scope);
        if (closure->call_name)
            slp_gc_mark_obj(vm, (SlpObj*)closure->call_name);
        for (int i = 0; i < closure->regex_state_count; i++) {
            slp_gc_mark_obj(vm, (SlpObj*)closure->regex_states[i].text);
            slp_gc_mark_obj(vm, (SlpObj*)closure->regex_states[i].pattern);
        }
        if (closure->last_regex_matches)
            slp_gc_mark_obj(
                vm, (SlpObj*)closure->last_regex_matches);
        for (int i = 0; i < closure->function->upvalue_count; i++)
            if (closure->upvalues[i])
                slp_gc_mark_obj(vm, (SlpObj*)closure->upvalues[i]);
        if (closure->coroutine_stack) {
            for (int i = 0; i < closure->coroutine_stack_count; i++) {
                slp_gc_mark_value(vm, closure->coroutine_stack[i]);
            }
        }
        if (closure->coroutine_frames) {
            for (int i = 0; i < closure->coroutine_frame_count; i++) {
                SlpCallFrame *frame = &closure->coroutine_frames[i];
                slp_gc_mark_obj(vm, (SlpObj*)frame->closure);
                if (frame->local_scopes)
                    slp_gc_mark_obj(vm, (SlpObj*)frame->local_scopes);
                if (frame->closure_scope)
                    slp_gc_mark_obj(vm, (SlpObj*)frame->closure_scope);
                if (frame->continuation_return)
                    slp_gc_mark_obj(
                        vm, (SlpObj*)frame->continuation_return);
                if (frame->trace_call)
                    slp_gc_mark_obj(vm, (SlpObj*)frame->trace_call);
                if (frame->trace_source)
                    slp_gc_mark_obj(vm, (SlpObj*)frame->trace_source);
            }
        }
        break;
    }
    case SLP_OBJ_UPVALUE:
        slp_gc_mark_value(vm, ((SlpObjUpvalue*)obj)->closed);
        break;
    case SLP_OBJ_CONTINUATION: {
        SlpObjContinuation *cont = (SlpObjContinuation*)obj;
        if (cont->stack) {
            for (int i = 0; i < cont->stack_count; i++)
                slp_gc_mark_value(vm, cont->stack[i]);
        }
        // The captured frames hold raw closure pointers; without marking them a
        // live continuation could have its frames' closures collected, leading
        // to a use-after-free when the continuation is resumed.
        if (cont->frames) {
            for (int i = 0; i < cont->frame_count; i++) {
                slp_gc_mark_obj(vm, (SlpObj*)cont->frames[i].closure);
                if (cont->frames[i].local_scopes)
                    slp_gc_mark_obj(vm,
                        (SlpObj*)cont->frames[i].local_scopes);
                if (cont->frames[i].closure_scope)
                    slp_gc_mark_obj(vm,
                        (SlpObj*)cont->frames[i].closure_scope);
                if (cont->frames[i].continuation_return)
                    slp_gc_mark_obj(
                        vm,
                        (SlpObj*)cont->frames[i].continuation_return);
                if (cont->frames[i].trace_call)
                    slp_gc_mark_obj(
                        vm, (SlpObj*)cont->frames[i].trace_call);
                if (cont->frames[i].trace_source)
                    slp_gc_mark_obj(
                        vm, (SlpObj*)cont->frames[i].trace_source);
            }
        }
        if (cont->coroutine_owner)
            slp_gc_mark_obj(vm, (SlpObj*)cont->coroutine_owner);
        if (cont->return_trace_call)
            slp_gc_mark_obj(vm, (SlpObj*)cont->return_trace_call);
        if (cont->return_trace_source)
            slp_gc_mark_obj(vm, (SlpObj*)cont->return_trace_source);
        break;
    }
    case SLP_OBJ_NATIVE: {
        SlpObjNative *native = (SlpObjNative*)obj;
        if (native->name) slp_gc_mark_obj(vm, (SlpObj*)native->name);
        break;
    }
    case SLP_OBJ_BRIDGE: {
        SlpObjBridge *bridge = (SlpObjBridge*)obj;
        if (bridge->keyword) slp_gc_mark_obj(vm, (SlpObj*)bridge->keyword);
        if (bridge->name) slp_gc_mark_obj(vm, (SlpObj*)bridge->name);
        if (bridge->closure) slp_gc_mark_obj(vm, (SlpObj*)bridge->closure);
        break;
    }
    case SLP_OBJ_IO_HANDLE: {
        SlpObjIOHandle *handle = (SlpObjIOHandle*)obj;
        slp_gc_mark_value(vm, handle->token);
        for (int i = 0; i < handle->object_count; i++)
            slp_gc_mark_value(vm, handle->object_values[i]);
        break;
    }
    case SLP_OBJ_KEY_VALUE: {
        SlpObjKeyValue *kv = (SlpObjKeyValue*)obj;
        slp_gc_mark_value(vm, kv->key);
        slp_gc_mark_value(vm, kv->value);
        break;
    }
    case SLP_OBJ_TAINTED:
        slp_gc_mark_value(
            vm,
            ((SlpObjTainted*)obj)->value);
        break;
    case SLP_OBJ_SCALAR_CELL: {
        SlpObjScalarCell *cell =
            (SlpObjScalarCell*)obj;
        slp_gc_mark_value(vm, cell->value);
        if (cell->watch_name)
            slp_gc_mark_obj(
                vm, (SlpObj*)cell->watch_name);
        break;
    }
    }
}

static void mark_roots(SlpVM *vm) {
    for (SlpValue *slot = vm->stack; slot < vm->stack_top; slot++)
        slp_gc_mark_value(vm, *slot);

    for (int i = 0; i < vm->frame_count; i++) {
        slp_gc_mark_obj(vm, (SlpObj*)vm->frames[i].closure);
        if (vm->frames[i].local_scopes)
            slp_gc_mark_obj(vm, (SlpObj*)vm->frames[i].local_scopes);
        if (vm->frames[i].closure_scope)
            slp_gc_mark_obj(vm, (SlpObj*)vm->frames[i].closure_scope);
        if (vm->frames[i].continuation_return)
            slp_gc_mark_obj(
                vm, (SlpObj*)vm->frames[i].continuation_return);
        if (vm->frames[i].trace_call)
            slp_gc_mark_obj(
                vm, (SlpObj*)vm->frames[i].trace_call);
        if (vm->frames[i].trace_source)
            slp_gc_mark_obj(
                vm, (SlpObj*)vm->frames[i].trace_source);
    }

    SlpObjUpvalue *uv = vm->open_upvalues;
    while (uv) {
        slp_gc_mark_obj(vm, (SlpObj*)uv);
        uv = uv->next;
    }

    if (vm->globals)
        slp_gc_mark_obj(vm, (SlpObj*)vm->globals);
    if (vm->natives)
        slp_gc_mark_obj(vm, (SlpObj*)vm->natives);
    if (vm->last_regex_matches)
        slp_gc_mark_obj(vm, (SlpObj*)vm->last_regex_matches);
    if (vm->last_stack_trace)
        slp_gc_mark_obj(vm, (SlpObj*)vm->last_stack_trace);
    if (vm->profile_statistics)
        slp_gc_mark_obj(
            vm,
            (SlpObj*)vm->profile_statistics);
    if (vm->packed_objects)
        slp_gc_mark_obj(
            vm, (SlpObj*)vm->packed_objects);
    if (vm->console_handle)
        slp_gc_mark_obj(
            vm, (SlpObj*)vm->console_handle);
    slp_gc_mark_value(vm, vm->thrown_exception);
    slp_gc_mark_value(vm, vm->last_error);

    for (int i = 0; i < 256; i++) {
        slp_gc_mark_value(vm, vm->ffi_slots[i]);
    }
    slp_embed_mark_roots(vm);

    SlpBridgeType *bt = vm->bridge_types;
    while (bt) {
        bt = bt->next;
    }
}

static void trace_references(SlpVM *vm) {
    while (vm->gc_gray_count > 0) {
        SlpObj *obj = vm->gc_gray_stack[--vm->gc_gray_count];
        blacken_object(vm, obj);
    }
}


static void free_obj_contents(SlpVM *vm, SlpObj *obj) {
    switch (obj->type) {
    case SLP_OBJ_ARRAY: {
        SlpObjArray *arr = (SlpObjArray*)obj;
        if (arr->elements) SLP_FREE(vm->allocator, arr->elements);
        break;
    }
    case SLP_OBJ_HASH: {
        SlpObjHash *hash = (SlpObjHash*)obj;
        if (hash->entries) SLP_FREE(vm->allocator, hash->entries);
        break;
    }
    case SLP_OBJ_FUNCTION: {
        SlpObjFunction *fn = (SlpObjFunction*)obj;
        if (fn->chunk) {
            slp_chunk_free(fn->chunk);
            SLP_FREE(vm->allocator, fn->chunk);
        }
        break;
    }
    case SLP_OBJ_CLOSURE: {
        SlpObjClosure *closure = (SlpObjClosure*)obj;
        if (closure->upvalues) SLP_FREE(vm->allocator, closure->upvalues);
        if (closure->regex_states)
            SLP_FREE(vm->allocator, closure->regex_states);
        if (closure->coroutine_stack) SLP_FREE(vm->allocator, closure->coroutine_stack);
        if (closure->coroutine_frames) SLP_FREE(vm->allocator, closure->coroutine_frames);
        if (closure->coroutine_try_handlers)
            SLP_FREE(vm->allocator, closure->coroutine_try_handlers);
        break;
    }
    case SLP_OBJ_CONTINUATION: {
        SlpObjContinuation *cont = (SlpObjContinuation*)obj;
        if (cont->frames) SLP_FREE(vm->allocator, cont->frames);
        if (cont->stack) SLP_FREE(vm->allocator, cont->stack);
        if (cont->try_handlers) SLP_FREE(vm->allocator, cont->try_handlers);
        break;
    }
    case SLP_OBJ_IO_HANDLE: {
        SlpObjIOHandle *handle = (SlpObjIOHandle*)obj;
        if (handle->object_values) {
            SLP_FREE(vm->allocator, handle->object_values);
            handle->object_values = NULL;
        }
        if (handle->file &&
            !handle->is_console) {
            slp_platform_close_file(handle->file, handle->is_pipeline);
            handle->file = NULL;
        }
        if (handle->socket_fd != -1) {
            slp_platform_close_socket(handle->socket_fd);
            handle->socket_fd = -1;
        }
        if (handle->pid != -1) {
            int status;
            slp_platform_waitpid(handle->pid, &status, 1 /* WNOHANG */);
            handle->pid = -1;
        }
        break;
    }
    default:
        break;
    }
}

void slp_gc_sweep(SlpVM *vm) {
    SlpObj **obj_ptr = &vm->objects;
    while (*obj_ptr) {
        if ((*obj_ptr)->is_marked) {
            (*obj_ptr)->is_marked = false;
            obj_ptr = &(*obj_ptr)->next;
        } else {
            SlpObj *unreached = *obj_ptr;
            *obj_ptr = unreached->next;
            size_t size = slp_gc_object_size(unreached);
            vm->bytes_allocated -= size;
            free_obj_contents(vm, unreached);
            SLP_FREE(vm->allocator, unreached);
        }
    }
}

size_t slp_gc_object_size(SlpObj *obj) {
    switch (obj->type) {
    case SLP_OBJ_STRING: {
        SlpObjString *s = (SlpObjString*)obj;
        return sizeof(SlpObjString) + s->length + 1;
    }
    case SLP_OBJ_CLASS: return sizeof(SlpObjClass);
    case SLP_OBJ_JAVA_OBJECT: return sizeof(SlpObjJavaObject);
    case SLP_OBJ_LONG: return sizeof(SlpObjLong);
    case SLP_OBJ_DOUBLE: return sizeof(SlpObjDouble);
    case SLP_OBJ_ARRAY: return sizeof(SlpObjArray);
    case SLP_OBJ_HASH: return sizeof(SlpObjHash);
    case SLP_OBJ_FUNCTION: return sizeof(SlpObjFunction);
    case SLP_OBJ_CLOSURE: return sizeof(SlpObjClosure);
    case SLP_OBJ_UPVALUE: return sizeof(SlpObjUpvalue);
    case SLP_OBJ_CONTINUATION: return sizeof(SlpObjContinuation);
    case SLP_OBJ_NATIVE: return sizeof(SlpObjNative);
    case SLP_OBJ_BRIDGE: return sizeof(SlpObjBridge);
    case SLP_OBJ_IO_HANDLE: return sizeof(SlpObjIOHandle);
    case SLP_OBJ_KEY_VALUE: return sizeof(SlpObjKeyValue);
    case SLP_OBJ_TAINTED: return sizeof(SlpObjTainted);
    case SLP_OBJ_SCALAR_CELL: return sizeof(SlpObjScalarCell);
    }
    return sizeof(SlpObj);
}

// The interned-string table holds raw, non-owning pointers. After sweeping,
// any swept string would leave a dangling entry, so rebuild the table from the
// surviving objects (which are exactly the strings still on vm->objects). This
// gives the table weak-reference semantics without tricky open-addressing
// deletion.
static void rebuild_interned(SlpVM *vm) {
    if (!vm->interned || vm->interned_capacity == 0) return;
    for (int i = 0; i < vm->interned_capacity; i++)
        vm->interned[i] = NULL;
    vm->interned_count = 0;
    for (SlpObj *o = vm->objects; o; o = o->next) {
        if (o->type != SLP_OBJ_STRING) continue;
        SlpObjString *s = (SlpObjString*)o;
        uint32_t idx = s->hash & (vm->interned_capacity - 1);
        while (vm->interned[idx])
            idx = (idx + 1) & (vm->interned_capacity - 1);
        vm->interned[idx] = s;
        vm->interned_count++;
    }
}

void slp_gc_collect(SlpVM *vm) {
    mark_roots(vm);
    trace_references(vm);
    slp_gc_sweep(vm);
    rebuild_interned(vm);
    vm->next_gc_threshold = vm->bytes_allocated * 3 / 2;
    if (vm->next_gc_threshold < 256 * 1024)
        vm->next_gc_threshold = 256 * 1024;
}

void slp_gc_mark_obj(SlpVM *vm, SlpObj *obj) {
    if (!obj || obj->is_marked) return;
    obj->is_marked = true;
    gc_gray_stack_push(vm, obj);
}

void slp_gc_mark_value(SlpVM *vm, SlpValue value) {
    if (SLP_IS_OBJ(value))
        slp_gc_mark_obj(vm, SLP_AS_OBJ(value));
}

void slp_gc_free(SlpVM *vm) {
    SlpObj *obj = vm->objects;
    while (obj) {
        SlpObj *next = obj->next;
        free_obj_contents(vm, obj);
        SLP_FREE(vm->allocator, obj);
        obj = next;
    }
    if (vm->gc_gray_stack)
        SLP_FREE(vm->allocator, vm->gc_gray_stack);
    vm->objects = NULL;
    vm->gc_gray_stack = NULL;
    vm->gc_gray_count = 0;
    vm->gc_gray_capacity = 0;
}
