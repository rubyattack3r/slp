/*
 * aggressor.c - Aggressor Script Extension for the Slp VM
 *
 * This file implements the generic dispatch framework. It registers all
 * known Aggressor functions into the VM and routes calls through either:
 *   1. An explicit per-function override (set via aggressor_set())
 *   2. A consumer-provided fallback handler
 *   3. A built-in safe default stub (returns SLP_NULL_VAL)
 *
 * Pure language utilities (iff, strlen, substr, etc.) have real
 * implementations here and don't go through the dispatch table.
 */

#include "aggressor.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* -----------------------------------------------------------------------
 * Internal Dispatch Table
 * ----------------------------------------------------------------------- */

#define AGGRESSOR_MAX_OVERRIDES 128
#define AGGRESSOR_MAX_TRAMPOLINES 128

typedef struct {
    char name[64];
    AggressorNativeFn fn;
} AggressorOverride;

typedef struct {
    char name[64];
} TrampolineSlot;

typedef struct {
    char key[64];
    char value[256];
} BeaconProperty;

typedef struct {
    BeaconProperty properties[32];
    int property_count;
} MockBeacon;

struct AggressorState {
    AggressorConfig config;
    AggressorOverride overrides[AGGRESSOR_MAX_OVERRIDES];
    int override_count;
    TrampolineSlot trampoline_names[AGGRESSOR_MAX_TRAMPOLINES];
    int trampoline_count;

    /* Dynamic command registry */
    AggressorCommand *commands;
    int command_count;
    int command_capacity;

    /* Dynamic mock beacons */
    MockBeacon mock_beacons[32];
    int mock_beacon_count;
};



static AggressorNativeFn find_override(AggressorState *state, const char *name) {
    for (int i = 0; i < state->override_count; i++) {
        if (strcmp(state->overrides[i].name, name) == 0) {
            return state->overrides[i].fn;
        }
    }
    return NULL;
}

/* Default fallback: returns NULL silently */
static SlpValue default_fallback(SlpVM *vm, const char *func_name,
                                     SlpValue *args, int argc,
                                     void *user_data) {
    (void)vm; (void)func_name; (void)args; (void)argc; (void)user_data;
    return SLP_NULL_VAL;
}

void aggressor_register_command(AggressorState *state, const char *name, const char *description, const char *help) {
    if (!state || !name) return;

    /* Check if already exists; if so, update in place */
    for (int i = 0; i < state->command_count; i++) {
        if (strcmp(state->commands[i].name, name) == 0) {
            free(state->commands[i].description);
            free(state->commands[i].help);
            state->commands[i].description = strdup(description ? description : "");
            state->commands[i].help = strdup(help ? help : "");
            return;
        }
    }

    /* Expand array dynamically if capacity is reached */
    if (state->command_count >= state->command_capacity) {
        int new_capacity = state->command_capacity == 0 ? 8 : state->command_capacity * 2;
        AggressorCommand *new_commands = realloc(state->commands, new_capacity * sizeof(AggressorCommand));
        if (new_commands) {
            state->commands = new_commands;
            state->command_capacity = new_capacity;
        } else {
            return;
        }
    }

    /* Add new command entry */
    AggressorCommand *cmd = &state->commands[state->command_count++];
    cmd->name = strdup(name);
    cmd->description = strdup(description ? description : "");
    cmd->help = strdup(help ? help : "");
}

const char *aggressor_get_command_help(AggressorState *state, const char *name) {
    if (!state || !name) return NULL;
    for (int i = 0; i < state->command_count; i++) {
        if (strcmp(state->commands[i].name, name) == 0) {
            return state->commands[i].help;
        }
    }
    return NULL;
}

void aggressor_deinit(AggressorState *state) {
    if (!state) return;
    for (int i = 0; i < state->command_count; i++) {
        free(state->commands[i].name);
        free(state->commands[i].description);
        free(state->commands[i].help);
    }
    free(state->commands);
    free(state);
}

