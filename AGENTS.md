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
- Keep extensions (`extensions/`) separate from the core. They register native
  functions into a `SlpVM` after construction; do not fold extension code into
  the core library.

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

- `include/`: Public C headers for the VM, embedding API, lexer, parser,
  compiler, values, bytecode, GC, disassembler, AST, and utilities.
- `src/`: Core implementation — lexer, parser, AST, compiler, bytecode
  emitter, chunk storage, value representation, GC, VM dispatch, disassembler,
  utilities.
- `extensions/stdlib/`: Standard library implementation and header
  (`stdlib.c`, `slp_stdlib.h`; functions such as `println`).
- `tools/slp.c`: Main CLI interpreter and interactive REPL.
- `tools/slp_fmt.c`: AST-based code formatter (`slp_fmt`).
- `tools/slpd.c`: Bytecode disassembler (`slpd`).
- `tools/bench_slp.c`: Performance benchmarking tool.
- `tools/common/`: Shared CLI utilities (e.g. `utils.c`, `console.c`).
- `deps/bestline/`: Third-party line-editing library (used by REPL).
- `tests/`: C++ unit tests using `doctest.h` against the C API, Sleep fixtures
  in `tests/fixtures/` and `tests/fixtures_vm/`, reference outputs in
  `tests/reference_output/`, and the compatibility ledger
  `tests/reference_unverified.tsv`.
- `scripts/`: Build helpers (amalgamation, etc.).

## Building

```bash
make                # Build the core library, interpreter, and tools
make slp            # Build just the interpreter
make test           # Build and run the unit test suite
make test-amalgamation # Run generated-library compile and execution checks
make format-check   # Check fixture formatting
make bench          # Build and run benchmarks
make amalgamate     # Generate copyable header/source files
make amalgamate-single # Generate single-header integration files
make debug          # Build with UBSan and run tests
make debug-asan     # Build with UBSan+ASan (Linux only)
make clean          # Remove all build artifacts
```

Cross-compile for Windows via Nix + Zig:

```bash
nix develop -c make clean slp CC="zig cc -target x86_64-windows" CXX="zig c++ -target x86_64-windows"
```

## Testing

Use `make test` to build and run the full C++ unit test suite and the
amalgamation checks. Tests exercise the lexer, parser, AST, compiler, VM, GC,
values, standard library, embedding API, formatter, disassembler, and fixture
compatibility. Reference-output status is tracked in
`tests/reference_unverified.tsv`. Use `make format-check` to validate fixture
formatting and `make bench` for performance validation.
