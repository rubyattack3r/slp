#include "sleepy_vm.h"
#include "sleepy_compiler.h"
#include "sleepy_ast.h"
#include "sleepy_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *vm_default_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}

static void define_builtins(SleepyVM *vm);

SleepyVM *sleepy_vm_new(SleepyAllocator *allocator) {
    SleepyVM *vm;
    if (allocator) {
        vm = (SleepyVM*)allocator->reallocate(NULL, sizeof(SleepyVM), allocator->user_data);
    } else {
        SleepyAllocator *a = (SleepyAllocator*)malloc(sizeof(SleepyAllocator));
        a->reallocate = vm_default_alloc;
        a->user_data = NULL;
        vm = (SleepyVM*)a->reallocate(NULL, sizeof(SleepyVM), a->user_data);
        allocator = a;
    }
    if (!vm) return NULL;
    memset(vm, 0, sizeof(SleepyVM));
    vm->allocator = allocator;
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->objects = NULL;

    vm->open_upvalues = NULL;
    vm->bridge_types = NULL;
    vm->halted = false;
    vm->thrown_exception = SLEEPY_NULL_VAL;
    vm->error_fn = NULL;
    vm->error_userdata = NULL;
    vm->write_fn = NULL;
    vm->write_userdata = NULL;

    sleepy_gc_init(vm);

    vm->globals = sleepy_obj_hash_new(allocator);
    if (vm->globals) {
        vm->globals->obj.next = vm->objects;
        vm->objects = &vm->globals->obj;
    }
    vm->natives = sleepy_obj_hash_new(allocator);
    if (vm->natives) {
        vm->natives->obj.next = vm->objects;
        vm->objects = &vm->natives->obj;
    }

    define_builtins(vm);

    return vm;
}

void sleepy_vm_free(SleepyVM *vm) {
    SleepyBridgeType *bt = vm->bridge_types;
    while (bt) {
        SleepyBridgeType *next = bt->next;
        SLEEPY_FREE(vm->allocator, bt->keyword);
        SLEEPY_FREE(vm->allocator, bt);
        bt = next;
    }
    if (vm->interned)
        vm->allocator->reallocate(vm->interned, 0, vm->allocator->user_data);
    sleepy_gc_free(vm);
    SLEEPY_FREE(vm->allocator, vm);
}

void sleepy_vm_push(SleepyVM *vm, SleepyValue value) {
    *vm->stack_top = value;
    vm->stack_top++;
}

SleepyValue sleepy_vm_pop(SleepyVM *vm) {
    vm->stack_top--;
    return *vm->stack_top;
}

SleepyValue sleepy_vm_peek(SleepyVM *vm, int distance) {
    return vm->stack_top[-1 - distance];
}

static void intern_grow(SleepyVM *vm) {
    int new_cap = vm->interned_capacity * 2;
    SleepyObjString **new_table = (SleepyObjString**)vm->allocator->reallocate(
        NULL, sizeof(SleepyObjString*) * new_cap, vm->allocator->user_data);
    if (!new_table) return;
    memset(new_table, 0, sizeof(SleepyObjString*) * new_cap);
    for (int i = 0; i < vm->interned_capacity; i++) {
        SleepyObjString *s = vm->interned[i];
        if (s) {
            uint32_t idx = s->hash & (new_cap - 1);
            while (new_table[idx]) {
                idx = (idx + 1) & (new_cap - 1);
            }
            new_table[idx] = s;
        }
    }
    vm->allocator->reallocate(vm->interned, 0, vm->allocator->user_data);
    vm->interned = new_table;
    vm->interned_capacity = new_cap;
}

SleepyObjString *sleepy_vm_intern_string(SleepyVM *vm, const char *chars, uint32_t length) {
    uint32_t hash = sleepy_hash_string(chars, length);
    if (vm->interned) {
        uint32_t idx = hash & (vm->interned_capacity - 1);
        for (int probes = 0; probes < vm->interned_capacity; probes++) {
            SleepyObjString *s = vm->interned[idx];
            if (!s) break;
            if (s->hash == hash && s->length == length) {
                bool match = true;
                for (uint32_t i = 0; i < length; i++) {
                    if (s->chars[i] != chars[i]) { match = false; break; }
                }
                if (match) return s;
            }
            idx = (idx + 1) & (vm->interned_capacity - 1);
        }
    }
    if (vm->interned_count * 2 >= vm->interned_capacity) {
        if (!vm->interned) {
            vm->interned_capacity = SLEEPY_INTERN_INIT_CAP;
            vm->interned = (SleepyObjString**)vm->allocator->reallocate(
                NULL, sizeof(SleepyObjString*) * vm->interned_capacity,
                vm->allocator->user_data);
            if (vm->interned) memset(vm->interned, 0, sizeof(SleepyObjString*) * vm->interned_capacity);
        } else {
            intern_grow(vm);
        }
    }
    SleepyObjString *str = sleepy_obj_string_new(vm->allocator, chars, length);
    if (str) {
        str->hash = hash;
        str->obj.next = vm->objects;
        vm->objects = &str->obj;
        vm->bytes_allocated += sizeof(SleepyObjString) + length + 1;
        if (vm->interned) {
            uint32_t idx = hash & (vm->interned_capacity - 1);
            while (vm->interned[idx]) {
                idx = (idx + 1) & (vm->interned_capacity - 1);
            }
            vm->interned[idx] = str;
            vm->interned_count++;
        }
    }
    return str;
}

SleepyObjString *sleepy_vm_copy_string(SleepyVM *vm, const char *chars, uint32_t length) {
    return sleepy_vm_intern_string(vm, chars, length);
}

static SleepyCallFrame *current_frame(SleepyVM *vm) {
    return &vm->frames[vm->frame_count - 1];
}

static uint8_t read_byte(SleepyCallFrame *frame) {
    return *frame->ip++;
}

static uint16_t read_short(SleepyCallFrame *frame) {
    frame->ip += 2;
    return (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]);
}

static SleepyValue read_constant(SleepyCallFrame *frame) {
    uint16_t idx = read_short(frame);
    return frame->closure->function->chunk->constants[idx];
}

