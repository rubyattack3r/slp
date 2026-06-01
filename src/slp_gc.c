#include "slp_gc.h"
#include "slp_vm.h"
#include "slp_platform.h"
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
    case SLP_OBJ_LONG:
        break;
    case SLP_OBJ_ARRAY: {
        SlpObjArray *arr = (SlpObjArray*)obj;
        for (int i = 0; i < arr->count; i++)
            slp_gc_mark_value(vm, arr->elements[i]);
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
        break;
    }
    case SLP_OBJ_FUNCTION: {
        SlpObjFunction *fn = (SlpObjFunction*)obj;
        if (fn->name) slp_gc_mark_obj(vm, (SlpObj*)fn->name);
        if (fn->chunk) {
            for (int i = 0; i < fn->chunk->constant_count; i++)
                slp_gc_mark_value(vm, fn->chunk->constants[i]);
        }
        break;
    }
    case SLP_OBJ_CLOSURE: {
        SlpObjClosure *closure = (SlpObjClosure*)obj;
        slp_gc_mark_obj(vm, (SlpObj*)closure->function);
        for (int i = 0; i < closure->function->upvalue_count; i++)
            if (closure->upvalues[i])
                slp_gc_mark_obj(vm, (SlpObj*)closure->upvalues[i]);
        if (closure->coroutine_stack) {
            for (int i = 0; i < closure->coroutine_stack_count; i++) {
                slp_gc_mark_value(vm, closure->coroutine_stack[i]);
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
            for (int i = 0; i < cont->frame_count; i++)
                slp_gc_mark_obj(vm, (SlpObj*)cont->frames[i].closure);
        }
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
    case SLP_OBJ_IO_HANDLE:
        break;
    }
}

static void mark_roots(SlpVM *vm) {
    for (SlpValue *slot = vm->stack; slot < vm->stack_top; slot++)
        slp_gc_mark_value(vm, *slot);

    for (int i = 0; i < vm->frame_count; i++) {
        slp_gc_mark_obj(vm, (SlpObj*)vm->frames[i].closure);
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
    slp_gc_mark_value(vm, vm->thrown_exception);

    for (int i = 0; i < 256; i++) {
        slp_gc_mark_value(vm, vm->ffi_slots[i]);
    }

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
        if (closure->coroutine_stack) SLP_FREE(vm->allocator, closure->coroutine_stack);
        break;
    }
    case SLP_OBJ_CONTINUATION: {
        SlpObjContinuation *cont = (SlpObjContinuation*)obj;
        if (cont->frames) SLP_FREE(vm->allocator, cont->frames);
        if (cont->stack) SLP_FREE(vm->allocator, cont->stack);
        break;
    }
    case SLP_OBJ_IO_HANDLE: {
        SlpObjIOHandle *handle = (SlpObjIOHandle*)obj;
        if (handle->file) {
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
    case SLP_OBJ_LONG: return sizeof(SlpObjLong);
    case SLP_OBJ_ARRAY: return sizeof(SlpObjArray);
    case SLP_OBJ_HASH: return sizeof(SlpObjHash);
    case SLP_OBJ_FUNCTION: return sizeof(SlpObjFunction);
    case SLP_OBJ_CLOSURE: return sizeof(SlpObjClosure);
    case SLP_OBJ_UPVALUE: return sizeof(SlpObjUpvalue);
    case SLP_OBJ_CONTINUATION: return sizeof(SlpObjContinuation);
    case SLP_OBJ_NATIVE: return sizeof(SlpObjNative);
    case SLP_OBJ_BRIDGE: return sizeof(SlpObjBridge);
    case SLP_OBJ_IO_HANDLE: return sizeof(SlpObjIOHandle);
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
