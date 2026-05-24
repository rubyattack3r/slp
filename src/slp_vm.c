#include "slp_vm.h"
#include "slp_compiler.h"
#include "slp_ast.h"
#include "slp_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *vm_default_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}

static void define_builtins(SlpVM *vm);

SlpVM *slp_vm_new(SlpAllocator *allocator) {
    SlpVM *vm;
    if (allocator) {
        vm = (SlpVM*)allocator->reallocate(NULL, sizeof(SlpVM), allocator->user_data);
    } else {
        SlpAllocator *a = (SlpAllocator*)malloc(sizeof(SlpAllocator));
        a->reallocate = vm_default_alloc;
        a->user_data = NULL;
        vm = (SlpVM*)a->reallocate(NULL, sizeof(SlpVM), a->user_data);
        allocator = a;
    }
    if (!vm) return NULL;
    memset(vm, 0, sizeof(SlpVM));
    vm->allocator = allocator;
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->objects = NULL;

    vm->open_upvalues = NULL;
    vm->bridge_types = NULL;
    vm->halted = false;
    vm->thrown_exception = SLP_NULL_VAL;
    vm->error_fn = NULL;
    vm->error_userdata = NULL;
    vm->write_fn = NULL;
    vm->write_userdata = NULL;

    slp_gc_init(vm);

    vm->globals = slp_obj_hash_new(allocator);
    if (vm->globals) {
        vm->globals->obj.next = vm->objects;
        vm->objects = &vm->globals->obj;
    }
    vm->natives = slp_obj_hash_new(allocator);
    if (vm->natives) {
        vm->natives->obj.next = vm->objects;
        vm->objects = &vm->natives->obj;
    }

    define_builtins(vm);

    return vm;
}

void slp_vm_free(SlpVM *vm) {
    SlpBridgeType *bt = vm->bridge_types;
    while (bt) {
        SlpBridgeType *next = bt->next;
        SLP_FREE(vm->allocator, bt->keyword);
        SLP_FREE(vm->allocator, bt);
        bt = next;
    }
    if (vm->interned)
        vm->allocator->reallocate(vm->interned, 0, vm->allocator->user_data);
    slp_gc_free(vm);
    SLP_FREE(vm->allocator, vm);
}

void slp_vm_push(SlpVM *vm, SlpValue value) {
    *vm->stack_top = value;
    vm->stack_top++;
}

SlpValue slp_vm_pop(SlpVM *vm) {
    vm->stack_top--;
    return *vm->stack_top;
}

SlpValue slp_vm_peek(SlpVM *vm, int distance) {
    return vm->stack_top[-1 - distance];
}