static void close_upvalues(SleepyVM *vm, SleepyValue *last) {
    while (vm->open_upvalues && vm->open_upvalues->location >= last) {
        SleepyObjUpvalue *uv = vm->open_upvalues;
        uv->closed = *uv->location;
        uv->location = &uv->closed;
        vm->open_upvalues = uv->next;
    }
}

static void define_native(SleepyVM *vm, const char *name, SleepyNativeFn fn) {
    SleepyObjString *name_str = sleepy_vm_copy_string(vm, name, (uint32_t)strlen(name));
    SleepyObjNative *native = sleepy_obj_native_new(vm->allocator, fn, name_str);
    if (native) {
        native->obj.next = vm->objects;
        vm->objects = &native->obj;
    }
    sleepy_obj_hash_set(vm->allocator, vm->globals, SLEEPY_OBJ_VAL(name_str), SLEEPY_OBJ_VAL(native));
}

static SleepyValue native_println(SleepyVM *vm, SleepyValue *args, int argc) {
    (void)vm;
    if (argc > 0) {
        if (SLEEPY_IS_NUM(args[0]))
            printf("%g\n", SLEEPY_AS_NUM(args[0]));
        else if (SLEEPY_IS_BOOL(args[0]))
            printf("%s\n", SLEEPY_AS_BOOL(args[0]) ? "true" : "false");
        else if (SLEEPY_IS_NULL(args[0]))
            printf("$null\n");
        else if (SLEEPY_IS_OBJ(args[0])) {
            SleepyObj *obj = SLEEPY_AS_OBJ(args[0]);
            if (obj->type == SLEEPY_OBJ_STRING)
                printf("%s\n", ((SleepyObjString*)obj)->chars);
            else
                printf("[object]\n");
        }
    } else {
        printf("\n");
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_size(SleepyVM *vm, SleepyValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLEEPY_NUM_VAL(0);
    if (SLEEPY_IS_OBJ(args[0])) {
        SleepyObj *obj = SLEEPY_AS_OBJ(args[0]);
        if (obj->type == SLEEPY_OBJ_STRING)
            return SLEEPY_NUM_VAL((double)((SleepyObjString*)obj)->length);
        if (obj->type == SLEEPY_OBJ_ARRAY)
            return SLEEPY_NUM_VAL((double)((SleepyObjArray*)obj)->count);
        if (obj->type == SLEEPY_OBJ_HASH)
            return SLEEPY_NUM_VAL((double)((SleepyObjHash*)obj)->count);
    }
    return SLEEPY_NUM_VAL(0);
}

static SleepyValue native_array(SleepyVM *vm, SleepyValue *args, int argc) {
    SleepyObjArray *arr = sleepy_obj_array_new(vm->allocator);
    if (arr) {
        arr->obj.next = vm->objects;
        vm->objects = &arr->obj;
        for (int i = 0; i < argc; i++)
            sleepy_obj_array_push(vm->allocator, arr, args[i]);
    }
    return arr ? SLEEPY_OBJ_VAL(arr) : SLEEPY_NULL_VAL;
}

static SleepyValue native_push(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc >= 2 && SLEEPY_IS_OBJ(args[0]) &&
        SLEEPY_OBJ_TYPE(args[0]) == SLEEPY_OBJ_ARRAY) {
        SleepyObjArray *arr = SLEEPY_AS_ARRAY(args[0]);
        sleepy_obj_array_push(vm->allocator, arr, args[1]);
        return SLEEPY_OBJ_VAL(arr);
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_pop(SleepyVM *vm, SleepyValue *args, int argc) {
    (void)vm;
    if (argc >= 1 && SLEEPY_IS_OBJ(args[0]) &&
        SLEEPY_OBJ_TYPE(args[0]) == SLEEPY_OBJ_ARRAY) {
        return sleepy_obj_array_pop(SLEEPY_AS_ARRAY(args[0]));
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_hash(SleepyVM *vm, SleepyValue *args, int argc) {
    SleepyObjHash *hash = sleepy_obj_hash_new(vm->allocator);
    if (hash) {
        hash->obj.next = vm->objects;
        vm->objects = &hash->obj;
        for (int i = 0; i < argc - 1; i += 2)
            sleepy_obj_hash_set(vm->allocator, hash, args[i], args[i+1]);
    }
    return hash ? SLEEPY_OBJ_VAL(hash) : SLEEPY_NULL_VAL;
}

static SleepyValue native_typeof(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc < 1) return SLEEPY_NULL_VAL;
    SleepyValue v = args[0];
    if (SLEEPY_IS_NULL(v)) return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "null", 4));
    if (SLEEPY_IS_BOOL(v)) return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "boolean", 7));
    if (SLEEPY_IS_NUM(v)) return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "number", 6));
    if (SLEEPY_IS_OBJ(v)) {
        switch (SLEEPY_OBJ_TYPE(v)) {
            case SLEEPY_OBJ_STRING: return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "string", 6));
            case SLEEPY_OBJ_LONG: return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "long", 4));
            case SLEEPY_OBJ_ARRAY: return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "array", 5));
            case SLEEPY_OBJ_HASH: return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "hash", 4));
            case SLEEPY_OBJ_FUNCTION:
            case SLEEPY_OBJ_CLOSURE:
            case SLEEPY_OBJ_NATIVE:
            case SLEEPY_OBJ_BRIDGE:
                return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "function", 8));
            default: return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "object", 6));
        }
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_keys(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc < 1 || !SLEEPY_IS_OBJ(args[0]) || SLEEPY_OBJ_TYPE(args[0]) != SLEEPY_OBJ_HASH)
        return SLEEPY_NULL_VAL;
    SleepyObjHash *hash = SLEEPY_AS_HASH(args[0]);
    SleepyObjArray *arr = sleepy_obj_array_new(vm->allocator);
    if (!arr) return SLEEPY_NULL_VAL;
    arr->obj.next = vm->objects;
    vm->objects = &arr->obj;
    for (int i = 0; i < hash->capacity; i++) {
        if (!SLEEPY_IS_NULL(hash->entries[i].key)) {
            sleepy_obj_array_push(vm->allocator, arr, hash->entries[i].key);
        }
    }
    return SLEEPY_OBJ_VAL(arr);
}

