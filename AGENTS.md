# Agent Notes

This is a native C implementation of the Sleep programming language: a lexer,
LALR-like parser producing an AST, a compiler emitting bytecode chunks, and a
stack-based VM that executes them.

## Goals

- Keep the core (`src/`, `include/`) a zero-dependency C99 library. All memory
  goes through `SlpAllocator`; never call `malloc`/`free` directly.
- Keep the public API narrow. Tools and embedders should not reach into
  internal structs behind `include/`.
- Preserve correctness before speed. Do not keep a faster path with
  unexplained value, GC, or bytecode drift.
- Keep extensions (`extensions/`) as separate static libraries that register
  native functions into a `SlpVM` after construction. Do not fold them into
  the core.

## Quality Rules

- Comment important code where parser mechanics, GC lifetime, bytecode
  semantics, or VM dispatch are not obvious from the local code.
- Prefer comments beside the implementation over separate design documents.
- Keep comments instructive and compact: explain why an ordering, boundary, or
  memory choice exists.
- Do not introduce C++ into the core or extensions. C++ is only acceptable in
  `tests/` where `doctest.h` requires it.
- Do not add permanent feature variants behind flags. Diagnostic switches are
  fine when they validate the one release path.
- All new public functions must accept an `SlpAllocator *` as their first
  parameter (or be reachable from one that does). No bare stdlib allocation.

## Safety

- ASan deadlocks on recent macOS at init due to dyld shadow-memory recursion
  on Apple Silicon. Run `BUILD=debug-asan` only on Linux CI.
- The `Makefile` warning baseline (`-Wconversion`, `-Wdouble-promotion`) is
  intentional. Do not widen or suppress without reason.

## Layout

- `include/`: Public C headers (`slp_vm.h`, `slp_lexer.h`, `slp_parser.h`,
  `slp_compiler.h`, `slp_value.h`, `slp_chunk.h`, `slp_opcodes.h`,
  `slp_gc.h`, `slp_disasm.h`, `slp_ast.h`, `slp_common.h`, etc.).
- `src/`: Core implementation — lexer, parser, AST, compiler, bytecode
  emitter, chunk storage, value representation, GC, VM dispatch, disassembler,
  utilities.
- `extensions/stdlib/`: `libslp_stdlib` — standard library functions
  (`println`, etc.).
- `tools/slp.c`: Main CLI interpreter and interactive REPL.
- `tools/slp_fmt.c`: AST-based code formatter (`slp_fmt`).
- `tools/slpd.c`: Bytecode disassembler (`slpd`).
- `tools/bench_slp.c`: Performance benchmarking tool.
- `tools/common/`: Shared CLI utilities (e.g. `utils.c`, `console.c`).
- `deps/bestline/`: Third-party line-editing library (used by REPL).
- `tests/`: C++ unit tests using `doctest.h` against the C API. Test fixtures
  in `tests/fixtures/` and `tests/fixtures_vm/`.
- `scripts/`: Build helpers (amalgamation, etc.).

## Building

```bash
make                # Build core library and slp interpreter
make slp            # Build just the interpreter
make test           # Build and run the unit test suite
make bench          # Build and run benchmarks
make debug          # Build with UBSan and run tests
make debug-asan     # Build with UBSan+ASan (Linux only)
make clean          # Remove all build artifacts
```

Cross-compile for Windows via Nix + Zig:

```bash
nix develop -c make clean slp CC="zig cc -target x86_64-windows" CXX="zig c++ -target x86_64-windows"
```

## Testing

Use `make test` to build and run the full C++ unit test suite. Tests exercise
the lexer, parser, AST, compiler, VM, GC, values, and disassembler against
fixture scripts from the reference Sleep implementation. Use `make bench`
for performance validation.
