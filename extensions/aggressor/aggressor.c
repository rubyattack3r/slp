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

struct AggressorState {
    AggressorConfig config;
    AggressorOverride overrides[AGGRESSOR_MAX_OVERRIDES];
    int override_count;
    TrampolineSlot trampoline_names[AGGRESSOR_MAX_TRAMPOLINES];
    int trampoline_count;
};

static void *aggressor_alloc(SlpVM *vm, size_t size) {
    return vm->allocator->reallocate(NULL, size, vm->allocator->user_data);
}

static void aggressor_free(SlpVM *vm, void *ptr) {
    vm->allocator->reallocate(ptr, 0, vm->allocator->user_data);
}

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

/* Dispatch logic shared by all trampolines */
static SlpValue dispatch(SlpVM *vm, AggressorState *state, const char *name,
                             SlpValue *args, int argc) {
    /* Check for explicit override first */
    AggressorNativeFn override = find_override(state, name);
    if (override) {
        return override(vm, args, argc, state->config.user_data);
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
 * Built-in Utility Implementations
 * ----------------------------------------------------------------------- */

static SlpValue builtin_iff(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 3) return SLP_NULL_VAL;
    if (SLP_IS_TRUE(args[0])) return args[1];
    return args[2];
}

static SlpValue builtin_istrue(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(SLP_IS_TRUE(args[0]) ? 1 : 0);
}

static SlpValue builtin_strlen(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    if (!SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NUM_VAL(0);
    return SLP_NUM_VAL((double)SLP_AS_STRING(args[0])->length);
}

static SlpValue builtin_int(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    if (!SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL((double)(int)SLP_AS_NUM(args[0]));
}

static SlpValue builtin_size(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    /* If it's an array, return the actual length */
    if (SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_ARRAY) {
        return SLP_NUM_VAL((double)SLP_AS_ARRAY(args[0])->count);
    }
    /* If it's a string, return string length */
    if (SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
        return SLP_NUM_VAL((double)SLP_AS_STRING(args[0])->length);
    }
    return SLP_NUM_VAL(0);
}

static SlpValue builtin_local(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
    return SLP_NULL_VAL;
}

static SlpValue builtin_print(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
    return SLP_NULL_VAL;
}

static SlpValue builtin_println(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc >= 1 && SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
        printf("%s\n", SLP_AS_STRING(args[0])->chars);
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_ticks(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
    return SLP_NUM_VAL(0);
}

static SlpValue builtin_substr(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2) return SLP_NULL_VAL;
    if (!SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    int start = SLP_IS_NUM(args[1]) ? (int)SLP_AS_NUM(args[1]) : 0;
    int end = (argc >= 3 && SLP_IS_NUM(args[2]))
              ? (int)SLP_AS_NUM(args[2])
              : (int)str->length;
    if (start < 0) start = 0;
    if (end > (int)str->length) end = (int)str->length;
    if (start >= end) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "", 0));
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, str->chars + start, end - start));
}

static SlpValue builtin_lc(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    char *buf = aggressor_alloc(vm, str->length + 1);
    for (uint32_t i = 0; i < str->length; i++) buf[i] = (char)tolower(str->chars[i]);
    buf[str->length] = '\0';
    SlpObjString *result = slp_vm_copy_string(vm, buf, str->length);
    aggressor_free(vm, buf);
    return SLP_OBJ_VAL(result);
}

static SlpValue builtin_uc(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    char *buf = aggressor_alloc(vm, str->length + 1);
    for (uint32_t i = 0; i < str->length; i++) buf[i] = (char)toupper(str->chars[i]);
    buf[str->length] = '\0';
    SlpObjString *result = slp_vm_copy_string(vm, buf, str->length);
    aggressor_free(vm, buf);
    return SLP_OBJ_VAL(result);
}

static SlpValue builtin_replace(SlpVM *vm, SlpValue *args, int argc) {
    /* replace(string, old, new) */
    if (argc < 3) return SLP_NULL_VAL;
    if (!SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING) return args[0];
    if (!SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING) return args[0];
    if (!SLP_IS_OBJ(args[2]) || SLP_OBJ_TYPE(args[2]) != SLP_OBJ_STRING) return args[0];
    SlpObjString *src = SLP_AS_STRING(args[0]);
    SlpObjString *old = SLP_AS_STRING(args[1]);
    SlpObjString *rep = SLP_AS_STRING(args[2]);
    if (old->length == 0) return args[0];

    /* Count occurrences to allocate result */
    int count = 0;
    const char *p = src->chars;
    while ((p = strstr(p, old->chars)) != NULL) { count++; p += old->length; }
    if (count == 0) return args[0];

    uint32_t new_len = src->length + (uint32_t)count * (rep->length - old->length);
    char *buf = aggressor_alloc(vm, new_len + 1);
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
    aggressor_free(vm, buf);
    return SLP_OBJ_VAL(result);
}

/* -----------------------------------------------------------------------
 * Registration: Built-in utilities
 * ----------------------------------------------------------------------- */

static void register_builtins(SlpVM *vm) {
    slp_vm_register_native(vm, "iff",     builtin_iff);
    slp_vm_register_native(vm, "-istrue", builtin_istrue);
    slp_vm_register_native(vm, "strlen",  builtin_strlen);
    slp_vm_register_native(vm, "int",     builtin_int);
    slp_vm_register_native(vm, "size",    builtin_size);
    slp_vm_register_native(vm, "local",   builtin_local);
    slp_vm_register_native(vm, "print",   builtin_print);
    slp_vm_register_native(vm, "println", builtin_println);
    slp_vm_register_native(vm, "ticks",   builtin_ticks);
    slp_vm_register_native(vm, "substr",  builtin_substr);
    slp_vm_register_native(vm, "lc",      builtin_lc);
    slp_vm_register_native(vm, "uc",      builtin_uc);
    slp_vm_register_native(vm, "replace", builtin_replace);
    slp_vm_register_native(vm, "strrep",  builtin_replace);  /* alias */
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

    /* Crypto */
    "ohash",

    /* Misc utilities that need dispatch (not pure) */
    "sleep",
    "rand",
    "keys",
    "values",
    "charAt",
    "byteAt",
    "pack",
    "unpack",
    "parseNumber",
    "tstamp",
    "mynick",
    "split",
    "join",
    "left",

    NULL  /* sentinel */
};

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

AggressorState *aggressor_init(SlpVM *vm, AggressorConfig *config) {
    AggressorState *state = malloc(sizeof(AggressorState));
    memset(state, 0, sizeof(AggressorState));

    /* Store config */
    if (config) {
        state->config = *config;
    }

    /* Store state pointer in VM FFI slot 255 */
    slp_vm_ffi_set_long(vm, 255, (int64_t)(uintptr_t)state);

    /* Register built-in pure utilities (always the same) */
    register_builtins(vm);

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