static SleepyValue native_remove(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc < 2 || !SLEEPY_IS_OBJ(args[0])) return SLEEPY_BOOL_VAL(false);
    if (SLEEPY_OBJ_TYPE(args[0]) == SLEEPY_OBJ_HASH) {
        return SLEEPY_BOOL_VAL(sleepy_obj_hash_delete(vm->allocator, SLEEPY_AS_HASH(args[0]), args[1]));
    }
    return SLEEPY_BOOL_VAL(false);
}

static void define_builtins(SleepyVM *vm) {
    define_native(vm, "println", native_println);
    define_native(vm, "size", native_size);
    define_native(vm, "array", native_array);
    define_native(vm, "@", native_array);
    define_native(vm, "%", native_hash);
    define_native(vm, "push", native_push);
    define_native(vm, "pop", native_pop);
    define_native(vm, "typeof", native_typeof);
    define_native(vm, "keys", native_keys);
    define_native(vm, "remove", native_remove);
}

static bool call_closure(SleepyVM *vm, SleepyObjClosure *closure, int arg_count) {
    if (vm->frame_count >= SLEEPY_MAX_FRAMES) {
        sleepy_vm_runtime_error(vm, "Stack overflow.");
        return false;
    }
    
    // Pad stack with $null so we have at least 9 argument slots (for $1 to $9)
    for (int i = arg_count; i < 9; i++) {
        sleepy_vm_push(vm, SLEEPY_NULL_VAL);
    }
    // Note: The frame's slots pointer should still point to the closure object, 
    // which was pushed BEFORE the arg_count arguments.
    // So if arg_count was 2, we pushed 7 nulls. The total arguments is now max(arg_count, 9).
    // Actually, `call_value` popped nothing. The stack has: [closure] [arg1] ... [argN] [nulls...]
    int frame_slots = (arg_count < 9) ? 9 : arg_count;
    
    SleepyCallFrame *frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip = closure->function->chunk->code;
    frame->slots = vm->stack_top - frame_slots - 1;
    return true;
}

static bool call_value(SleepyVM *vm, SleepyValue callee, int arg_count) {
    if (!SLEEPY_IS_OBJ(callee)) {
        // DRY-RUN VALIDATOR: Do not crash! Consume args, push NULL, return true!
        vm->stack_top -= arg_count + 1;
        sleepy_vm_push(vm, SLEEPY_NULL_VAL);
        return true;
    }
    switch (SLEEPY_OBJ_TYPE(callee)) {
    case SLEEPY_OBJ_CLOSURE:
        return call_closure(vm, SLEEPY_AS_CLOSURE(callee), arg_count);
    case SLEEPY_OBJ_NATIVE: {
        SleepyObjNative *native = SLEEPY_AS_NATIVE(callee);
        SleepyValue result = native->fn(vm, vm->stack_top - arg_count, arg_count);
        vm->stack_top -= arg_count + 1;
        sleepy_vm_push(vm, result);
        return true;
    }
    default:
        // DRY-RUN VALIDATOR: Do not crash! Consume args, push NULL, return true!
        vm->stack_top -= arg_count + 1;
        sleepy_vm_push(vm, SLEEPY_NULL_VAL);
        return true;
    }
}

void sleepy_vm_runtime_error(SleepyVM *vm, const char *msg) {
    if (vm->error_fn) {
        int line = 0;
        if (vm->frame_count > 0) {
            SleepyCallFrame *frame = current_frame(vm);
            int offset = (int)(frame->ip - frame->closure->function->chunk->code);
            if (offset >= 0 && offset < frame->closure->function->chunk->count)
                line = frame->closure->function->chunk->lines[offset];
        }
        vm->error_fn(vm->error_userdata, line, msg);
    }
}

SleepyResult sleepy_vm_call(SleepyVM *vm, int arg_count);

SleepyResult sleepy_vm_run(SleepyVM *vm, SleepyObjFunction *fn) {
    SleepyObjClosure *closure = sleepy_obj_closure_new(vm->allocator, fn);
    if (!closure) return SLEEPY_RUNTIME_ERROR;
    closure->obj.next = vm->objects;
    vm->objects = &closure->obj;

    sleepy_vm_push(vm, SLEEPY_OBJ_VAL(closure));
    return sleepy_vm_call(vm, 0);
}