/* Dispatch logic shared by all trampolines */
static SlpValue dispatch(SlpVM *vm, AggressorState *state, const char *name,
                             SlpValue *args, int argc) {
    /* Check for explicit override first */
    AggressorNativeFn override = find_override(state, name);
    if (override) {
        return override(vm, args, argc, state->config.user_data);
    }

    /* Default dynamic stateful fallback for beacon_command_register and beacon_command_detail */
    if (strcmp(name, "beacon_command_register") == 0) {
        if (argc >= 1 && SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
            const char *cmd_name = SLP_AS_STRING(args[0])->chars;
            const char *description = (argc >= 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING) ? SLP_AS_STRING(args[1])->chars : "";
            const char *help = (argc >= 3 && SLP_IS_OBJ(args[2]) && SLP_OBJ_TYPE(args[2]) == SLP_OBJ_STRING) ? SLP_AS_STRING(args[2])->chars : "";
            aggressor_register_command(state, cmd_name, description, help);
        }
    } else if (strcmp(name, "beacon_command_detail") == 0) {
        if (argc >= 1 && SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
            const char *cmd_name = SLP_AS_STRING(args[0])->chars;
            const char *help = aggressor_get_command_help(state, cmd_name);
            if (help) {
                return SLP_OBJ_VAL(slp_vm_copy_string(vm, help, strlen(help)));
            }
        }
        return SLP_OBJ_VAL(slp_vm_copy_string(vm, "Mock details", 12));
    } else if (strcmp(name, "beacons") == 0) {
        SlpObjArray *arr = slp_obj_array_new(vm->allocator);
        for (int i = 0; i < state->mock_beacon_count; i++) {
            SlpObjHash *b = slp_obj_hash_new(vm->allocator);
            for (int j = 0; j < state->mock_beacons[i].property_count; j++) {
                const char *key = state->mock_beacons[i].properties[j].key;
                const char *val = state->mock_beacons[i].properties[j].value;
                SlpValue val_obj;
                if (strcmp(key, "isadmin") == 0) {
                    val_obj = SLP_NUM_VAL(atoi(val));
                } else {
                    val_obj = SLP_OBJ_VAL(slp_vm_copy_string(vm, val, strlen(val)));
                }
                slp_obj_hash_set(vm->allocator, b, 
                    SLP_OBJ_VAL(slp_vm_copy_string(vm, key, strlen(key))),
                    val_obj);
            }
            slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(b));
        }
        return SLP_OBJ_VAL(arr);
    } else if (strcmp(name, "beacon_ids") == 0) {
        SlpObjArray *arr = slp_obj_array_new(vm->allocator);
        for (int i = 0; i < state->mock_beacon_count; i++) {
            const char *id_val = "unknown";
            for (int j = 0; j < state->mock_beacons[i].property_count; j++) {
                if (strcmp(state->mock_beacons[i].properties[j].key, "id") == 0) {
                    id_val = state->mock_beacons[i].properties[j].value;
                    break;
                }
            }
            slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(slp_vm_copy_string(vm, id_val, strlen(id_val))));
        }
        return SLP_OBJ_VAL(arr);
    } else if (strcmp(name, "beacon_info") == 0) {
        if (argc >= 2 && SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING &&
                         SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING) {
            const char *id = SLP_AS_STRING(args[0])->chars;
            const char *key = SLP_AS_STRING(args[1])->chars;
            
            for (int i = 0; i < state->mock_beacon_count; i++) {
                int matches = 0;
                for (int j = 0; j < state->mock_beacons[i].property_count; j++) {
                    if (strcmp(state->mock_beacons[i].properties[j].key, "id") == 0 &&
                        strcmp(state->mock_beacons[i].properties[j].value, id) == 0) {
                        matches = 1;
                        break;
                    }
                }
                if (matches) {
                    for (int j = 0; j < state->mock_beacons[i].property_count; j++) {
                        if (strcmp(state->mock_beacons[i].properties[j].key, key) == 0) {
                            const char *val = state->mock_beacons[i].properties[j].value;
                            if (strcmp(key, "isadmin") == 0) {
                                return SLP_NUM_VAL(atoi(val));
                            }
                            return SLP_OBJ_VAL(slp_vm_copy_string(vm, val, strlen(val)));
                        }
                    }
                    if (strcmp(key, "barch") == 0) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "x64", 3));
                    if (strcmp(key, "isadmin") == 0) return SLP_NUM_VAL(1);
                    return SLP_NULL_VAL;
                }
            }
            
            if (strcmp(key, "barch") == 0) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "x64", 3));
            if (strcmp(key, "isadmin") == 0) return SLP_NUM_VAL(1);
            if (strcmp(key, "computer") == 0) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "MOCK-COMP", 9));
            if (strcmp(key, "user") == 0) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "mockuser", 8));
        }
        return SLP_OBJ_VAL(slp_vm_copy_string(vm, "mock_beacon_info_val", 20));
    } else if (strcmp(name, "beacon_info_set") == 0) {
        if (argc >= 3 && SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING &&
                         SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING) {
            const char *id = SLP_AS_STRING(args[0])->chars;
            const char *key = SLP_AS_STRING(args[1])->chars;
            
            char val_buf[256] = {0};
            if (SLP_IS_NUM(args[2])) {
                snprintf(val_buf, sizeof(val_buf), "%g", SLP_AS_NUM(args[2]));
            } else if (SLP_IS_BOOL(args[2])) {
                strncpy(val_buf, SLP_AS_BOOL(args[2]) ? "1" : "0", sizeof(val_buf)-1);
            } else if (SLP_IS_NULL(args[2])) {
                val_buf[0] = '\0';
            } else if (SLP_IS_OBJ(args[2]) && SLP_OBJ_TYPE(args[2]) == SLP_OBJ_STRING) {
                strncpy(val_buf, SLP_AS_STRING(args[2])->chars, sizeof(val_buf)-1);
            } else {
                strncpy(val_buf, "[object]", sizeof(val_buf)-1);
            }
            
            MockBeacon *target = NULL;
            for (int i = 0; i < state->mock_beacon_count; i++) {
                for (int j = 0; j < state->mock_beacons[i].property_count; j++) {
                    if (strcmp(state->mock_beacons[i].properties[j].key, "id") == 0) {
                        if (strcmp(state->mock_beacons[i].properties[j].value, id) == 0) {
                            target = &state->mock_beacons[i];
                            break;
                        }
                    }
                }
                if (target) break;
            }
            
            if (!target && state->mock_beacon_count < 32) {
                target = &state->mock_beacons[state->mock_beacon_count++];
                target->property_count = 1;
                strcpy(target->properties[0].key, "id");
                strncpy(target->properties[0].value, id, 255);
            }
            
            if (target) {
                int prop_idx = -1;
                for (int j = 0; j < target->property_count; j++) {
                    if (strcmp(target->properties[j].key, key) == 0) {
                        prop_idx = j;
                        break;
                    }
                }
                
                if (prop_idx != -1) {
                    strncpy(target->properties[prop_idx].value, val_buf, 255);
                } else if (target->property_count < 32) {
                    int new_idx = target->property_count++;
                    strncpy(target->properties[new_idx].key, key, 63);
                    strncpy(target->properties[new_idx].value, val_buf, 255);
                }
            }
        }
        return SLP_NULL_VAL;
    } else if (strcmp(name, "barch") == 0) {
        return SLP_OBJ_VAL(slp_vm_copy_string(vm, "x64", 3));
    } else if (strcmp(name, "dialog") == 0) {
        SlpObjHash *dlg = slp_obj_hash_new(vm->allocator);
        slp_obj_hash_set(vm->allocator, dlg, SLP_OBJ_VAL(slp_vm_copy_string(vm, "__type", 6)), SLP_OBJ_VAL(slp_vm_copy_string(vm, "dialog", 6)));
        
        const char *title = (argc >= 1 && SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) ? SLP_AS_STRING(args[0])->chars : "Untitled Dialog";
        slp_obj_hash_set(vm->allocator, dlg, SLP_OBJ_VAL(slp_vm_copy_string(vm, "__title", 7)), SLP_OBJ_VAL(slp_vm_copy_string(vm, title, strlen(title))));
        
        if (argc >= 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_HASH) {
            SlpObjHash *defaults = SLP_AS_HASH(args[1]);
            for (int i = 0; i < defaults->capacity; i++) {
                if (!SLP_IS_NULL(defaults->entries[i].key)) {
                    slp_obj_hash_set(vm->allocator, dlg, defaults->entries[i].key, defaults->entries[i].value);
                }
            }
        }
        
        if (argc >= 3 && SLP_IS_OBJ(args[2]) && SLP_OBJ_TYPE(args[2]) == SLP_OBJ_CLOSURE) {
            slp_obj_hash_set(vm->allocator, dlg, SLP_OBJ_VAL(slp_vm_copy_string(vm, "__callback", 10)), args[2]);
        }
        
        SlpObjArray *ctrls = slp_obj_array_new(vm->allocator);
        slp_obj_hash_set(vm->allocator, dlg, SLP_OBJ_VAL(slp_vm_copy_string(vm, "__controls", 10)), SLP_OBJ_VAL(ctrls));
        
        SlpObjArray *btns = slp_obj_array_new(vm->allocator);
        slp_obj_hash_set(vm->allocator, dlg, SLP_OBJ_VAL(slp_vm_copy_string(vm, "__buttons", 9)), SLP_OBJ_VAL(btns));
        
        return SLP_OBJ_VAL(dlg);
    } else if (strcmp(name, "drow") == 0 || strcmp(name, "drow_text") == 0 || strcmp(name, "dcheckbox") == 0 || 
               strcmp(name, "dtext") == 0 || strcmp(name, "dtextarea") == 0 || strcmp(name, "dcombobox") == 0 || 
               strcmp(name, "dlist") == 0 || strcmp(name, "dfile") == 0) {
        if (argc >= 1 && SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_HASH) {
            SlpObjHash *dlg = SLP_AS_HASH(args[0]);
            SlpValue ctrls_val = slp_obj_hash_get(dlg, SLP_OBJ_VAL(slp_vm_copy_string(vm, "__controls", 10)));
            if (SLP_IS_OBJ(ctrls_val) && SLP_OBJ_TYPE(ctrls_val) == SLP_OBJ_ARRAY) {
                SlpObjArray *ctrls = SLP_AS_ARRAY(ctrls_val);
                
                SlpObjHash *ctrl = slp_obj_hash_new(vm->allocator);
                slp_obj_hash_set(vm->allocator, ctrl, SLP_OBJ_VAL(slp_vm_copy_string(vm, "type", 4)), SLP_OBJ_VAL(slp_vm_copy_string(vm, name, strlen(name))));
                
                if (strcmp(name, "drow") == 0 || strcmp(name, "drow_text") == 0) {
                    if (argc >= 2) slp_obj_hash_set(vm->allocator, ctrl, SLP_OBJ_VAL(slp_vm_copy_string(vm, "key", 3)), args[1]);
                    if (argc >= 3) slp_obj_hash_set(vm->allocator, ctrl, SLP_OBJ_VAL(slp_vm_copy_string(vm, "label", 5)), args[2]);
                    if (argc >= 4) {
                        slp_obj_hash_set(vm->allocator, ctrl, SLP_OBJ_VAL(slp_vm_copy_string(vm, "description", 11)), args[3]);
                        if (SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING) {
                            slp_obj_hash_set(vm->allocator, dlg, args[1], args[3]);
                        }
                    }
                } else if (strcmp(name, "dcheckbox") == 0) {
                    if (argc >= 2) slp_obj_hash_set(vm->allocator, ctrl, SLP_OBJ_VAL(slp_vm_copy_string(vm, "key", 3)), args[1]);
                    if (argc >= 3) slp_obj_hash_set(vm->allocator, ctrl, SLP_OBJ_VAL(slp_vm_copy_string(vm, "label", 5)), args[2]);
                    if (argc >= 4) slp_obj_hash_set(vm->allocator, ctrl, SLP_OBJ_VAL(slp_vm_copy_string(vm, "description", 11)), args[3]);
                    if (SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING) {
                        SlpValue current = slp_obj_hash_get(dlg, args[1]);
                        if (SLP_IS_NULL(current)) {
                            slp_obj_hash_set(vm->allocator, dlg, args[1], SLP_BOOL_VAL(false));
                        }
                    }
                } else {
                    if (argc >= 2) slp_obj_hash_set(vm->allocator, ctrl, SLP_OBJ_VAL(slp_vm_copy_string(vm, "key", 3)), args[1]);
                    if (argc >= 3) slp_obj_hash_set(vm->allocator, ctrl, SLP_OBJ_VAL(slp_vm_copy_string(vm, "label", 5)), args[2]);
                    if (argc >= 4) {
                        slp_obj_hash_set(vm->allocator, ctrl, SLP_OBJ_VAL(slp_vm_copy_string(vm, "options", 7)), args[3]);
                        if (SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING) {
                            SlpValue current = slp_obj_hash_get(dlg, args[1]);
                            if (SLP_IS_NULL(current)) {
                                slp_obj_hash_set(vm->allocator, dlg, args[1], args[3]);
                            }
                        }
                    }
                }
                slp_obj_array_push(vm->allocator, ctrls, SLP_OBJ_VAL(ctrl));
            }
        }
        return SLP_NULL_VAL;
    } else if (strcmp(name, "dbutton") == 0) {
        if (argc >= 3 && SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_HASH) {
            SlpObjHash *dlg = SLP_AS_HASH(args[0]);
            SlpValue btns_val = slp_obj_hash_get(dlg, SLP_OBJ_VAL(slp_vm_copy_string(vm, "__buttons", 9)));
            if (SLP_IS_OBJ(btns_val) && SLP_OBJ_TYPE(btns_val) == SLP_OBJ_ARRAY) {
                SlpObjArray *btns = SLP_AS_ARRAY(btns_val);
                
                SlpObjHash *btn = slp_obj_hash_new(vm->allocator);
                slp_obj_hash_set(vm->allocator, btn, SLP_OBJ_VAL(slp_vm_copy_string(vm, "label", 5)), args[1]);
                slp_obj_hash_set(vm->allocator, btn, SLP_OBJ_VAL(slp_vm_copy_string(vm, "action", 6)), args[2]);
                
                slp_obj_array_push(vm->allocator, btns, SLP_OBJ_VAL(btn));
            }
        }
        return SLP_NULL_VAL;
    } else if (strcmp(name, "dialog_show") == 0) {
        return SLP_BOOL_VAL(true);
    }

    /* Fall through to consumer fallback */
    AggressorFallbackFn fb = state->config.fallback ? state->config.fallback
                                                 : default_fallback;
    return fb(vm, name, args, argc, state->config.user_data);
}

