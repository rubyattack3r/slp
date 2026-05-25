/*
 * aggressor.h - Aggressor Script Extension for the Slp VM
 *
 * This library provides a generic framework for registering all known
 * Aggressor Script native functions into a Slp VM instance. The actual
 * behavior of each function is injected by the consumer through:
 *
 *   1. A fallback handler (called for any function without an explicit override)
 *   2. Per-function overrides via aggressor_set()
 *   3. Category-based bulk registration via aggressor_set_*_ops()
 *
 * This allows the same library to power validators, REPLs, emulators, and
 * real C2 integrations without any code duplication.
 *
 * Usage:
 *   AggressorConfig cfg = {
 *       .fallback  = my_fallback,
 *       .user_data = &my_state,
 *   };
 *   aggressor_new(vm, &cfg);
 *   aggressor_set(vm, "openf", my_custom_openf);
 */

#ifndef AGGRESSOR_H
#define AGGRESSOR_H

#include "slp_vm.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Core Types
 * ----------------------------------------------------------------------- */

/*
 * Fallback handler signature. Called for any Aggressor function that has
 * not been explicitly overridden. The `func_name` parameter identifies
 * which function was called, allowing consumers to log, stub, or error.
 */
typedef SlpValue (*AggressorFallbackFn)(
    SlpVM   *vm,
    const char *func_name,
    SlpValue *args,
    int          argc,
    void        *user_data
);

/*
 * Extended native function signature. Unlike SlpNativeFn, this carries
 * the user_data pointer so consumers don't need global state.
 */
typedef SlpValue (*AggressorNativeFn)(
    SlpVM    *vm,
    SlpValue *args,
    int          argc,
    void        *user_data
);

/*
 * Bridge handler for keyword registrations (alias, on, popup, etc.).
 */
typedef void (*AggressorBridgeFn)(
    SlpVM         *vm,
    const char       *keyword,
    const char       *name,
    const char       *str,
    SlpObjClosure *closure,
    void             *user_data
);

/* -----------------------------------------------------------------------
 * Configuration
 * ----------------------------------------------------------------------- */

typedef struct {
    /*
     * Called for any registered Aggressor function that hasn't been
     * explicitly overridden. If NULL, a safe default stub is used
     * (returns SLP_NULL_VAL silently).
     */
    AggressorFallbackFn fallback;

    /*
     * Opaque pointer passed to every handler (fallback, overrides,
     * and bridge handlers). Consumers use this for their own state
     * (e.g., a validator context, a team server handle, etc.).
     */
    void *user_data;
} AggressorConfig;

/* -----------------------------------------------------------------------
 * Lifecycle
 * ----------------------------------------------------------------------- */

typedef struct AggressorState AggressorState;

/*
 * Dynamic metadata structure for a registered beacon command.
 */
typedef struct {
    char *name;
    char *description;
    char *help;
} AggressorCommand;

/*
 * Initialize the Aggressor extension. Registers all known function names
 * into the VM using safe default stubs. Functions that are pure language
 * utilities (iff, strlen, substr, etc.) are registered with real
 * implementations. Everything else routes through the fallback.
 *
 * Call this ONCE after slp_vm_new().
 */
AggressorState *aggressor_new(SlpVM *vm, AggressorConfig *config);

/*
 * Deinitialize the Aggressor extension. Fully reclaims the state along with
 * all dynamically registered commands and metadata to prevent memory leaks.
 */
void aggressor_free(AggressorState *state);

/*
 * Override a single function after init. The provided function will be
 * called instead of the fallback for this specific function name.
 */
void aggressor_set(AggressorState *state, const char *name, AggressorNativeFn fn);

/*
 * Register a beacon command dynamically with descriptions and usage help.
 * If the command already exists, its description and help details are updated.
 * Automatically resizes the dynamic backing array as needed.
 */
void aggressor_register_command(AggressorState *state, const char *name, const char *description, const char *help);

/*
 * Query the registered usage help string for a beacon command.
 * Returns NULL if the command has not been registered.
 */
const char *aggressor_get_command_help(AggressorState *state, const char *name);

/* -----------------------------------------------------------------------
 * Category-Based Overrides
 *
 * Each struct groups related functions. Set individual fields to non-NULL
 * to override; NULL fields keep the current behavior (fallback or default).
 * ----------------------------------------------------------------------- */

/* Beacon interaction functions */
typedef struct {
    AggressorNativeFn beacon_inline_execute;
    AggressorNativeFn bof_pack;
    AggressorNativeFn btask;
    AggressorNativeFn blog;
    AggressorNativeFn berror;
    AggressorNativeFn barch;
    AggressorNativeFn binfo;
    AggressorNativeFn beacon_info;
    AggressorNativeFn beacons;
    AggressorNativeFn beacon_ids;
    AggressorNativeFn beacon_command_register;
    AggressorNativeFn beacon_command_detail;
    AggressorNativeFn bupload_raw;
    AggressorNativeFn bgetprivs;
    AggressorNativeFn bwrite;
    AggressorNativeFn bls;
    AggressorNativeFn bshell;
    AggressorNativeFn bexecute_assembly;
    AggressorNativeFn bpowershell;
} AggressorBeaconOps;

void aggressor_set_beacon_ops(SlpVM *vm, AggressorBeaconOps *ops);

/* File I/O functions */
typedef struct {
    AggressorNativeFn openf;
    AggressorNativeFn readb;
    AggressorNativeFn closef;
    AggressorNativeFn writeb;
    AggressorNativeFn deleteFile;
    AggressorNativeFn lof;
    AggressorNativeFn script_resource;
} AggressorFileOps;

void aggressor_set_file_ops(SlpVM *vm, AggressorFileOps *ops);

/* Bridge handlers (alias, on, popup, etc.) */
typedef struct {
    AggressorBridgeFn alias_handler;
    AggressorBridgeFn on_handler;
    AggressorBridgeFn popup_handler;
} AggressorBridgeOps;

void aggressor_set_bridge_ops(SlpVM *vm, AggressorBridgeOps *ops);

/* -----------------------------------------------------------------------
 * Utility: Retrieve the config from inside a handler
 * ----------------------------------------------------------------------- */

/*
 * Check if a mock beacon ID exists. Returns true if it exists, false otherwise.
 */
bool aggressor_beacon_exists(AggressorState *state, const char *id);

/*
 * Create a new mock beacon with realistic defaults if it does not already exist.
 */
void aggressor_ensure_beacon(AggressorState *state, const char *id);

/*
 * Get the AggressorConfig associated with this VM. Returns NULL if
 * aggressor_new() hasn't been called yet.
 */
AggressorConfig *aggressor_get_config(SlpVM *vm);

#ifdef __cplusplus
}
#endif

#endif /* AGGRESSOR_H */