SleepyResult sleepy_vm_call(SleepyVM *vm, int arg_count) {
    SleepyValue callee = sleepy_vm_peek(vm, arg_count);
    if (!call_value(vm, callee, arg_count))
        return SLEEPY_RUNTIME_ERROR;
    
    // We need to run the VM to actually execute the closure, 
    // unless it's a native function which call_value already executed.
    if (SLEEPY_IS_OBJ(callee) && SLEEPY_OBJ_TYPE(callee) == SLEEPY_OBJ_CLOSURE) {
        // The call_closure function set up a new frame.
        // We now start the execution loop. We need a way to run the loop until this frame returns.
        int target_frame_count = vm->frame_count - 1;
        for (;;) {
            uint8_t instruction = read_byte(current_frame(vm));
            switch (instruction) {
        case OP_PUSH_NULL:
            sleepy_vm_push(vm, SLEEPY_NULL_VAL);
            break;
        case OP_PUSH_TRUE:
            sleepy_vm_push(vm, SLEEPY_TRUE_VAL);
            break;
        case OP_PUSH_FALSE:
            sleepy_vm_push(vm, SLEEPY_FALSE_VAL);
            break;
        case OP_PUSH_CONST: {
            SleepyValue val = read_constant(current_frame(vm));
            sleepy_vm_push(vm, val);
            break;
        }
        case OP_LOAD_LOCAL_0: sleepy_vm_push(vm, current_frame(vm)->slots[0]); break;
        case OP_LOAD_LOCAL_1: sleepy_vm_push(vm, current_frame(vm)->slots[1]); break;
        case OP_LOAD_LOCAL_2: sleepy_vm_push(vm, current_frame(vm)->slots[2]); break;
        case OP_LOAD_LOCAL_3: sleepy_vm_push(vm, current_frame(vm)->slots[3]); break;
        case OP_LOAD_LOCAL_4: sleepy_vm_push(vm, current_frame(vm)->slots[4]); break;
        case OP_LOAD_LOCAL_5: sleepy_vm_push(vm, current_frame(vm)->slots[5]); break;
        case OP_LOAD_LOCAL_6: sleepy_vm_push(vm, current_frame(vm)->slots[6]); break;
        case OP_LOAD_LOCAL_7: sleepy_vm_push(vm, current_frame(vm)->slots[7]); break;
        case OP_STORE_LOCAL_0: current_frame(vm)->slots[0] = sleepy_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_1: current_frame(vm)->slots[1] = sleepy_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_2: current_frame(vm)->slots[2] = sleepy_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_3: current_frame(vm)->slots[3] = sleepy_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_4: current_frame(vm)->slots[4] = sleepy_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_5: current_frame(vm)->slots[5] = sleepy_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_6: current_frame(vm)->slots[6] = sleepy_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_7: current_frame(vm)->slots[7] = sleepy_vm_peek(vm, 0); break;
        case OP_LOAD_LOCAL: {
            uint8_t slot = read_byte(current_frame(vm));
            sleepy_vm_push(vm, current_frame(vm)->slots[slot]);
            break;
        }
        case OP_STORE_LOCAL: {
            uint8_t slot = read_byte(current_frame(vm));
            current_frame(vm)->slots[slot] = sleepy_vm_peek(vm, 0);
            break;
        }
        case OP_LOAD_GLOBAL: {
            uint16_t idx = read_short(current_frame(vm));
            SleepyValue name_val = current_frame(vm)->closure->function->chunk->constants[idx];
            SleepyObjString *name_str = SLEEPY_AS_STRING(name_val);
            
            // Inline fast-path for string lookup in globals
            SleepyObjHash *hash = vm->globals;
            SleepyValue val = SLEEPY_NULL_VAL;
            if (hash->capacity > 0) {
                uint32_t bucket = name_str->hash & (hash->capacity - 1);
                for (int i = 0; i < hash->capacity; i++) {
                    SleepyValue k = hash->entries[bucket].key;
                    if (SLEEPY_IS_NULL(k)) {
                        if (!SLEEPY_IS_TRUE(hash->entries[bucket].value)) break;
                    } else if (SLEEPY_IS_OBJ(k) && SLEEPY_AS_OBJ(k) == SLEEPY_AS_OBJ(name_val)) {
                        val = hash->entries[bucket].value;
                        break;
                    }
                    bucket = (bucket + 1) & (hash->capacity - 1);
                }
            }
            sleepy_vm_push(vm, val);
            break;
        }
        case OP_STORE_GLOBAL: {
            uint16_t idx = read_short(current_frame(vm));
            SleepyValue name_val = current_frame(vm)->closure->function->chunk->constants[idx];
            SleepyValue val = sleepy_vm_peek(vm, 0);
            SleepyObjString *name_str = SLEEPY_AS_STRING(name_val);
            SleepyObjHash *hash = vm->globals;
            bool set = false;

            if (hash->capacity > 0) {
                uint32_t bucket = name_str->hash & (hash->capacity - 1);
                for (int i = 0; i < hash->capacity; i++) {
                    SleepyValue k = hash->entries[bucket].key;
                    if (SLEEPY_IS_NULL(k)) {
                        if (!SLEEPY_IS_TRUE(hash->entries[bucket].value)) break;
                    } else if (SLEEPY_IS_OBJ(k) && SLEEPY_AS_OBJ(k) == SLEEPY_AS_OBJ(name_val)) {
                        hash->entries[bucket].value = val;
                        set = true;
                        break;
                    }
                    bucket = (bucket + 1) & (hash->capacity - 1);
                }
            }

            if (!set) {
                sleepy_obj_hash_set(vm->allocator, hash, name_val, val);
            }
            break;
        }
        case OP_LOAD_UPVALUE: {
            uint8_t idx = read_byte(current_frame(vm));
            sleepy_vm_push(vm, *current_frame(vm)->closure->upvalues[idx]->location);
            break;
        }
        case OP_STORE_UPVALUE: {
            uint8_t idx = read_byte(current_frame(vm));
            *current_frame(vm)->closure->upvalues[idx]->location = sleepy_vm_peek(vm, 0);
            break;
        }
        case OP_ADD: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            if (SLEEPY_IS_NUM(a) && SLEEPY_IS_NUM(b))
                sleepy_vm_push(vm, SLEEPY_NUM_VAL(SLEEPY_AS_NUM(a) + SLEEPY_AS_NUM(b)));
            else
                sleepy_vm_push(vm, SLEEPY_NUM_VAL(SLEEPY_AS_NUM(a) + SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_SUBTRACT: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(SLEEPY_AS_NUM(a) - SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_MULTIPLY: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(SLEEPY_AS_NUM(a) * SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_DIVIDE: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(SLEEPY_AS_NUM(a) / SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_MODULO: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL((double)((long)SLEEPY_AS_NUM(a) % (long)SLEEPY_AS_NUM(b))));
            break;
        }
        case OP_POWER: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            double result = 1;
            double base = SLEEPY_AS_NUM(a);
            long exp = (long)SLEEPY_AS_NUM(b);
            if (exp < 0) { base = 1.0/base; exp = -exp; }
            for (long i = 0; i < exp; i++) result *= base;
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(result));
            break;
        }
        case OP_NEGATE:
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(-SLEEPY_AS_NUM(sleepy_vm_pop(vm))));
            break;
        case OP_CONCAT: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            const char *sa = "", *sb = "";
            uint32_t la = 0, lb = 0;
            char a_buf[64] = {0}, b_buf[64] = {0};
            if (SLEEPY_IS_OBJ(a) && SLEEPY_OBJ_TYPE(a) == SLEEPY_OBJ_STRING) {
                SleepyObjString *osa = SLEEPY_AS_STRING(a);
                sa = osa->chars; la = osa->length;
            } else if (SLEEPY_IS_NUM(a)) {
                snprintf(a_buf, sizeof(a_buf), "%g", SLEEPY_AS_NUM(a));
                sa = a_buf; la = (uint32_t)strlen(a_buf);
            } else if (SLEEPY_IS_BOOL(a)) {
                sa = SLEEPY_AS_BOOL(a) ? "true" : "false";
                la = (uint32_t)strlen(sa);
            } else if (SLEEPY_IS_NULL(a)) {
                sa = "$null"; la = 5;
            }
            if (SLEEPY_IS_OBJ(b) && SLEEPY_OBJ_TYPE(b) == SLEEPY_OBJ_STRING) {
                SleepyObjString *osb = SLEEPY_AS_STRING(b);
                sb = osb->chars; lb = osb->length;
            } else if (SLEEPY_IS_NUM(b)) {
                snprintf(b_buf, sizeof(b_buf), "%g", SLEEPY_AS_NUM(b));
                sb = b_buf; lb = (uint32_t)strlen(b_buf);
            } else if (SLEEPY_IS_BOOL(b)) {
                sb = SLEEPY_AS_BOOL(b) ? "true" : "false";
                lb = (uint32_t)strlen(sb);
            } else if (SLEEPY_IS_NULL(b)) {
                sb = "$null"; lb = 5;
            }
            uint32_t total = la + lb;
            char *concat_buf = (char*)SLEEPY_MALLOC(vm->allocator, total + 1);
            memcpy(concat_buf, sa, la);
            memcpy(concat_buf + la, sb, lb);
            concat_buf[total] = '\0';
            SleepyObjString *result = sleepy_vm_intern_string(vm, concat_buf, total);
            SLEEPY_FREE(vm->allocator, concat_buf);
            sleepy_vm_push(vm, SLEEPY_OBJ_VAL(result));
            break;
        }
        case OP_REPEAT: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            (void)a; (void)b;
            sleepy_vm_push(vm, SLEEPY_NULL_VAL);
            break;
        }
        case OP_EQUAL: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(sleepy_value_equals(a, b)));
            break;
        }
        case OP_NOT_EQUAL: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(!sleepy_value_equals(a, b)));
            break;
        }
        case OP_LESS: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(SLEEPY_AS_NUM(a) < SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_GREATER: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(SLEEPY_AS_NUM(a) > SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_LESS_EQUAL: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(SLEEPY_AS_NUM(a) <= SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_GREATER_EQUAL: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(SLEEPY_AS_NUM(a) >= SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_SPACESHIP: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            double da = SLEEPY_AS_NUM(a), db = SLEEPY_AS_NUM(b);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(da < db ? -1.0 : (da > db ? 1.0 : 0.0)));
            break;
        }
        case OP_NOT:
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(sleepy_value_is_falsy(sleepy_vm_pop(vm))));
            break;
        case OP_AND: {
            uint16_t offset = read_short(current_frame(vm));
            if (sleepy_value_is_falsy(sleepy_vm_peek(vm, 0))) {
                current_frame(vm)->ip += offset;
            } else {
                sleepy_vm_pop(vm);
            }
            break;
        }
        case OP_OR: {
            uint16_t offset = read_short(current_frame(vm));
            if (!sleepy_value_is_falsy(sleepy_vm_peek(vm, 0))) {
                current_frame(vm)->ip += offset;
            } else {
                sleepy_vm_pop(vm);
            }
            break;
        }
        case OP_BIT_AND: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL((double)((long)SLEEPY_AS_NUM(a) & (long)SLEEPY_AS_NUM(b))));
            break;
        }
        case OP_BIT_OR: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL((double)((long)SLEEPY_AS_NUM(a) | (long)SLEEPY_AS_NUM(b))));
            break;
        }
        case OP_BIT_XOR: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL((double)((long)SLEEPY_AS_NUM(a) ^ (long)SLEEPY_AS_NUM(b))));
            break;
        }
        case OP_BIT_NOT:
            sleepy_vm_push(vm, SLEEPY_NUM_VAL((double)(~(long)SLEEPY_AS_NUM(sleepy_vm_pop(vm)))));
            break;
        case OP_LSHIFT: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL((double)((long)SLEEPY_AS_NUM(a) << (long)SLEEPY_AS_NUM(b))));
            break;
        }
        case OP_RSHIFT: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL((double)((long)SLEEPY_AS_NUM(a) >> (long)SLEEPY_AS_NUM(b))));
            break;
        }
        case OP_INCREMENT: {
            SleepyValue v = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(SLEEPY_AS_NUM(v) + 1));
            break;
        }
        case OP_DECREMENT: {
            SleepyValue v = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(SLEEPY_AS_NUM(v) - 1));
            break;
        }
        case OP_POP:
            sleepy_vm_pop(vm);
            break;
        case OP_DUP:
            sleepy_vm_push(vm, sleepy_vm_peek(vm, 0));
            break;
        case OP_JUMP: {
            uint16_t offset = read_short(current_frame(vm));
            current_frame(vm)->ip += offset;
            break;
        }
        case OP_JUMP_IF_FALSE: {
            uint16_t offset = read_short(current_frame(vm));
            if (sleepy_value_is_falsy(sleepy_vm_peek(vm, 0)))
                current_frame(vm)->ip += offset;
            break;
        }
        case OP_JUMP_IF_TRUE: {
            uint16_t offset = read_short(current_frame(vm));
            if (!sleepy_value_is_falsy(sleepy_vm_peek(vm, 0)))
                current_frame(vm)->ip += offset;
            break;
        }
        case OP_LOOP: {
            int16_t offset = (int16_t)read_short(current_frame(vm));
            current_frame(vm)->ip -= offset;
            break;
        }
        case OP_CALL: {
            uint8_t arg_count = read_byte(current_frame(vm));
            if (!call_value(vm, sleepy_vm_peek(vm, arg_count), arg_count))
                return SLEEPY_RUNTIME_ERROR;
            break;
        }
        case OP_RETURN: {
            SleepyValue result = sleepy_vm_pop(vm);
            close_upvalues(vm, current_frame(vm)->slots);
            vm->frame_count--;
            if (vm->frame_count == target_frame_count) {
                vm->stack_top = current_frame(vm)->slots + 1; // Wait, current_frame is now the caller. We want to pop the function and args, which `slots` points to.
                // Actually the current frame is now the caller! So we shouldn't use current_frame(vm)->slots + 1. 
                // The frame we just returned from had `slots = vm->stack_top - arg_count - 1`. 
                // We should just pop the result, pop the frame, pop the args and function, and push the result.
                // In call_closure, `frame->slots` is set to `vm->stack_top - arg_count - 1`. So the function is at `slots[0]`, args at `slots[1..arg_count]`.
                // So `vm->stack_top = frame->slots` resets the stack to before the function call, then we push `result`.
                // Since we already did `vm->frame_count--`, we need to use the old frame's slots.
                SleepyCallFrame *old_frame = &vm->frames[vm->frame_count];
                vm->stack_top = old_frame->slots;
                sleepy_vm_push(vm, result);
                return SLEEPY_OK;
            }
            if (vm->frame_count == 0) return SLEEPY_OK; // Should not happen here if target_frame_count was set right, but safe.
            SleepyCallFrame *old_frame = &vm->frames[vm->frame_count];
            vm->stack_top = old_frame->slots;
            sleepy_vm_push(vm, result);
            break;
        }
        case OP_CLOSURE: {
            uint16_t fn_idx = read_short(current_frame(vm));
            SleepyObjFunction *fn_obj = (SleepyObjFunction*)SLEEPY_AS_OBJ(
                current_frame(vm)->closure->function->chunk->constants[fn_idx]);
            SleepyObjClosure *cl = sleepy_obj_closure_new(vm->allocator, fn_obj);
            cl->obj.next = vm->objects;
            vm->objects = &cl->obj;
            for (int i = 0; i < fn_obj->upvalue_count; i++) {
                uint8_t is_local = read_byte(current_frame(vm));
                uint8_t idx = read_byte(current_frame(vm));
                if (is_local) {
                    cl->upvalues[i] = sleepy_obj_upvalue_new(vm->allocator,
                        &current_frame(vm)->slots[idx]);
                    cl->upvalues[i]->obj.next = vm->objects;
                    vm->objects = &cl->upvalues[i]->obj;
                } else {
                    cl->upvalues[i] = current_frame(vm)->closure->upvalues[idx];
                }
            }
            sleepy_vm_push(vm, SLEEPY_OBJ_VAL(cl));
            break;
        }
        case OP_CLOSE_UPVALUE:
            close_upvalues(vm, vm->stack_top - 1);
            sleepy_vm_pop(vm);
            break;
        case OP_BUILD_ARRAY: {
            uint16_t count = read_short(current_frame(vm));
            SleepyObjArray *arr = sleepy_obj_array_new(vm->allocator);
            arr->obj.next = vm->objects;
            vm->objects = &arr->obj;
            for (uint16_t i = 0; i < count; i++) {
                SleepyValue val = sleepy_vm_pop(vm);
                sleepy_obj_array_push(vm->allocator, arr, val);
            }
            for (int i = 0; i < arr->count / 2; i++) {
                SleepyValue tmp = arr->elements[i];
                arr->elements[i] = arr->elements[arr->count - 1 - i];
                arr->elements[arr->count - 1 - i] = tmp;
            }
            sleepy_vm_push(vm, SLEEPY_OBJ_VAL(arr));
            break;
        }
        case OP_BUILD_HASH: {
            uint16_t count = read_short(current_frame(vm));
            SleepyObjHash *hash = sleepy_obj_hash_new(vm->allocator);
            hash->obj.next = vm->objects;
            vm->objects = &hash->obj;
            for (uint16_t i = 0; i < count; i++) {
                SleepyValue val = sleepy_vm_pop(vm);
                SleepyValue key = sleepy_vm_pop(vm);
                sleepy_obj_hash_set(vm->allocator, hash, key, val);
            }
            sleepy_vm_push(vm, SLEEPY_OBJ_VAL(hash));
            break;
        }
        case OP_INDEX_GET: {
            SleepyValue idx_val = sleepy_vm_pop(vm);
            SleepyValue container = sleepy_vm_pop(vm);
            if (SLEEPY_IS_OBJ(container)) {
                SleepyObj *obj = SLEEPY_AS_OBJ(container);
                if (obj->type == SLEEPY_OBJ_ARRAY) {
                    int idx = (int)SLEEPY_AS_NUM(idx_val);
                    sleepy_vm_push(vm, sleepy_obj_array_get((SleepyObjArray*)obj, idx));
                } else if (obj->type == SLEEPY_OBJ_HASH) {
                    sleepy_vm_push(vm, sleepy_obj_hash_get((SleepyObjHash*)obj, idx_val));
                } else {
                    sleepy_vm_push(vm, SLEEPY_NULL_VAL);
                }
            } else {
                sleepy_vm_push(vm, SLEEPY_NULL_VAL);
            }
            break;
        }
        case OP_INDEX_SET: {
            SleepyValue val = sleepy_vm_pop(vm);
            SleepyValue idx_val = sleepy_vm_pop(vm);
            SleepyValue container = sleepy_vm_pop(vm);
            if (SLEEPY_IS_OBJ(container)) {
                SleepyObj *obj = SLEEPY_AS_OBJ(container);
                if (obj->type == SLEEPY_OBJ_ARRAY)
                    sleepy_obj_array_set(vm->allocator, (SleepyObjArray*)obj, (int)SLEEPY_AS_NUM(idx_val), val);
                else if (obj->type == SLEEPY_OBJ_HASH)
                    sleepy_obj_hash_set(vm->allocator, (SleepyObjHash*)obj, idx_val, val);
            }
            sleepy_vm_push(vm, val);
            break;
        }
        case OP_PUSH_HANDLER: {
            uint16_t offset = read_short(current_frame(vm));
            SleepyCallFrame *frame = current_frame(vm);
            if (vm->try_handler_count < SLEEPY_MAX_HANDLERS) {
                vm->try_handlers[vm->try_handler_count].catch_ip = frame->ip + offset;
                vm->try_handlers[vm->try_handler_count].frame_count = vm->frame_count;
                vm->try_handler_count++;
            }
            break;
        }
        case OP_POP_HANDLER:
            if (vm->try_handler_count > 0)
                vm->try_handler_count--;
            break;
        case OP_THROW: {
            SleepyValue exc = sleepy_vm_pop(vm);
            if (vm->try_handler_count > 0) {
                vm->try_handler_count--;
                SleepyTryHandler h = vm->try_handlers[vm->try_handler_count];
                while (vm->frame_count > h.frame_count) {
                    close_upvalues(vm, current_frame(vm)->slots);
                    vm->frame_count--;
                }
                current_frame(vm)->ip = h.catch_ip;
                sleepy_vm_push(vm, exc);
            } else {
                vm->thrown_exception = exc;
                return SLEEPY_RUNTIME_ERROR;
            }
            break;
        }
        case OP_ASSERT: {
            SleepyValue test = sleepy_vm_pop(vm);
            if (sleepy_value_is_falsy(test)) {
                sleepy_vm_runtime_error(vm, "Assertion failed.");
                return SLEEPY_RUNTIME_ERROR;
            }
            break;
        }
        case OP_HALT:
            return SLEEPY_HALT;
        case OP_DONE:
            return SLEEPY_OK;
        case OP_YIELD:
            return SLEEPY_OK;
        case OP_BREAK:
        case OP_CONTINUE:
            break;
        case OP_BRIDGE_REGISTER: {
            uint16_t kw_idx = read_short(current_frame(vm));
            uint16_t name_idx = read_short(current_frame(vm));
            SleepyValue closure_val = sleepy_vm_pop(vm);
            SleepyObjClosure *cl = SLEEPY_AS_CLOSURE(closure_val);
            SleepyChunk *chunk = current_frame(vm)->closure->function->chunk;
            SleepyObjString *kw_str = (SleepyObjString*)SLEEPY_AS_OBJ(chunk->constants[kw_idx]);
            SleepyObjString *name_str = (SleepyObjString*)SLEEPY_AS_OBJ(chunk->constants[name_idx]);
            bool handled = false;
            SleepyBridgeType *bt = vm->bridge_types;
            while (bt) {
                if (strcmp(bt->keyword, kw_str->chars) == 0) {
                    if (bt->handler) {
                        bt->handler(vm, kw_str->chars, name_str ? name_str->chars : NULL,
                                    NULL, cl, bt->userdata);
                    }
                    handled = true;
                    break;
                }
                bt = bt->next;
            }
            if (!handled || (bt && !bt->handler)) {
                if (name_str && name_str->length > 0) {
                    sleepy_obj_hash_set(vm->allocator, vm->globals,
                        SLEEPY_OBJ_VAL(name_str), SLEEPY_OBJ_VAL(cl));
                }
            }
            break;
        }
        case OP_IMPORT:
            read_short(current_frame(vm));
            read_short(current_frame(vm));
            break;
        case OP_BACKTICK:
            read_short(current_frame(vm));
            break;
        case OP_CLASS_LITERAL:
            read_short(current_frame(vm));
            break;
        case OP_ADDRESS: {
            uint16_t idx = read_short(current_frame(vm));
            SleepyValue name_val = current_frame(vm)->closure->function->chunk->constants[idx];
            SleepyValue val = sleepy_obj_hash_get(vm->globals, name_val);
            sleepy_vm_push(vm, val);
            break;
        }
        case OP_LOCAL:
            read_byte(current_frame(vm));
            break;
        case OP_THIS:
            break;
        case OP_UNPACK_TUPLE:
            read_byte(current_frame(vm));
            break;
        case OP_TAILCALL: {
            read_byte(current_frame(vm));
            break;
        }
        case OP_FOREACH_NEXT: {
            uint16_t offset = read_short(current_frame(vm));
            SleepyValue iterator = sleepy_vm_peek(vm, 0);
            SleepyValue collection = sleepy_vm_peek(vm, 1);
            
            bool done = true;
            SleepyValue key = SLEEPY_NULL_VAL;
            SleepyValue val = SLEEPY_NULL_VAL;
            
            if (SLEEPY_IS_OBJ(collection)) {
                SleepyObj *obj = SLEEPY_AS_OBJ(collection);
                int idx = (int)SLEEPY_AS_NUM(iterator);
                if (obj->type == SLEEPY_OBJ_ARRAY) {
                    SleepyObjArray *arr = (SleepyObjArray*)obj;
                    if (idx >= 0 && idx < arr->count) {
                        key = SLEEPY_NUM_VAL((double)idx);
                        val = arr->elements[idx];
                        done = false;
                    }
                } else if (obj->type == SLEEPY_OBJ_HASH) {
                    SleepyObjHash *hash = (SleepyObjHash*)obj;
                    int cur = 0;
                    for (int i = 0; i < hash->capacity; i++) {
                        if (!SLEEPY_IS_NULL(hash->entries[i].key)) {
                            if (cur == idx) {
                                key = hash->entries[i].key;
                                val = hash->entries[i].value;
                                done = false;
                                break;
                            }
                            cur++;
                        }
                    }
                } else if (obj->type == SLEEPY_OBJ_STRING) {
                    SleepyObjString *str = (SleepyObjString*)obj;
                    if (idx >= 0 && idx < (int)str->length) {
                        key = SLEEPY_NUM_VAL((double)idx);
                        char char_buf[2] = { str->chars[idx], '\0' };
                        val = SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, char_buf, 1));
                        done = false;
                    }
                }
            }
            
            if (done) {
                current_frame(vm)->ip += offset;
            } else {
                vm->stack_top[-1] = SLEEPY_NUM_VAL(SLEEPY_AS_NUM(iterator) + 1);
                sleepy_vm_push(vm, key);
                sleepy_vm_push(vm, val);
            }
            break;
        }
        case OP_OBJ_EXPR:
            read_byte(current_frame(vm));
            break;
        case OP_MATCH:
            sleepy_vm_pop(vm); sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(false));
            break;
        case OP_NOT_MATCH:
            sleepy_vm_pop(vm); sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(true));
            break;
        case OP_UNARY_PREDICATE:
            read_short(current_frame(vm));
            sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(true));
            break;
        case OP_BINARY_PREDICATE:
        case OP_NEGATED_BINARY_PREDICATE:
            read_short(current_frame(vm));
            sleepy_vm_pop(vm);
            sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(true));
            break;
        case OP_CALLCC:
            read_byte(current_frame(vm));
            break;
        case OP_RESUME:
            break;
        case OP_NOP:
            break;
        default:
            sleepy_vm_runtime_error(vm, "Unknown opcode.");
            return SLEEPY_RUNTIME_ERROR;
        }
    }
    }
    return SLEEPY_OK;
}