/*
 * Generate trampoline functions via macro. Each has a unique index
 * and looks up its name from the trampoline table.
 */
#define TRAMPOLINE(n) \
    static SlpValue trampoline_##n(SlpVM *vm, SlpValue *args, int argc) { \
        int64_t addr = slp_vm_ffi_get_long(vm, 255); \
        AggressorState *state = (AggressorState *)(uintptr_t)addr; \
        if (!state) return SLP_NULL_VAL; \
        return dispatch(vm, state, state->trampoline_names[n].name, args, argc); \
    }

/* Generate 128 trampoline slots */
TRAMPOLINE(0)   TRAMPOLINE(1)   TRAMPOLINE(2)   TRAMPOLINE(3)
TRAMPOLINE(4)   TRAMPOLINE(5)   TRAMPOLINE(6)   TRAMPOLINE(7)
TRAMPOLINE(8)   TRAMPOLINE(9)   TRAMPOLINE(10)  TRAMPOLINE(11)
TRAMPOLINE(12)  TRAMPOLINE(13)  TRAMPOLINE(14)  TRAMPOLINE(15)
TRAMPOLINE(16)  TRAMPOLINE(17)  TRAMPOLINE(18)  TRAMPOLINE(19)
TRAMPOLINE(20)  TRAMPOLINE(21)  TRAMPOLINE(22)  TRAMPOLINE(23)
TRAMPOLINE(24)  TRAMPOLINE(25)  TRAMPOLINE(26)  TRAMPOLINE(27)
TRAMPOLINE(28)  TRAMPOLINE(29)  TRAMPOLINE(30)  TRAMPOLINE(31)
TRAMPOLINE(32)  TRAMPOLINE(33)  TRAMPOLINE(34)  TRAMPOLINE(35)
TRAMPOLINE(36)  TRAMPOLINE(37)  TRAMPOLINE(38)  TRAMPOLINE(39)
TRAMPOLINE(40)  TRAMPOLINE(41)  TRAMPOLINE(42)  TRAMPOLINE(43)
TRAMPOLINE(44)  TRAMPOLINE(45)  TRAMPOLINE(46)  TRAMPOLINE(47)
TRAMPOLINE(48)  TRAMPOLINE(49)  TRAMPOLINE(50)  TRAMPOLINE(51)
TRAMPOLINE(52)  TRAMPOLINE(53)  TRAMPOLINE(54)  TRAMPOLINE(55)
TRAMPOLINE(56)  TRAMPOLINE(57)  TRAMPOLINE(58)  TRAMPOLINE(59)
TRAMPOLINE(60)  TRAMPOLINE(61)  TRAMPOLINE(62)  TRAMPOLINE(63)
TRAMPOLINE(64)  TRAMPOLINE(65)  TRAMPOLINE(66)  TRAMPOLINE(67)
TRAMPOLINE(68)  TRAMPOLINE(69)  TRAMPOLINE(70)  TRAMPOLINE(71)
TRAMPOLINE(72)  TRAMPOLINE(73)  TRAMPOLINE(74)  TRAMPOLINE(75)
TRAMPOLINE(76)  TRAMPOLINE(77)  TRAMPOLINE(78)  TRAMPOLINE(79)
TRAMPOLINE(80)  TRAMPOLINE(81)  TRAMPOLINE(82)  TRAMPOLINE(83)
TRAMPOLINE(84)  TRAMPOLINE(85)  TRAMPOLINE(86)  TRAMPOLINE(87)
TRAMPOLINE(88)  TRAMPOLINE(89)  TRAMPOLINE(90)  TRAMPOLINE(91)
TRAMPOLINE(92)  TRAMPOLINE(93)  TRAMPOLINE(94)  TRAMPOLINE(95)
TRAMPOLINE(96)  TRAMPOLINE(97)  TRAMPOLINE(98)  TRAMPOLINE(99)
TRAMPOLINE(100) TRAMPOLINE(101) TRAMPOLINE(102) TRAMPOLINE(103)
TRAMPOLINE(104) TRAMPOLINE(105) TRAMPOLINE(106) TRAMPOLINE(107)
TRAMPOLINE(108) TRAMPOLINE(109) TRAMPOLINE(110) TRAMPOLINE(111)
TRAMPOLINE(112) TRAMPOLINE(113) TRAMPOLINE(114) TRAMPOLINE(115)
TRAMPOLINE(116) TRAMPOLINE(117) TRAMPOLINE(118) TRAMPOLINE(119)
TRAMPOLINE(120) TRAMPOLINE(121) TRAMPOLINE(122) TRAMPOLINE(123)
TRAMPOLINE(124) TRAMPOLINE(125) TRAMPOLINE(126) TRAMPOLINE(127)