static void intern_grow(SlpVM *vm) {
    int new_cap = vm->interned_capacity * 2;
    SlpObjString **new_table = (SlpObjString**)vm->allocator->reallocate(
        NULL, sizeof(SlpObjString*) * new_cap, vm->allocator->user_data);
    if (!new_table) return;
    memset(new_table, 0, sizeof(SlpObjString*) * new_cap);
    for (int i = 0; i < vm->interned_capacity; i++) {
        SlpObjString *s = vm->interned[i];
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

SlpObjString *slp_vm_intern_string(SlpVM *vm, const char *chars, uint32_t length) {
    uint32_t hash = slp_hash_string(chars, length);
    if (vm->interned) {
        uint32_t idx = hash & (vm->interned_capacity - 1);
        for (int probes = 0; probes < vm->interned_capacity; probes++) {
            SlpObjString *s = vm->interned[idx];
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
            vm->interned_capacity = SLP_INTERN_INIT_CAP;
            vm->interned = (SlpObjString**)vm->allocator->reallocate(
                NULL, sizeof(SlpObjString*) * vm->interned_capacity,
                vm->allocator->user_data);
            if (vm->interned) memset(vm->interned, 0, sizeof(SlpObjString*) * vm->interned_capacity);
        } else {
            intern_grow(vm);
        }
    }
    SlpObjString *str = slp_obj_string_new(vm->allocator, chars, length);
    if (str) {
        str->hash = hash;
        str->obj.next = vm->objects;
        vm->objects = &str->obj;
        vm->bytes_allocated += sizeof(SlpObjString) + length + 1;
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

SlpObjString *slp_vm_copy_string(SlpVM *vm, const char *chars, uint32_t length) {
    return slp_vm_intern_string(vm, chars, length);
}

static SlpCallFrame *current_frame(SlpVM *vm) {
    return &vm->frames[vm->frame_count - 1];
}

static uint8_t read_byte(SlpCallFrame *frame) {
    return *frame->ip++;
}

static uint16_t read_short(SlpCallFrame *frame) {
    frame->ip += 2;
    return (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]);
}

static SlpValue read_constant(SlpCallFrame *frame) {
    uint16_t idx = read_short(frame);
    return frame->closure->function->chunk->constants[idx];
}

static void close_upvalues(SlpVM *vm, SlpValue *last) {
    while (vm->open_upvalues && vm->open_upvalues->location >= last) {
        SlpObjUpvalue *uv = vm->open_upvalues;
        uv->closed = *uv->location;
        uv->location = &uv->closed;
        vm->open_upvalues = uv->next;
    }
}

static void define_native(SlpVM *vm, const char *name, SlpNativeFn fn) {
    SlpObjString *name_str = slp_vm_copy_string(vm, name, (uint32_t)strlen(name));
    SlpObjNative *native = slp_obj_native_new(vm->allocator, fn, name_str);
    if (native) {
        native->obj.next = vm->objects;
        vm->objects = &native->obj;
    }
    slp_obj_hash_set(vm->allocator, vm->globals, SLP_OBJ_VAL(name_str), SLP_OBJ_VAL(native));
}

static SlpValue native_println(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc > 0) {
        if (SLP_IS_NUM(args[0]))
            printf("%g\n", SLP_AS_NUM(args[0]));
        else if (SLP_IS_BOOL(args[0]))
            printf("%s\n", SLP_AS_BOOL(args[0]) ? "true" : "false");
        else if (SLP_IS_NULL(args[0]))
            printf("$null\n");
        else if (SLP_IS_OBJ(args[0])) {
            SlpObj *obj = SLP_AS_OBJ(args[0]);
            if (obj->type == SLP_OBJ_STRING)
                printf("%s\n", ((SlpObjString*)obj)->chars);
            else
                printf("[object]\n");
        }
    } else {
        printf("\n");
    }
    return SLP_NULL_VAL;
}

static SlpValue native_size(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    if (SLP_IS_OBJ(args[0])) {
        SlpObj *obj = SLP_AS_OBJ(args[0]);
        if (obj->type == SLP_OBJ_STRING)
            return SLP_NUM_VAL((double)((SlpObjString*)obj)->length);
        if (obj->type == SLP_OBJ_ARRAY)
            return SLP_NUM_VAL((double)((SlpObjArray*)obj)->count);
        if (obj->type == SLP_OBJ_HASH)
            return SLP_NUM_VAL((double)((SlpObjHash*)obj)->count);
    }
    return SLP_NUM_VAL(0);
}

static SlpValue native_array(SlpVM *vm, SlpValue *args, int argc) {
    SlpObjArray *arr = slp_obj_array_new(vm->allocator);
    if (arr) {
        arr->obj.next = vm->objects;
        vm->objects = &arr->obj;
        for (int i = 0; i < argc; i++)
            slp_obj_array_push(vm->allocator, arr, args[i]);
    }
    return arr ? SLP_OBJ_VAL(arr) : SLP_NULL_VAL;
}

static SlpValue native_push(SlpVM *vm, SlpValue *args, int argc) {
    if (argc >= 2 && SLP_IS_OBJ(args[0]) &&
        SLP_OBJ_TYPE(args[0]) == SLP_OBJ_ARRAY) {
        SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
        slp_obj_array_push(vm->allocator, arr, args[1]);
        return SLP_OBJ_VAL(arr);
    }
    return SLP_NULL_VAL;
}

static SlpValue native_pop(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc >= 1 && SLP_IS_OBJ(args[0]) &&
        SLP_OBJ_TYPE(args[0]) == SLP_OBJ_ARRAY) {
        return slp_obj_array_pop(SLP_AS_ARRAY(args[0]));
    }
    return SLP_NULL_VAL;
}

static SlpValue native_hash(SlpVM *vm, SlpValue *args, int argc) {
    SlpObjHash *hash = slp_obj_hash_new(vm->allocator);
    if (hash) {
        hash->obj.next = vm->objects;
        vm->objects = &hash->obj;
        for (int i = 0; i < argc - 1; i += 2)
            slp_obj_hash_set(vm->allocator, hash, args[i], args[i+1]);
    }
    return hash ? SLP_OBJ_VAL(hash) : SLP_NULL_VAL;
}

static SlpValue native_typeof(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    SlpValue v = args[0];
    if (SLP_IS_NULL(v)) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "null", 4));
    if (SLP_IS_BOOL(v)) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "boolean", 7));
    if (SLP_IS_NUM(v)) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "number", 6));
    if (SLP_IS_OBJ(v)) {
        switch (SLP_OBJ_TYPE(v)) {
            case SLP_OBJ_STRING: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "string", 6));
            case SLP_OBJ_LONG: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "long", 4));
            case SLP_OBJ_ARRAY: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "array", 5));
            case SLP_OBJ_HASH: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "hash", 4));
            case SLP_OBJ_FUNCTION:
            case SLP_OBJ_CLOSURE:
            case SLP_OBJ_NATIVE:
            case SLP_OBJ_BRIDGE:
                return SLP_OBJ_VAL(slp_vm_copy_string(vm, "function", 8));
            default: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "object", 6));
        }
    }
    return SLP_NULL_VAL;
}

static SlpValue native_keys(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_HASH)
        return SLP_NULL_VAL;
    SlpObjHash *hash = SLP_AS_HASH(args[0]);
    SlpObjArray *arr = slp_obj_array_new(vm->allocator);
    if (!arr) return SLP_NULL_VAL;
    arr->obj.next = vm->objects;
    vm->objects = &arr->obj;
    for (int i = 0; i < hash->capacity; i++) {
        if (!SLP_IS_NULL(hash->entries[i].key)) {
            slp_obj_array_push(vm->allocator, arr, hash->entries[i].key);
        }
    }
    return SLP_OBJ_VAL(arr);
}

static SlpValue native_remove(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0])) return SLP_BOOL_VAL(false);
    if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_HASH) {
        return SLP_BOOL_VAL(slp_obj_hash_delete(vm->allocator, SLP_AS_HASH(args[0]), args[1]));
    }
    return SLP_BOOL_VAL(false);
}