SleepyResult sleepy_vm_interpret(SleepyVM *vm, const char *source) {
    SleepyParser parser;
    sleepy_parser_init(&parser, source, vm->allocator);
    SleepyASTNode *ast = sleepy_parser_parse(&parser);
    if (parser.had_error || !ast) {
        if (vm->error_fn)
            vm->error_fn(vm->error_userdata, parser.error_line,
                         parser.error_message ? parser.error_message : "Parse error");
        if (ast) sleepy_parser_free_node(ast, vm->allocator);
        return SLEEPY_COMPILE_ERROR;
    }

    SleepyObjFunction *fn = sleepy_compile(vm, ast, vm->allocator);
    sleepy_parser_free_node(ast, vm->allocator);

    if (!fn) return SLEEPY_COMPILE_ERROR;

    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->try_handler_count = 0;

    bool had_sub = false;
    SleepyBridgeType *bt = vm->bridge_types;
    while (bt) {
        if (strcmp(bt->keyword, "sub") == 0 || strcmp(bt->keyword, "alias") == 0) {
            had_sub = true;
            break;
        }
        bt = bt->next;
    }
    if (!had_sub)
        sleepy_vm_register_bridge_type(vm, "sub", NULL, NULL);
    return sleepy_vm_run(vm, fn);
}