static SlpNativeFn trampoline_table[AGGRESSOR_MAX_TRAMPOLINES] = {
    trampoline_0,   trampoline_1,   trampoline_2,   trampoline_3,
    trampoline_4,   trampoline_5,   trampoline_6,   trampoline_7,
    trampoline_8,   trampoline_9,   trampoline_10,  trampoline_11,
    trampoline_12,  trampoline_13,  trampoline_14,  trampoline_15,
    trampoline_16,  trampoline_17,  trampoline_18,  trampoline_19,
    trampoline_20,  trampoline_21,  trampoline_22,  trampoline_23,
    trampoline_24,  trampoline_25,  trampoline_26,  trampoline_27,
    trampoline_28,  trampoline_29,  trampoline_30,  trampoline_31,
    trampoline_32,  trampoline_33,  trampoline_34,  trampoline_35,
    trampoline_36,  trampoline_37,  trampoline_38,  trampoline_39,
    trampoline_40,  trampoline_41,  trampoline_42,  trampoline_43,
    trampoline_44,  trampoline_45,  trampoline_46,  trampoline_47,
    trampoline_48,  trampoline_49,  trampoline_50,  trampoline_51,
    trampoline_52,  trampoline_53,  trampoline_54,  trampoline_55,
    trampoline_56,  trampoline_57,  trampoline_58,  trampoline_59,
    trampoline_60,  trampoline_61,  trampoline_62,  trampoline_63,
    trampoline_64,  trampoline_65,  trampoline_66,  trampoline_67,
    trampoline_68,  trampoline_69,  trampoline_70,  trampoline_71,
    trampoline_72,  trampoline_73,  trampoline_74,  trampoline_75,
    trampoline_76,  trampoline_77,  trampoline_78,  trampoline_79,
    trampoline_80,  trampoline_81,  trampoline_82,  trampoline_83,
    trampoline_84,  trampoline_85,  trampoline_86,  trampoline_87,
    trampoline_88,  trampoline_89,  trampoline_90,  trampoline_91,
    trampoline_92,  trampoline_93,  trampoline_94,  trampoline_95,
    trampoline_96,  trampoline_97,  trampoline_98,  trampoline_99,
    trampoline_100, trampoline_101, trampoline_102, trampoline_103,
    trampoline_104, trampoline_105, trampoline_106, trampoline_107,
    trampoline_108, trampoline_109, trampoline_110, trampoline_111,
    trampoline_112, trampoline_113, trampoline_114, trampoline_115,
    trampoline_116, trampoline_117, trampoline_118, trampoline_119,
    trampoline_120, trampoline_121, trampoline_122, trampoline_123,
    trampoline_124, trampoline_125, trampoline_126, trampoline_127,
};