static void define_builtins(SlpVM *vm) {
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

static bool call_closure(SlpVM *vm, SlpObjClosure *closure, int arg_count) {
    if (vm->frame_count >= SLP_MAX_FRAMES) {
        slp_vm_runtime_error(vm, "Stack overflow.");
        return false;
    }
    
    /* Construct array '_' containing all passed arguments */
    SlpObjArray *arr = slp_obj_array_new(vm->allocator);
    if (!arr) {
        slp_vm_runtime_error(vm, "Out of memory.");
        return false;
    }
    arr->obj.next = vm->objects;
    vm->objects = &arr->obj;

    for (int i = 0; i < arg_count; i++) {
        slp_obj_array_push(vm->allocator, arr, vm->stack_top[-arg_count + i]);
    }

    if (arg_count < 9) {
        /* Pad stack with $null so we have at least 9 argument slots (for $1 to $9) */
        for (int i = arg_count; i < 9; i++) {
            slp_vm_push(vm, SLP_NULL_VAL);
        }
        /* Push the @_ array as slot 10 */
        slp_vm_push(vm, SLP_OBJ_VAL(arr));
    } else {
        /* arg_count >= 9. Insert array '_' at slot 10 (index 10 from frame slots)
         * Stack layout currently has: [closure] [arg1] ... [arg9] [arg10] ... [argN]
         * We want to shift [arg10] ... [argN] to the right to make space for arr at slot 10. */
        int shift_count = arg_count - 9;
        slp_vm_push(vm, SLP_NULL_VAL); /* grow stack by 1 */
        for (int i = shift_count - 1; i >= 0; i--) {
            vm->stack_top[-1 - i] = vm->stack_top[-2 - i];
        }
        vm->stack_top[-shift_count - 1] = SLP_OBJ_VAL(arr);
    }

    int frame_slots = (arg_count < 9) ? 10 : arg_count + 1;
    
    SlpCallFrame *frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip = closure->function->chunk->code;
    frame->slots = vm->stack_top - frame_slots - 1;
    return true;
}

static bool call_value(SlpVM *vm, SlpValue callee, int arg_count) {
    if (!SLP_IS_OBJ(callee)) {
        // DRY-RUN VALIDATOR: Do not crash! Consume args, push NULL, return true!
        vm->stack_top -= arg_count + 1;
        slp_vm_push(vm, SLP_NULL_VAL);
        return true;
    }
    switch (SLP_OBJ_TYPE(callee)) {
    case SLP_OBJ_CLOSURE:
        return call_closure(vm, SLP_AS_CLOSURE(callee), arg_count);
    case SLP_OBJ_NATIVE: {
        SlpObjNative *native = SLP_AS_NATIVE(callee);
        SlpValue result = native->fn(vm, vm->stack_top - arg_count, arg_count);
        vm->stack_top -= arg_count + 1;
        slp_vm_push(vm, result);
        return true;
    }
    default:
        // DRY-RUN VALIDATOR: Do not crash! Consume args, push NULL, return true!
        vm->stack_top -= arg_count + 1;
        slp_vm_push(vm, SLP_NULL_VAL);
        return true;
    }
}

void slp_vm_runtime_error(SlpVM *vm, const char *msg) {
    if (vm->error_fn) {
        int line = 0;
        if (vm->frame_count > 0) {
            SlpCallFrame *frame = current_frame(vm);
            int offset = (int)(frame->ip - frame->closure->function->chunk->code);
            if (offset >= 0 && offset < frame->closure->function->chunk->count)
                line = frame->closure->function->chunk->lines[offset];
        }
        vm->error_fn(vm->error_userdata, line, msg);
    }
}

SlpResult slp_vm_call(SlpVM *vm, int arg_count);

SlpResult slp_vm_run(SlpVM *vm, SlpObjFunction *fn) {
    SlpObjClosure *closure = slp_obj_closure_new(vm->allocator, fn);
    if (!closure) return SLP_RUNTIME_ERROR;
    closure->obj.next = vm->objects;
    vm->objects = &closure->obj;

    slp_vm_push(vm, SLP_OBJ_VAL(closure));
    return slp_vm_call(vm, 0);
}

SlpResult slp_vm_call(SlpVM *vm, int arg_count) {
    SlpValue callee = slp_vm_peek(vm, arg_count);
    if (!call_value(vm, callee, arg_count))
        return SLP_RUNTIME_ERROR;
    
    // We need to run the VM to actually execute the closure, 
    // unless it's a native function which call_value already executed.
    if (SLP_IS_OBJ(callee) && SLP_OBJ_TYPE(callee) == SLP_OBJ_CLOSURE) {
        // The call_closure function set up a new frame.
        // We now start the execution loop. We need a way to run the loop until this frame returns.
        int target_frame_count = vm->frame_count - 1;
        for (;;) {
            uint8_t instruction = read_byte(current_frame(vm));
            switch (instruction) {
        case OP_PUSH_NULL:
            slp_vm_push(vm, SLP_NULL_VAL);
            break;
        case OP_PUSH_TRUE:
            slp_vm_push(vm, SLP_TRUE_VAL);
            break;
        case OP_PUSH_FALSE:
            slp_vm_push(vm, SLP_FALSE_VAL);
            break;
        case OP_PUSH_CONST: {
            SlpValue val = read_constant(current_frame(vm));
            slp_vm_push(vm, val);
            break;
        }
        case OP_LOAD_LOCAL_0: slp_vm_push(vm, current_frame(vm)->slots[0]); break;
        case OP_LOAD_LOCAL_1: slp_vm_push(vm, current_frame(vm)->slots[1]); break;
        case OP_LOAD_LOCAL_2: slp_vm_push(vm, current_frame(vm)->slots[2]); break;
        case OP_LOAD_LOCAL_3: slp_vm_push(vm, current_frame(vm)->slots[3]); break;
        case OP_LOAD_LOCAL_4: slp_vm_push(vm, current_frame(vm)->slots[4]); break;
        case OP_LOAD_LOCAL_5: slp_vm_push(vm, current_frame(vm)->slots[5]); break;
        case OP_LOAD_LOCAL_6: slp_vm_push(vm, current_frame(vm)->slots[6]); break;
        case OP_LOAD_LOCAL_7: slp_vm_push(vm, current_frame(vm)->slots[7]); break;
        case OP_STORE_LOCAL_0: current_frame(vm)->slots[0] = slp_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_1: current_frame(vm)->slots[1] = slp_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_2: current_frame(vm)->slots[2] = slp_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_3: current_frame(vm)->slots[3] = slp_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_4: current_frame(vm)->slots[4] = slp_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_5: current_frame(vm)->slots[5] = slp_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_6: current_frame(vm)->slots[6] = slp_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_7: current_frame(vm)->slots[7] = slp_vm_peek(vm, 0); break;
        case OP_LOAD_LOCAL: {
            uint8_t slot = read_byte(current_frame(vm));
            slp_vm_push(vm, current_frame(vm)->slots[slot]);
            break;
        }
        case OP_STORE_LOCAL: {
            uint8_t slot = read_byte(current_frame(vm));
            current_frame(vm)->slots[slot] = slp_vm_peek(vm, 0);
            break;
        }
        case OP_LOAD_GLOBAL: {
            uint16_t idx = read_short(current_frame(vm));
            SlpValue name_val = current_frame(vm)->closure->function->chunk->constants[idx];
            SlpObjString *name_str = SLP_AS_STRING(name_val);
            
            // Inline fast-path for string lookup in globals
            SlpObjHash *hash = vm->globals;
            SlpValue val = SLP_NULL_VAL;
            if (hash->capacity > 0) {
                uint32_t bucket = name_str->hash & (hash->capacity - 1);
                for (int i = 0; i < hash->capacity; i++) {
                    SlpValue k = hash->entries[bucket].key;
                    if (SLP_IS_NULL(k)) {
                        if (!SLP_IS_TRUE(hash->entries[bucket].value)) break;
                    } else if (SLP_IS_OBJ(k) && SLP_AS_OBJ(k) == SLP_AS_OBJ(name_val)) {
                        val = hash->entries[bucket].value;
                        break;
                    }
                    bucket = (bucket + 1) & (hash->capacity - 1);
                }
            }
            slp_vm_push(vm, val);
            break;
        }
        case OP_STORE_GLOBAL: {
            uint16_t idx = read_short(current_frame(vm));
            SlpValue name_val = current_frame(vm)->closure->function->chunk->constants[idx];
            SlpValue val = slp_vm_peek(vm, 0);
            SlpObjString *name_str = SLP_AS_STRING(name_val);
            SlpObjHash *hash = vm->globals;
            bool set = false;

            if (hash->capacity > 0) {
                uint32_t bucket = name_str->hash & (hash->capacity - 1);
                for (int i = 0; i < hash->capacity; i++) {
                    SlpValue k = hash->entries[bucket].key;
                    if (SLP_IS_NULL(k)) {
                        if (!SLP_IS_TRUE(hash->entries[bucket].value)) break;
                    } else if (SLP_IS_OBJ(k) && SLP_AS_OBJ(k) == SLP_AS_OBJ(name_val)) {
                        hash->entries[bucket].value = val;
                        set = true;
                        break;
                    }
                    bucket = (bucket + 1) & (hash->capacity - 1);
                }
            }

            if (!set) {
                slp_obj_hash_set(vm->allocator, hash, name_val, val);
            }
            break;
        }
        case OP_LOAD_UPVALUE: {
            uint8_t idx = read_byte(current_frame(vm));
            slp_vm_push(vm, *current_frame(vm)->closure->upvalues[idx]->location);
            break;
        }
        case OP_STORE_UPVALUE: {
            uint8_t idx = read_byte(current_frame(vm));
            *current_frame(vm)->closure->upvalues[idx]->location = slp_vm_peek(vm, 0);
            break;
        }
        case OP_ADD: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            if (SLP_IS_NUM(a) && SLP_IS_NUM(b))
                slp_vm_push(vm, SLP_NUM_VAL(SLP_AS_NUM(a) + SLP_AS_NUM(b)));
            else
                slp_vm_push(vm, SLP_NUM_VAL(SLP_AS_NUM(a) + SLP_AS_NUM(b)));
            break;
        }
        case OP_SUBTRACT: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_NUM_VAL(SLP_AS_NUM(a) - SLP_AS_NUM(b)));
            break;
        }
        case OP_MULTIPLY: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_NUM_VAL(SLP_AS_NUM(a) * SLP_AS_NUM(b)));
            break;
        }
        case OP_DIVIDE: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_NUM_VAL(SLP_AS_NUM(a) / SLP_AS_NUM(b)));
            break;
        }
        case OP_MODULO: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_NUM_VAL((double)((long)SLP_AS_NUM(a) % (long)SLP_AS_NUM(b))));
            break;
        }
        case OP_POWER: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            double result = 1;
            double base = SLP_AS_NUM(a);
            long exp = (long)SLP_AS_NUM(b);
            if (exp < 0) { base = 1.0/base; exp = -exp; }
            for (long i = 0; i < exp; i++) result *= base;
            slp_vm_push(vm, SLP_NUM_VAL(result));
            break;
        }
        case OP_NEGATE:
            slp_vm_push(vm, SLP_NUM_VAL(-SLP_AS_NUM(slp_vm_pop(vm))));
            break;
        case OP_CONCAT: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            const char *sa = "", *sb = "";
            uint32_t la = 0, lb = 0;
            char a_buf[64] = {0}, b_buf[64] = {0};
            if (SLP_IS_OBJ(a) && SLP_OBJ_TYPE(a) == SLP_OBJ_STRING) {
                SlpObjString *osa = SLP_AS_STRING(a);
                sa = osa->chars; la = osa->length;
            } else if (SLP_IS_NUM(a)) {
                snprintf(a_buf, sizeof(a_buf), "%g", SLP_AS_NUM(a));
                sa = a_buf; la = (uint32_t)strlen(a_buf);
            } else if (SLP_IS_BOOL(a)) {
                sa = SLP_AS_BOOL(a) ? "true" : "false";
                la = (uint32_t)strlen(sa);
            } else if (SLP_IS_NULL(a)) {
                sa = "$null"; la = 5;
            }
            if (SLP_IS_OBJ(b) && SLP_OBJ_TYPE(b) == SLP_OBJ_STRING) {
                SlpObjString *osb = SLP_AS_STRING(b);
                sb = osb->chars; lb = osb->length;
            } else if (SLP_IS_NUM(b)) {
                snprintf(b_buf, sizeof(b_buf), "%g", SLP_AS_NUM(b));
                sb = b_buf; lb = (uint32_t)strlen(b_buf);
            } else if (SLP_IS_BOOL(b)) {
                sb = SLP_AS_BOOL(b) ? "true" : "false";
                lb = (uint32_t)strlen(sb);
            } else if (SLP_IS_NULL(b)) {
                sb = "$null"; lb = 5;
            }
            uint32_t total = la + lb;
            char *concat_buf = (char*)SLP_MALLOC(vm->allocator, total + 1);
            memcpy(concat_buf, sa, la);
            memcpy(concat_buf + la, sb, lb);
            concat_buf[total] = '\0';
            SlpObjString *result = slp_vm_intern_string(vm, concat_buf, total);
            SLP_FREE(vm->allocator, concat_buf);
            slp_vm_push(vm, SLP_OBJ_VAL(result));
            break;
        }
        case OP_REPEAT: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            (void)a; (void)b;
            slp_vm_push(vm, SLP_NULL_VAL);
            break;
        }
        case OP_EQUAL: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_BOOL_VAL(slp_value_equals(a, b)));
            break;
        }
        case OP_NOT_EQUAL: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_BOOL_VAL(!slp_value_equals(a, b)));
            break;
        }
        case OP_LESS: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_BOOL_VAL(SLP_AS_NUM(a) < SLP_AS_NUM(b)));
            break;
        }
        case OP_GREATER: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_BOOL_VAL(SLP_AS_NUM(a) > SLP_AS_NUM(b)));
            break;
        }
        case OP_LESS_EQUAL: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_BOOL_VAL(SLP_AS_NUM(a) <= SLP_AS_NUM(b)));
            break;
        }
        case OP_GREATER_EQUAL: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_BOOL_VAL(SLP_AS_NUM(a) >= SLP_AS_NUM(b)));
            break;
        }
        case OP_SPACESHIP: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            double da = SLP_AS_NUM(a), db = SLP_AS_NUM(b);
            slp_vm_push(vm, SLP_NUM_VAL(da < db ? -1.0 : (da > db ? 1.0 : 0.0)));
            break;
        }
        case OP_NOT:
            slp_vm_push(vm, SLP_BOOL_VAL(slp_value_is_falsy(slp_vm_pop(vm))));
            break;
        case OP_AND: {
            uint16_t offset = read_short(current_frame(vm));
            if (slp_value_is_falsy(slp_vm_peek(vm, 0))) {
                current_frame(vm)->ip += offset;
            } else {
                slp_vm_pop(vm);
            }
            break;
        }
        case OP_OR: {
            uint16_t offset = read_short(current_frame(vm));
            if (!slp_value_is_falsy(slp_vm_peek(vm, 0))) {
                current_frame(vm)->ip += offset;
            } else {
                slp_vm_pop(vm);
            }
            break;
        }
        case OP_BIT_AND: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_NUM_VAL((double)((long)SLP_AS_NUM(a) & (long)SLP_AS_NUM(b))));
            break;
        }
        case OP_BIT_OR: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_NUM_VAL((double)((long)SLP_AS_NUM(a) | (long)SLP_AS_NUM(b))));
            break;
        }
        case OP_BIT_XOR: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_NUM_VAL((double)((long)SLP_AS_NUM(a) ^ (long)SLP_AS_NUM(b))));
            break;
        }
        case OP_BIT_NOT:
            slp_vm_push(vm, SLP_NUM_VAL((double)(~(long)SLP_AS_NUM(slp_vm_pop(vm)))));
            break;
        case OP_LSHIFT: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_NUM_VAL((double)((long)SLP_AS_NUM(a) << (long)SLP_AS_NUM(b))));
            break;
        }
        case OP_RSHIFT: {
            SlpValue b = slp_vm_pop(vm);
            SlpValue a = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_NUM_VAL((double)((long)SLP_AS_NUM(a) >> (long)SLP_AS_NUM(b))));
            break;
        }
        case OP_INCREMENT: {
            SlpValue v = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_NUM_VAL(SLP_AS_NUM(v) + 1));
            break;
        }
        case OP_DECREMENT: {
            SlpValue v = slp_vm_pop(vm);
            slp_vm_push(vm, SLP_NUM_VAL(SLP_AS_NUM(v) - 1));
            break;
        }
        case OP_POP:
            slp_vm_pop(vm);
            break;
        case OP_DUP:
            slp_vm_push(vm, slp_vm_peek(vm, 0));
            break;
        case OP_JUMP: {
            uint16_t offset = read_short(current_frame(vm));
            current_frame(vm)->ip += offset;
            break;
        }
        case OP_JUMP_IF_FALSE: {
            uint16_t offset = read_short(current_frame(vm));
            if (slp_value_is_falsy(slp_vm_peek(vm, 0)))
                current_frame(vm)->ip += offset;
            break;
        }
        case OP_JUMP_IF_TRUE: {
            uint16_t offset = read_short(current_frame(vm));
            if (!slp_value_is_falsy(slp_vm_peek(vm, 0)))
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
            if (!call_value(vm, slp_vm_peek(vm, arg_count), arg_count))
                return SLP_RUNTIME_ERROR;
            break;
        }
        case OP_RETURN: {
            SlpValue result = slp_vm_pop(vm);
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
                SlpCallFrame *old_frame = &vm->frames[vm->frame_count];
                vm->stack_top = old_frame->slots;
                slp_vm_push(vm, result);
                return SLP_OK;
            }
            if (vm->frame_count == 0) return SLP_OK; // Should not happen here if target_frame_count was set right, but safe.
            SlpCallFrame *old_frame = &vm->frames[vm->frame_count];
            vm->stack_top = old_frame->slots;
            slp_vm_push(vm, result);
            break;
        }
        case OP_CLOSURE: {
            uint16_t fn_idx = read_short(current_frame(vm));
            SlpObjFunction *fn_obj = (SlpObjFunction*)SLP_AS_OBJ(
                current_frame(vm)->closure->function->chunk->constants[fn_idx]);
            SlpObjClosure *cl = slp_obj_closure_new(vm->allocator, fn_obj);
            cl->obj.next = vm->objects;
            vm->objects = &cl->obj;
            for (int i = 0; i < fn_obj->upvalue_count; i++) {
                uint8_t is_local = read_byte(current_frame(vm));
                uint8_t idx = read_byte(current_frame(vm));
                if (is_local) {
                    cl->upvalues[i] = slp_obj_upvalue_new(vm->allocator,
                        &current_frame(vm)->slots[idx]);
                    cl->upvalues[i]->obj.next = vm->objects;
                    vm->objects = &cl->upvalues[i]->obj;
                } else {
                    cl->upvalues[i] = current_frame(vm)->closure->upvalues[idx];
                }
            }
            slp_vm_push(vm, SLP_OBJ_VAL(cl));
            break;
        }
        case OP_CLOSE_UPVALUE:
            close_upvalues(vm, vm->stack_top - 1);
            slp_vm_pop(vm);
            break;
        case OP_BUILD_ARRAY: {
            uint16_t count = read_short(current_frame(vm));
            SlpObjArray *arr = slp_obj_array_new(vm->allocator);
            arr->obj.next = vm->objects;
            vm->objects = &arr->obj;
            for (uint16_t i = 0; i < count; i++) {
                SlpValue val = slp_vm_pop(vm);
                slp_obj_array_push(vm->allocator, arr, val);
            }
            for (int i = 0; i < arr->count / 2; i++) {
                SlpValue tmp = arr->elements[i];
                arr->elements[i] = arr->elements[arr->count - 1 - i];
                arr->elements[arr->count - 1 - i] = tmp;
            }
            slp_vm_push(vm, SLP_OBJ_VAL(arr));
            break;
        }
        case OP_BUILD_HASH: {
            uint16_t count = read_short(current_frame(vm));
            SlpObjHash *hash = slp_obj_hash_new(vm->allocator);
            hash->obj.next = vm->objects;
            vm->objects = &hash->obj;
            for (uint16_t i = 0; i < count; i++) {
                SlpValue val = slp_vm_pop(vm);
                SlpValue key = slp_vm_pop(vm);
                slp_obj_hash_set(vm->allocator, hash, key, val);
            }
            slp_vm_push(vm, SLP_OBJ_VAL(hash));
            break;
        }
        case OP_INDEX_GET: {
            SlpValue idx_val = slp_vm_pop(vm);
            SlpValue container = slp_vm_pop(vm);
            if (SLP_IS_OBJ(container)) {
                SlpObj *obj = SLP_AS_OBJ(container);
                if (obj->type == SLP_OBJ_ARRAY) {
                    int idx = (int)SLP_AS_NUM(idx_val);
                    slp_vm_push(vm, slp_obj_array_get((SlpObjArray*)obj, idx));
                } else if (obj->type == SLP_OBJ_HASH) {
                    slp_vm_push(vm, slp_obj_hash_get((SlpObjHash*)obj, idx_val));
                } else {
                    slp_vm_push(vm, SLP_NULL_VAL);
                }
            } else {
                slp_vm_push(vm, SLP_NULL_VAL);
            }
            break;
        }
        case OP_INDEX_SET: {
            SlpValue val = slp_vm_pop(vm);
            SlpValue idx_val = slp_vm_pop(vm);
            SlpValue container = slp_vm_pop(vm);
            if (SLP_IS_OBJ(container)) {
                SlpObj *obj = SLP_AS_OBJ(container);
                if (obj->type == SLP_OBJ_ARRAY)
                    slp_obj_array_set(vm->allocator, (SlpObjArray*)obj, (int)SLP_AS_NUM(idx_val), val);
                else if (obj->type == SLP_OBJ_HASH)
                    slp_obj_hash_set(vm->allocator, (SlpObjHash*)obj, idx_val, val);
            }
            slp_vm_push(vm, val);
            break;
        }
        case OP_PUSH_HANDLER: {
            uint16_t offset = read_short(current_frame(vm));
            SlpCallFrame *frame = current_frame(vm);
            if (vm->try_handler_count < SLP_MAX_HANDLERS) {
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
            SlpValue exc = slp_vm_pop(vm);
            if (vm->try_handler_count > 0) {
                vm->try_handler_count--;
                SlpTryHandler h = vm->try_handlers[vm->try_handler_count];
                while (vm->frame_count > h.frame_count) {
                    close_upvalues(vm, current_frame(vm)->slots);
                    vm->frame_count--;
                }
                current_frame(vm)->ip = h.catch_ip;
                slp_vm_push(vm, exc);
            } else {
                vm->thrown_exception = exc;
                return SLP_RUNTIME_ERROR;
            }
            break;
        }
        case OP_ASSERT: {
            SlpValue test = slp_vm_pop(vm);
            if (slp_value_is_falsy(test)) {
                slp_vm_runtime_error(vm, "Assertion failed.");
                return SLP_RUNTIME_ERROR;
            }
            break;
        }
        case OP_HALT:
            return SLP_HALT;
        case OP_DONE:
            return SLP_OK;
        case OP_YIELD:
            return SLP_OK;
        case OP_BREAK:
        case OP_CONTINUE:
            break;
        case OP_BRIDGE_REGISTER: {
            uint16_t kw_idx = read_short(current_frame(vm));
            uint16_t name_idx = read_short(current_frame(vm));
            SlpValue closure_val = slp_vm_pop(vm);
            SlpObjClosure *cl = SLP_AS_CLOSURE(closure_val);
            SlpChunk *chunk = current_frame(vm)->closure->function->chunk;
            SlpObjString *kw_str = (SlpObjString*)SLP_AS_OBJ(chunk->constants[kw_idx]);
            SlpObjString *name_str = (SlpObjString*)SLP_AS_OBJ(chunk->constants[name_idx]);
            bool handled = false;
            SlpBridgeType *bt = vm->bridge_types;
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
                    slp_obj_hash_set(vm->allocator, vm->globals,
                        SLP_OBJ_VAL(name_str), SLP_OBJ_VAL(cl));
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
            SlpValue name_val = current_frame(vm)->closure->function->chunk->constants[idx];
            SlpValue val = slp_obj_hash_get(vm->globals, name_val);
            slp_vm_push(vm, val);
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
            SlpValue iterator = slp_vm_peek(vm, 0);
            SlpValue collection = slp_vm_peek(vm, 1);
            
            bool done = true;
            SlpValue key = SLP_NULL_VAL;
            SlpValue val = SLP_NULL_VAL;
            
            if (SLP_IS_OBJ(collection)) {
                SlpObj *obj = SLP_AS_OBJ(collection);
                int idx = (int)SLP_AS_NUM(iterator);
                if (obj->type == SLP_OBJ_ARRAY) {
                    SlpObjArray *arr = (SlpObjArray*)obj;
                    if (idx >= 0 && idx < arr->count) {
                        key = SLP_NUM_VAL((double)idx);
                        val = arr->elements[idx];
                        done = false;
                    }
                } else if (obj->type == SLP_OBJ_HASH) {
                    SlpObjHash *hash = (SlpObjHash*)obj;
                    int cur = 0;
                    for (int i = 0; i < hash->capacity; i++) {
                        if (!SLP_IS_NULL(hash->entries[i].key)) {
                            if (cur == idx) {
                                key = hash->entries[i].key;
                                val = hash->entries[i].value;
                                done = false;
                                break;
                            }
                            cur++;
                        }
                    }
                } else if (obj->type == SLP_OBJ_STRING) {
                    SlpObjString *str = (SlpObjString*)obj;
                    if (idx >= 0 && idx < (int)str->length) {
                        key = SLP_NUM_VAL((double)idx);
                        char char_buf[2] = { str->chars[idx], '\0' };
                        val = SLP_OBJ_VAL(slp_vm_copy_string(vm, char_buf, 1));
                        done = false;
                    }
                }
            }
            
            if (done) {
                current_frame(vm)->ip += offset;
            } else {
                vm->stack_top[-1] = SLP_NUM_VAL(SLP_AS_NUM(iterator) + 1);
                slp_vm_push(vm, key);
                slp_vm_push(vm, val);
            }
            break;
        }
        case OP_OBJ_EXPR:
            read_byte(current_frame(vm));
            break;
        case OP_MATCH:
            slp_vm_pop(vm); slp_vm_pop(vm);
            slp_vm_push(vm, SLP_BOOL_VAL(false));
            break;
        case OP_NOT_MATCH:
            slp_vm_pop(vm); slp_vm_pop(vm);
            slp_vm_push(vm, SLP_BOOL_VAL(true));
            break;
        case OP_UNARY_PREDICATE:
            read_short(current_frame(vm));
            slp_vm_pop(vm);
            slp_vm_push(vm, SLP_BOOL_VAL(true));
            break;
        case OP_BINARY_PREDICATE:
        case OP_NEGATED_BINARY_PREDICATE:
            read_short(current_frame(vm));
            slp_vm_pop(vm);
            slp_vm_pop(vm);
            slp_vm_push(vm, SLP_BOOL_VAL(true));
            break;
        case OP_CALLCC:
            read_byte(current_frame(vm));
            break;
        case OP_RESUME:
            break;
        case OP_NOP:
            break;
        default:
            slp_vm_runtime_error(vm, "Unknown opcode.");
            return SLP_RUNTIME_ERROR;
        }
    }
    }
    return SLP_OK;
}