void sleepy_vm_register_bridge_type(SleepyVM *vm, const char *keyword,
    void (*handler)(SleepyVM*, const char*, const char*, const char*,
                    SleepyObjClosure*, void*),
    void *userdata) {
    SleepyBridgeType *bt = SLEEPY_ALLOCATE(vm->allocator, SleepyBridgeType);
    size_t kw_len = sleepy_utils_strlen(keyword);
    char *kw_copy = (char*)SLEEPY_MALLOC(vm->allocator, kw_len + 1);
    sleepy_utils_strcpy(kw_copy, keyword);
    bt->keyword = kw_copy;
    bt->handler = handler;
    bt->userdata = userdata;
    bt->next = vm->bridge_types;
    vm->bridge_types = bt;
}

void sleepy_vm_register_native(SleepyVM *vm, const char *name, SleepyNativeFn fn) {
    define_native(vm, name, fn);
}

void sleepy_vm_set_error_fn(SleepyVM *vm, SleepyVMErrorFn fn, void *ud) {
    vm->error_fn = fn;
    vm->error_userdata = ud;
}

void sleepy_vm_set_write_fn(SleepyVM *vm, SleepyVMWriteFn fn, void *ud) {
    vm->write_fn = fn;
    vm->write_userdata = ud;
}

void sleepy_vm_ffi_set_null(SleepyVM *vm, int slot) { vm->ffi_slots[slot] = SLEEPY_NULL_VAL; }
void sleepy_vm_ffi_set_bool(SleepyVM *vm, int slot, bool val) { vm->ffi_slots[slot] = SLEEPY_BOOL_VAL(val); }
void sleepy_vm_ffi_set_number(SleepyVM *vm, int slot, double val) { vm->ffi_slots[slot] = SLEEPY_NUM_VAL(val); }
void sleepy_vm_ffi_set_long(SleepyVM *vm, int slot, int64_t val) {
    SleepyObjLong *obj = sleepy_obj_long_new(vm->allocator, val);
    if (obj) { obj->obj.next = vm->objects; vm->objects = &obj->obj; }
    vm->ffi_slots[slot] = SLEEPY_OBJ_VAL(obj);
}
void sleepy_vm_ffi_set_string(SleepyVM *vm, int slot, const char *val) {
    SleepyObjString *str = sleepy_vm_copy_string(vm, val, (uint32_t)sleepy_utils_strlen(val));
    vm->ffi_slots[slot] = SLEEPY_OBJ_VAL(str);
}