/*
 * Register a dispatched function. Allocates a trampoline slot and wires
 * it into the VM's native table.
 */
static void register_dispatched(SlpVM *vm, AggressorState *state, const char *name) {
    if (state->trampoline_count >= AGGRESSOR_MAX_TRAMPOLINES) {
        fprintf(stderr, "[aggressor] ERROR: trampoline table full (%d max)\n",
                AGGRESSOR_MAX_TRAMPOLINES);
        return;
    }
    int slot = state->trampoline_count++;
    strncpy(state->trampoline_names[slot].name, name, 63);
    state->trampoline_names[slot].name[63] = '\0';
    slp_vm_register_native(vm, name, trampoline_table[slot]);
}

/* -----------------------------------------------------------------------
 * Registration: Dispatched functions (go through override/fallback)
 * ----------------------------------------------------------------------- */

/* Functions that are NOT in the builtins list above get dispatched */
static const char *dispatched_functions[] = {
    /* Beacon operations */
    "beacon_inline_execute",
    "bof_pack",
    "btask",
    "blog",
    "berror",
    "barch",
    "binfo",
    "beacon_info",
    "beacons",
    "beacon_ids",
    "beacon_info_set",
    "beacon_command_register",
    "beacon_command_detail",
    "bupload_raw",
    "bgetprivs",
    "bwrite",
    "bls",
    "bdata",
    "bshell",
    "bexecute_assembly",
    "bpowershell",

    /* File I/O */
    "openf",
    "readb",
    "closef",
    "writeb",
    "deleteFile",
    "lof",
    "script_resource",
    "readAll",

    /* UI */
    "prompt_file_open",
    "show_message",
    "dialog",
    "dialog_show",
    "drow",
    "drow_text",
    "dbutton",
    "dcheckbox",
    "dtext",
    "dtextarea",
    "dcombobox",
    "dlist",
    "dfile",

    /* Crypto */
    "ohash",

    /* Misc utilities that need dispatch (not pure) */
    "sleep",
    "charAt",
    "byteAt",
    "pack",
    "unpack",
    "parseNumber",
    "mynick",
    "left",

    NULL  /* sentinel */
};

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

