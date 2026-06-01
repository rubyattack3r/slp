# Sleep C Implementation

This project is a complete, native C implementation of a parser, lexer, and stack-based VM for the [`sleep` programming language](https://sleep.dashnine.org/). Designed for performance, simplicity, and ease of embedding (similar to Lua or Wren), this implementation has zero external dependencies and enforces strict memory allocation limits.

## Design Constraints

1. **Zero Dependencies**: The core C implementation has absolutely zero external dependencies. It does not even rely on standard `malloc` and `free` implicitly.
2. **Allocator Approach**: All memory allocations are driven through a custom `SlpAllocator` which involves a `userdata` pointer and a user-provided reallocation function. This allocator must be passed around everywhere.
3. **Pure C99**: The codebase uses standard C99 structures, making it extremely portable and lightweight.

## Project Structure

- `include/`: Public headers for incorporating the Sleep C API into your applications.
- `src/`: Core implementation files for the Lexer, Parser, AST Compiler, and VM.
- `extensions/stdlib/`: Pure standard library extension functions (like `println`, string functions, etc.).
- `tools/`: Native command-line tools for running, formatting, and inspecting Sleep code.
- `tests/`: Unit tests written in C++ using `doctest.h` to verify the C implementation against standard test fixtures from the reference implementation.

## Building and Core Usage

You can build all the native core C tools using `make`:

```bash
make
```

This compiles the core static library (`libslp.a`) and the CLI interpreter (`slp`) in the `bin/` directory.

---

## Detailed Tool Suite Guide

The project contains a robust and highly specialized suite of native C-based tools for compiling, formatting, disassembling, and benchmarking Sleep scripts.

### 1. Main Script Interpreter & REPL (`slp`)
The `slp` tool is the primary CLI runner and interactive shell for the Sleep programming language.
- **How It Works**: It reads source code, runs the custom zero-dependency Lexer and LALR-like Parser to generate an Abstract Syntax Tree (AST), compiles the AST into VM bytecode, and executes it inside the stack-based SlpVM.
- **Interactive REPL**:
  ```bash
  ./bin/slp
  ```
- **Execute Script**:
  ```bash
  ./bin/slp path/to/script.sl
  ```

### 2. AST-Based Code Formatter (`slp_fmt`)
The `slp_fmt` tool parses a Sleep source script, normalizes its internal structure, and formats it cleanly back into formatted Sleep syntax.
- **How It Works**: It reads the input code, parses it into an AST, and passes the AST through a dedicated formatter (`slp_ast_format`) to reconstruct the source text with clean indentation and spacing.
- **Usage Options**:
  - Print formatted script to standard output:
    ```bash
    ./bin/slp_fmt path/to/script.sl
    ```
  - Format a script and write it back in-place:
    ```bash
    ./bin/slp_fmt -w path/to/script.sl
    ```

### 3. Bytecode Disassembler (`slpd`)
The `slpd` tool is a debugger and VM inspector that disassembles compiled Sleep code.
- **How It Works**: It parses and compiles source code into VM bytecode chunks, then runs `slp_disasm_chunk` recursively across all scopes and functions to print the exact SlpVM instructions (e.g. `OP_LOAD_VAL`, `OP_CALL`, `OP_RETURN`).
- **Usage**:
  ```bash
  ./bin/slpd path/to/script.sl
  ```

### 4. Performance Benchmarking Tool (`bench_slp`)
The `bench_slp` runs standard performance tests on the SlpVM core.
- **Usage**:
  ```bash
  ./bin/bench_slp
  ```

---

## CI/CD Pipeline Integration

The project uses GitHub Actions under `.github/workflows/ci.yml` to automate building all targets, running the full C++ unit test suite, and executing performance benchmarks natively on both **Ubuntu** and **macOS** runners.

## Testing and Benchmarks

Tests are written in C++ to leverage the `doctest.h` unit testing framework, but they test the C implementation. Test fixtures are located in `tests/fixtures/` and `tests/fixtures_vm/`.

To run all language unit tests:

```bash
make test
```

To build and run the performance benchmarks:

```bash
make bench
```

## License and Compliance

This repository uses a dual-licensing scheme to ensure compliance with the original Sleep project's license:

1. **Core C99 Implementation**: The parser, lexer, AST compiler, stack-based VM, and CLI tools (located in `src/`, `include/`, `extensions/`, and `tools/`) are built as an independent C99 implementation of the language. These components are licensed under the **MIT License** (see [LICENSE](LICENSE)).
2. **Test Suite Fixtures**: The regression test files (located in `tests/fixtures/` and `tests/fixtures_vm/`) are derived or copied directly from the reference Sleep programming language implementation (`jsleep`). These test files are licensed under the **Creative Commons Attribution-ShareAlike 3.0 United States License (CC BY-SA 3.0 US)** and are credited to the original author, **Raphael Mudge**. See [tests/LICENSE](file:///Users/operator/Documents/sleepy/tests/LICENSE) for more details.

