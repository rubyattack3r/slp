/*
 * aggressor_funcs.h - Master registry of all known Aggressor Script functions
 *
 * This file uses X-macros to define the complete list of functions that
 * Aggressor Script expects. Each entry specifies:
 *   - The function name (as it appears in .cna scripts)
 *   - The category (for grouping and documentation)
 *
 * Categories:
 *   BEACON   - Beacon interaction (btask, blog, berror, barch, ...)
 *   FILE_IO  - File operations (openf, readb, closef, ...)
 *   UTIL     - Pure language utilities (iff, strlen, substr, ...)
 *              These have real implementations, not stubs.
 *   UI       - GUI/interaction (prompt_file_open, show_message, ...)
 *   CRYPTO   - Hashing and crypto (ohash, ...)
 *   NET      - Network operations (...)
 *   MISC     - Everything else
 */

#ifndef AGGRESSOR_FUNCS_H
#define AGGRESSOR_FUNCS_H

/*
 * AGGRESSOR_FUNC(name, category)
 *
 * Define this macro before including this file to iterate over all functions.
 */

#ifndef AGGRESSOR_FUNC
#define AGGRESSOR_FUNC(name, category)
#endif

/* ---- Beacon Operations ---- */
AGGRESSOR_FUNC("beacon_inline_execute", BEACON)
AGGRESSOR_FUNC("bof_pack",              BEACON)
AGGRESSOR_FUNC("btask",                 BEACON)
AGGRESSOR_FUNC("blog",                  BEACON)
AGGRESSOR_FUNC("berror",                BEACON)
AGGRESSOR_FUNC("barch",                 BEACON)
AGGRESSOR_FUNC("binfo",                 BEACON)
AGGRESSOR_FUNC("beacon_info",           BEACON)
AGGRESSOR_FUNC("beacon_command_register", BEACON)
AGGRESSOR_FUNC("beacon_command_detail",  BEACON)
AGGRESSOR_FUNC("bupload_raw",           BEACON)
AGGRESSOR_FUNC("bgetprivs",             BEACON)
AGGRESSOR_FUNC("bwrite",                BEACON)
AGGRESSOR_FUNC("bls",                   BEACON)
AGGRESSOR_FUNC("bdata",                 BEACON)
AGGRESSOR_FUNC("bmenubar",              BEACON)
AGGRESSOR_FUNC("bshell",                BEACON)
AGGRESSOR_FUNC("bexecute_assembly",     BEACON)
AGGRESSOR_FUNC("bpowershell",           BEACON)

/* ---- File I/O ---- */
AGGRESSOR_FUNC("openf",                 FILE_IO)
AGGRESSOR_FUNC("readb",                 FILE_IO)
AGGRESSOR_FUNC("closef",                FILE_IO)
AGGRESSOR_FUNC("writeb",                FILE_IO)
AGGRESSOR_FUNC("deleteFile",            FILE_IO)
AGGRESSOR_FUNC("lof",                   FILE_IO)
AGGRESSOR_FUNC("script_resource",       FILE_IO)
AGGRESSOR_FUNC("readAll",               FILE_IO)

/* ---- Pure Language Utilities (have real implementations) ---- */
AGGRESSOR_FUNC("iff",                   UTIL)
AGGRESSOR_FUNC("-istrue",               UTIL)
AGGRESSOR_FUNC("strlen",                UTIL)
AGGRESSOR_FUNC("substr",                UTIL)
AGGRESSOR_FUNC("split",                 UTIL)
AGGRESSOR_FUNC("join",                  UTIL)
AGGRESSOR_FUNC("replace",               UTIL)
AGGRESSOR_FUNC("strrep",                UTIL)
AGGRESSOR_FUNC("lc",                    UTIL)
AGGRESSOR_FUNC("uc",                    UTIL)
AGGRESSOR_FUNC("left",                  UTIL)
AGGRESSOR_FUNC("int",                   UTIL)
AGGRESSOR_FUNC("size",                  UTIL)
AGGRESSOR_FUNC("local",                 UTIL)
AGGRESSOR_FUNC("print",                 UTIL)
AGGRESSOR_FUNC("println",               UTIL)
AGGRESSOR_FUNC("ticks",                 UTIL)
AGGRESSOR_FUNC("rand",                  UTIL)
AGGRESSOR_FUNC("sleep",                 UTIL)
AGGRESSOR_FUNC("keys",                  UTIL)
AGGRESSOR_FUNC("values",                UTIL)
AGGRESSOR_FUNC("charAt",                UTIL)
AGGRESSOR_FUNC("byteAt",                UTIL)
AGGRESSOR_FUNC("pack",                  UTIL)
AGGRESSOR_FUNC("unpack",                UTIL)
AGGRESSOR_FUNC("parseNumber",           UTIL)
AGGRESSOR_FUNC("tstamp",                UTIL)
AGGRESSOR_FUNC("mynick",                UTIL)

/* ---- UI / Interaction ---- */
AGGRESSOR_FUNC("prompt_file_open",      UI)
AGGRESSOR_FUNC("show_message",          UI)

/* ---- Crypto / Hashing ---- */
AGGRESSOR_FUNC("ohash",                 CRYPTO)

#undef AGGRESSOR_FUNC

#endif /* AGGRESSOR_FUNCS_H */