AggressorState *aggressor_init(SlpVM *vm, AggressorConfig *config) {
    AggressorState *state = malloc(sizeof(AggressorState));
    memset(state, 0, sizeof(AggressorState));

    /* Initialize default mock beacons */
    state->mock_beacon_count = 3;

    /* Beacon 12345 */
    MockBeacon *b1 = &state->mock_beacons[0];
    b1->property_count = 7;
    strcpy(b1->properties[0].key, "id"); strcpy(b1->properties[0].value, "12345");
    strcpy(b1->properties[1].key, "user"); strcpy(b1->properties[1].value, "SYSTEM");
    strcpy(b1->properties[2].key, "computer"); strcpy(b1->properties[2].value, "WIN-WORKSTATION");
    strcpy(b1->properties[3].key, "isadmin"); strcpy(b1->properties[3].value, "1");
    strcpy(b1->properties[4].key, "barch"); strcpy(b1->properties[4].value, "x64");
    strcpy(b1->properties[5].key, "os"); strcpy(b1->properties[5].value, "Windows");
    strcpy(b1->properties[6].key, "host"); strcpy(b1->properties[6].value, "192.168.1.50");

    /* Beacon 67890 */
    MockBeacon *b2 = &state->mock_beacons[1];
    b2->property_count = 7;
    strcpy(b2->properties[0].key, "id"); strcpy(b2->properties[0].value, "67890");
    strcpy(b2->properties[1].key, "user"); strcpy(b2->properties[1].value, "jdoe");
    strcpy(b2->properties[2].key, "computer"); strcpy(b2->properties[2].value, "HR-PC");
    strcpy(b2->properties[3].key, "isadmin"); strcpy(b2->properties[3].value, "0");
    strcpy(b2->properties[4].key, "barch"); strcpy(b2->properties[4].value, "x86");
    strcpy(b2->properties[5].key, "os"); strcpy(b2->properties[5].value, "Windows");
    strcpy(b2->properties[6].key, "host"); strcpy(b2->properties[6].value, "192.168.1.112");

    /* Beacon 11111 */
    MockBeacon *b3 = &state->mock_beacons[2];
    b3->property_count = 7;
    strcpy(b3->properties[0].key, "id"); strcpy(b3->properties[0].value, "11111");
    strcpy(b3->properties[1].key, "user"); strcpy(b3->properties[1].value, "root");
    strcpy(b3->properties[2].key, "computer"); strcpy(b3->properties[2].value, "prod-web-01");
    strcpy(b3->properties[3].key, "isadmin"); strcpy(b3->properties[3].value, "1");
    strcpy(b3->properties[4].key, "barch"); strcpy(b3->properties[4].value, "x64");
    strcpy(b3->properties[5].key, "os"); strcpy(b3->properties[5].value, "Linux");
    strcpy(b3->properties[6].key, "host"); strcpy(b3->properties[6].value, "10.0.4.15");

    /* Store config */
    if (config) {
        state->config = *config;
    }

    /* Store state pointer in VM FFI slot 255 */
    slp_vm_ffi_set_long(vm, 255, (int64_t)(uintptr_t)state);

    /* Register all dispatched functions through trampolines */
    for (int i = 0; dispatched_functions[i] != NULL; i++) {
        register_dispatched(vm, state, dispatched_functions[i]);
    }

    return state;
}

