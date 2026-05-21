#include "sleepy_gc.h"
#include "sleepy_vm.h"

void sleepy_gc_init(SleepyVM *vm) {
    vm->bytes_allocated = 0;
    vm->next_gc_threshold = 1024 * 1024;
    vm->gc_gray_stack = NULL;
    vm->gc_gray_count = 0;
    vm->gc_gray_capacity = 0;
}

static void gc_gray_stack_push(SleepyVM *vm, SleepyObj *obj) {
    if (vm->gc_gray_count + 1 > vm->gc_gray_capacity) {
        int new_cap = vm->gc_gray_capacity == 0 ? 64 : vm->gc_gray_capacity * 2;
        vm->gc_gray_stack = (SleepyObj**)SLEEPY_REALLOC(vm->allocator, vm->gc_gray_stack, sizeof(SleepyObj*) * new_cap);
        vm->gc_gray_capacity = new_cap;
    }
    vm->gc_gray_stack[vm->gc_gray_count++] = obj;
}

static void blacken_object(SleepyVM *vm, SleepyObj *obj) {
    switch (obj->type) {
    case SLEEPY_OBJ_STRING:
        break;
    case SLEEPY_OBJ_LONG:
        break;
    case SLEEPY_OBJ_ARRAY: {
        SleepyObjArray *arr = (SleepyObjArray*)obj;
        for (int i = 0; i < arr->count; i++)
            sleepy_gc_mark_value(vm, arr->elements[i]);
        break;
    }
    case SLEEPY_OBJ_HASH: {
        SleepyObjHash *hash = (SleepyObjHash*)obj;
        for (int i = 0; i < hash->capacity; i++) {
            if (!SLEEPY_IS_NULL(hash->entries[i].key)) {
                sleepy_gc_mark_value(vm, hash->entries[i].key);
                sleepy_gc_mark_value(vm, hash->entries[i].value);
            }
        }
        break;
    }
    case SLEEPY_OBJ_FUNCTION: {
        SleepyObjFunction *fn = (SleepyObjFunction*)obj;
        if (fn->name) sleepy_gc_mark_obj(vm, (SleepyObj*)fn->name);
        if (fn->chunk) {
            for (int i = 0; i < fn->chunk->constant_count; i++)
                sleepy_gc_mark_value(vm, fn->chunk->constants[i]);
        }
        break;
    }
    case SLEEPY_OBJ_CLOSURE: {
        SleepyObjClosure *closure = (SleepyObjClosure*)obj;
        sleepy_gc_mark_obj(vm, (SleepyObj*)closure->function);
        for (int i = 0; i < closure->function->upvalue_count; i++)
            if (closure->upvalues[i])
                sleepy_gc_mark_obj(vm, (SleepyObj*)closure->upvalues[i]);
        break;
    }
    case SLEEPY_OBJ_UPVALUE:
        sleepy_gc_mark_value(vm, ((SleepyObjUpvalue*)obj)->closed);
        break;
    case SLEEPY_OBJ_CONTINUATION: {
        SleepyObjContinuation *cont = (SleepyObjContinuation*)obj;
        if (cont->stack) {
            for (int i = 0; i < cont->stack_count; i++)
                sleepy_gc_mark_value(vm, cont->stack[i]);
        }
        break;
    }
    case SLEEPY_OBJ_NATIVE: {
        SleepyObjNative *native = (SleepyObjNative*)obj;
        if (native->name) sleepy_gc_mark_obj(vm, (SleepyObj*)native->name);
        break;
    }
    case SLEEPY_OBJ_BRIDGE: {
        SleepyObjBridge *bridge = (SleepyObjBridge*)obj;
        if (bridge->keyword) sleepy_gc_mark_obj(vm, (SleepyObj*)bridge->keyword);
        if (bridge->name) sleepy_gc_mark_obj(vm, (SleepyObj*)bridge->name);
        if (bridge->closure) sleepy_gc_mark_obj(vm, (SleepyObj*)bridge->closure);
        break;
    }
    }
}