SlpResult slp_vm_interpret(SlpVM *vm, const char *source) {
    SlpParser parser;
    slp_parser_init(&parser, source, vm->allocator);
    SlpASTNode *ast = slp_parser_parse(&parser);
    if (parser.had_error || !ast) {
        if (vm->error_fn)
            vm->error_fn(vm->error_userdata, parser.error_line,
                         parser.error_message ? parser.error_message : "Parse error");
        if (ast) slp_parser_free_node(ast, vm->allocator);
        return SLP_COMPILE_ERROR;
    }

    SlpObjFunction *fn = slp_compile(vm, ast, vm->allocator);
    slp_parser_free_node(ast, vm->allocator);

    if (!fn) return SLP_COMPILE_ERROR;

    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->try_handler_count = 0;

    bool had_sub = false;
    SlpBridgeType *bt = vm->bridge_types;
    while (bt) {
        if (strcmp(bt->keyword, "sub") == 0 || strcmp(bt->keyword, "alias") == 0) {
            had_sub = true;
            break;
        }
        bt = bt->next;
    }
    if (!had_sub)
        slp_vm_register_bridge_type(vm, "sub", NULL, NULL);
    return slp_vm_run(vm, fn);
}

void slp_vm_register_bridge_type(SlpVM *vm, const char *keyword,
    void (*handler)(SlpVM*, const char*, const char*, const char*,
                    SlpObjClosure*, void*),
    void *userdata) {
    SlpBridgeType *bt = SLP_ALLOCATE(vm->allocator, SlpBridgeType);
    size_t kw_len = slp_utils_strlen(keyword);
    char *kw_copy = (char*)SLP_MALLOC(vm->allocator, kw_len + 1);
    slp_utils_strcpy(kw_copy, keyword);
    bt->keyword = kw_copy;
    bt->handler = handler;
    bt->userdata = userdata;
    bt->next = vm->bridge_types;
    vm->bridge_types = bt;
}