void aggressor_set(AggressorState *state, const char *name, AggressorNativeFn fn) {
    if (!state) return;

    /* Check if already overridden */
    for (int i = 0; i < state->override_count; i++) {
        if (strcmp(state->overrides[i].name, name) == 0) {
            state->overrides[i].fn = fn;
            return;
        }
    }

    /* Add new override */
    if (state->override_count < AGGRESSOR_MAX_OVERRIDES) {
        strncpy(state->overrides[state->override_count].name, name, 63);
        state->overrides[state->override_count].name[63] = '\0';
        state->overrides[state->override_count].fn = fn;
        state->override_count++;
    }
}

AggressorConfig *aggressor_get_config(SlpVM *vm) {
    int64_t addr = slp_vm_ffi_get_long(vm, 255);
    AggressorState *state = (AggressorState *)(uintptr_t)addr;
    if (!state) return NULL;
    return &state->config;
}

/* -----------------------------------------------------------------------
 * Category-Based Overrides
 * ----------------------------------------------------------------------- */

void aggressor_set_beacon_ops(SlpVM *vm, AggressorBeaconOps *ops) {
    if (!ops) return;
    int64_t addr = slp_vm_ffi_get_long(vm, 255);
    AggressorState *state = (AggressorState *)(uintptr_t)addr;
    if (!state) return;

    if (ops->beacon_inline_execute) aggressor_set(state, "beacon_inline_execute", ops->beacon_inline_execute);
    if (ops->bof_pack)              aggressor_set(state, "bof_pack",              ops->bof_pack);
    if (ops->btask)                 aggressor_set(state, "btask",                 ops->btask);
    if (ops->blog)                  aggressor_set(state, "blog",                  ops->blog);
    if (ops->berror)                aggressor_set(state, "berror",                ops->berror);
    if (ops->barch)                 aggressor_set(state, "barch",                 ops->barch);
    if (ops->binfo)                 aggressor_set(state, "binfo",                 ops->binfo);
    if (ops->beacon_info)           aggressor_set(state, "beacon_info",           ops->beacon_info);
    if (ops->beacons)               aggressor_set(state, "beacons",               ops->beacons);
    if (ops->beacon_ids)            aggressor_set(state, "beacon_ids",            ops->beacon_ids);
    if (ops->beacon_command_register) aggressor_set(state, "beacon_command_register", ops->beacon_command_register);
    if (ops->beacon_command_detail) aggressor_set(state, "beacon_command_detail",  ops->beacon_command_detail);
    if (ops->bupload_raw)           aggressor_set(state, "bupload_raw",           ops->bupload_raw);
    if (ops->bgetprivs)             aggressor_set(state, "bgetprivs",             ops->bgetprivs);
    if (ops->bwrite)                aggressor_set(state, "bwrite",                ops->bwrite);
    if (ops->bls)                   aggressor_set(state, "bls",                   ops->bls);
    if (ops->bshell)                aggressor_set(state, "bshell",                ops->bshell);
    if (ops->bexecute_assembly)     aggressor_set(state, "bexecute_assembly",     ops->bexecute_assembly);
    if (ops->bpowershell)           aggressor_set(state, "bpowershell",           ops->bpowershell);
}