static void mark_roots(SleepyVM *vm) {
    for (SleepyValue *slot = vm->stack; slot < vm->stack_top; slot++)
        sleepy_gc_mark_value(vm, *slot);

    for (int i = 0; i < vm->frame_count; i++) {
        sleepy_gc_mark_obj(vm, (SleepyObj*)vm->frames[i].closure);
    }

    SleepyObjUpvalue *uv = vm->open_upvalues;
    while (uv) {
        sleepy_gc_mark_obj(vm, (SleepyObj*)uv);
        uv = uv->next;
    }

    if (vm->globals)
        sleepy_gc_mark_obj(vm, (SleepyObj*)vm->globals);
    if (vm->natives)
        sleepy_gc_mark_obj(vm, (SleepyObj*)vm->natives);

    SleepyBridgeType *bt = vm->bridge_types;
    while (bt) {
        bt = bt->next;
    }
}

static void trace_references(SleepyVM *vm) {
    while (vm->gc_gray_count > 0) {
        SleepyObj *obj = vm->gc_gray_stack[--vm->gc_gray_count];
        blacken_object(vm, obj);
    }
}

void sleepy_gc_sweep(SleepyVM *vm) {
    SleepyObj **obj_ptr = &vm->objects;
    while (*obj_ptr) {
        if ((*obj_ptr)->is_marked) {
            (*obj_ptr)->is_marked = false;
            obj_ptr = &(*obj_ptr)->next;
        } else {
            SleepyObj *unreached = *obj_ptr;
            *obj_ptr = unreached->next;
            size_t size = sleepy_gc_object_size(unreached);
            vm->bytes_allocated -= size;
            SLEEPY_FREE(vm->allocator, unreached);
        }
    }
}

size_t sleepy_gc_object_size(SleepyObj *obj) {
    switch (obj->type) {
    case SLEEPY_OBJ_STRING: {
        SleepyObjString *s = (SleepyObjString*)obj;
        return sizeof(SleepyObjString) + s->length + 1;
    }
    case SLEEPY_OBJ_LONG: return sizeof(SleepyObjLong);
    case SLEEPY_OBJ_ARRAY: return sizeof(SleepyObjArray);
    case SLEEPY_OBJ_HASH: return sizeof(SleepyObjHash);
    case SLEEPY_OBJ_FUNCTION: return sizeof(SleepyObjFunction);
    case SLEEPY_OBJ_CLOSURE: return sizeof(SleepyObjClosure);
    case SLEEPY_OBJ_UPVALUE: return sizeof(SleepyObjUpvalue);
    case SLEEPY_OBJ_CONTINUATION: return sizeof(SleepyObjContinuation);
    case SLEEPY_OBJ_NATIVE: return sizeof(SleepyObjNative);
    case SLEEPY_OBJ_BRIDGE: return sizeof(SleepyObjBridge);
    }
    return sizeof(SleepyObj);
}

void sleepy_gc_collect(SleepyVM *vm) {
    mark_roots(vm);
    trace_references(vm);
    sleepy_gc_sweep(vm);
    vm->next_gc_threshold = vm->bytes_allocated * 3 / 2;
    if (vm->next_gc_threshold < 256 * 1024)
        vm->next_gc_threshold = 256 * 1024;
}

void sleepy_gc_mark_obj(SleepyVM *vm, SleepyObj *obj) {
    if (!obj || obj->is_marked) return;
    obj->is_marked = true;
    gc_gray_stack_push(vm, obj);
}

void sleepy_gc_mark_value(SleepyVM *vm, SleepyValue value) {
    if (SLEEPY_IS_OBJ(value))
        sleepy_gc_mark_obj(vm, SLEEPY_AS_OBJ(value));
}

SleepyObj *sleepy_gc_allocate_object(SleepyVM *vm, size_t size, SleepyObjType type) {
    (void)type;
    vm->bytes_allocated += size;
    if (vm->bytes_allocated > vm->next_gc_threshold)
        sleepy_gc_collect(vm);
    return NULL;
}

void sleepy_gc_free(SleepyVM *vm) {
    SleepyObj *obj = vm->objects;
    while (obj) {
        SleepyObj *next = obj->next;
        SLEEPY_FREE(vm->allocator, obj);
        obj = next;
    }
    if (vm->gc_gray_stack)
        SLEEPY_FREE(vm->allocator, vm->gc_gray_stack);
    vm->objects = NULL;
    vm->gc_gray_stack = NULL;
    vm->gc_gray_count = 0;
    vm->gc_gray_capacity = 0;
}