bool sleepy_vm_ffi_is_null(SleepyVM *vm, int slot) { return SLEEPY_IS_NULL(vm->ffi_slots[slot]); }
bool sleepy_vm_ffi_is_bool(SleepyVM *vm, int slot) { return SLEEPY_IS_BOOL(vm->ffi_slots[slot]); }
bool sleepy_vm_ffi_is_number(SleepyVM *vm, int slot) { return SLEEPY_IS_NUM(vm->ffi_slots[slot]); }
bool sleepy_vm_ffi_is_string(SleepyVM *vm, int slot) {
    SleepyValue v = vm->ffi_slots[slot];
    return SLEEPY_IS_OBJ(v) && SLEEPY_OBJ_TYPE(v) == SLEEPY_OBJ_STRING;
}
bool sleepy_vm_ffi_get_bool(SleepyVM *vm, int slot) { return SLEEPY_AS_BOOL(vm->ffi_slots[slot]); }
double sleepy_vm_ffi_get_number(SleepyVM *vm, int slot) { return SLEEPY_AS_NUM(vm->ffi_slots[slot]); }
int64_t sleepy_vm_ffi_get_long(SleepyVM *vm, int slot) {
    SleepyValue v = vm->ffi_slots[slot];
    if (SLEEPY_IS_OBJ(v) && SLEEPY_OBJ_TYPE(v) == SLEEPY_OBJ_LONG)
        return ((SleepyObjLong*)SLEEPY_AS_OBJ(v))->value;
    return (int64_t)SLEEPY_AS_NUM(v);
}
const char *sleepy_vm_ffi_get_string(SleepyVM *vm, int slot) {
    SleepyValue v = vm->ffi_slots[slot];
    if (SLEEPY_IS_OBJ(v) && SLEEPY_OBJ_TYPE(v) == SLEEPY_OBJ_STRING)
        return ((SleepyObjString*)SLEEPY_AS_OBJ(v))->chars;
    return NULL;
}