void slp_vm_register_native(SlpVM *vm, const char *name, SlpNativeFn fn) {
    define_native(vm, name, fn);
}

void slp_vm_set_error_fn(SlpVM *vm, SlpVMErrorFn fn, void *ud) {
    vm->error_fn = fn;
    vm->error_userdata = ud;
}

void slp_vm_set_write_fn(SlpVM *vm, SlpVMWriteFn fn, void *ud) {
    vm->write_fn = fn;
    vm->write_userdata = ud;
}

void slp_vm_ffi_set_null(SlpVM *vm, int slot) { vm->ffi_slots[slot] = SLP_NULL_VAL; }
void slp_vm_ffi_set_bool(SlpVM *vm, int slot, bool val) { vm->ffi_slots[slot] = SLP_BOOL_VAL(val); }
void slp_vm_ffi_set_number(SlpVM *vm, int slot, double val) { vm->ffi_slots[slot] = SLP_NUM_VAL(val); }
void slp_vm_ffi_set_long(SlpVM *vm, int slot, int64_t val) {
    SlpObjLong *obj = slp_obj_long_new(vm->allocator, val);
    if (obj) { obj->obj.next = vm->objects; vm->objects = &obj->obj; }
    vm->ffi_slots[slot] = SLP_OBJ_VAL(obj);
}
void slp_vm_ffi_set_string(SlpVM *vm, int slot, const char *val) {
    SlpObjString *str = slp_vm_copy_string(vm, val, (uint32_t)slp_utils_strlen(val));
    vm->ffi_slots[slot] = SLP_OBJ_VAL(str);
}