void aggressor_set_file_ops(SlpVM *vm, AggressorFileOps *ops) {
    if (!ops) return;
    int64_t addr = slp_vm_ffi_get_long(vm, 255);
    AggressorState *state = (AggressorState *)(uintptr_t)addr;
    if (!state) return;

    if (ops->openf)           aggressor_set(state, "openf",           ops->openf);
    if (ops->readb)           aggressor_set(state, "readb",           ops->readb);
    if (ops->closef)          aggressor_set(state, "closef",          ops->closef);
    if (ops->writeb)          aggressor_set(state, "writeb",          ops->writeb);
    if (ops->deleteFile)      aggressor_set(state, "deleteFile",      ops->deleteFile);
    if (ops->lof)             aggressor_set(state, "lof",             ops->lof);
    if (ops->script_resource) aggressor_set(state, "script_resource", ops->script_resource);
}

void aggressor_set_bridge_ops(SlpVM *vm, AggressorBridgeOps *ops) {
    if (!ops) return;
    int64_t addr = slp_vm_ffi_get_long(vm, 255);
    AggressorState *state = (AggressorState *)(uintptr_t)addr;
    if (!state) return;

    if (ops->alias_handler)
        slp_vm_register_bridge_type(vm, "alias", ops->alias_handler, state->config.user_data);
    if (ops->on_handler)
        slp_vm_register_bridge_type(vm, "on", ops->on_handler, state->config.user_data);
    if (ops->popup_handler)
        slp_vm_register_bridge_type(vm, "popup", ops->popup_handler, state->config.user_data);
}