bool slp_vm_ffi_is_null(SlpVM *vm, int slot) { return SLP_IS_NULL(vm->ffi_slots[slot]); }
bool slp_vm_ffi_is_bool(SlpVM *vm, int slot) { return SLP_IS_BOOL(vm->ffi_slots[slot]); }
bool slp_vm_ffi_is_number(SlpVM *vm, int slot) { return SLP_IS_NUM(vm->ffi_slots[slot]); }
bool slp_vm_ffi_is_string(SlpVM *vm, int slot) {
    SlpValue v = vm->ffi_slots[slot];
    return SLP_IS_OBJ(v) && SLP_OBJ_TYPE(v) == SLP_OBJ_STRING;
}
bool slp_vm_ffi_get_bool(SlpVM *vm, int slot) { return SLP_AS_BOOL(vm->ffi_slots[slot]); }
double slp_vm_ffi_get_number(SlpVM *vm, int slot) { return SLP_AS_NUM(vm->ffi_slots[slot]); }
int64_t slp_vm_ffi_get_long(SlpVM *vm, int slot) {
    SlpValue v = vm->ffi_slots[slot];
    if (SLP_IS_OBJ(v) && SLP_OBJ_TYPE(v) == SLP_OBJ_LONG)
        return ((SlpObjLong*)SLP_AS_OBJ(v))->value;
    return (int64_t)SLP_AS_NUM(v);
}
const char *slp_vm_ffi_get_string(SlpVM *vm, int slot) {
    SlpValue v = vm->ffi_slots[slot];
    if (SLP_IS_OBJ(v) && SLP_OBJ_TYPE(v) == SLP_OBJ_STRING)
        return ((SlpObjString*)SLP_AS_OBJ(v))->chars;
    return NULL;
}
