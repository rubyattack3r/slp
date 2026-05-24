# Sleep C Implementation

This project is a C implementation of a parser, lexer, and eventually a VM for the `sleep` programming language.

## Design Constraints

1. **Zero Dependencies**: The core C implementation has absolutely zero external dependencies. It does not even rely on standard `malloc` and `free` implicitly. 
2. **Allocator Approach**: All memory allocations are driven through a custom `SlpAllocator` which involves a `userdata` pointer and a user-provided reallocation function. This allocator must be passed around everywhere.

## Project Structure

- `include/`: Public headers for incorporating the Sleep C API into your applications.
- `src/`: Core implementation files for the Lexer, Parser, and VM.
- `tests/`: Unit tests written in C++ using `doctest.h` to verify the C implementation against the standard test fixtures from the reference implementation.

## Building and Core Usage

You can build all the native core C tools and Aggressor tooling using `make`:

```bash
make tools
```

This will compile and output all compiled binaries in the `bin/` directory.

---

## Detailed Tool Suite Guide

The project contains a robust and highly specialized suite of native C-based tools for compiling, formatting, disassembling, bundling, validating, and interacting with Sleep and Aggressor Scripts.

### 1. Main Script Interpreter & REPL (`slp`)
The `slp` tool is the primary CLI runner and interactive shell for the Sleep programming language.
- **How It Works**: It reads source code, runs the custom zero-dependency Lexer and LALR-like Parser to generate an Abstract Syntax Tree (AST), compiles the AST into dynamic VM bytecode, and executes it inside the stack-based SlpVM.
- **Build**:
  ```bash
  make slp
  ```
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
- **Build**:
  ```bash
  make slp_fmt
  ```
- **Usage Options**:
  - Print formatted script to standard output:
    ```bash
    ./bin/slp_fmt path/to/script.sl
    ```
  - Format a script and write it back in-place:
    ```bash
    ./bin/slp_fmt -w path/to/script.sl
    ```
  - Format a script and write it to a specific output file:
    ```bash
    ./bin/slp_fmt -o formatted_output.sl path/to/script.sl
    ```

### 3. Bytecode Disassembler (`slpd`)
The `slpd` tool is a debugger and VM inspector that disassembles compiled Sleep code.
- **How It Works**: It parses and compiles source code into VM bytecode chunks, then runs `slp_disasm_chunk` recursively across all scopes and functions to print the exact SlpVM instructions (e.g. `OP_LOAD_VAL`, `OP_CALL`, `OP_RETURN`) in a readable trace format.
- **Build**:
  ```bash
  make slpd
  ```
- **Usage**:
  ```bash
  ./bin/slpd path/to/script.sl
  ```

### 4. Multi-Project CNA Bundler (`cna_bundler`)
The `cna_bundler` consolidates complex, multi-project Cobalt Strike Aggressor Scripts (CNA) into a single, cohesive bundle.
- **How It Works**: It processes a target project directory recursively, inlines child scripts resolved through `include()` or `script_resource()` dynamically, sanitizes variables and subroutines to avoid cross-project namespace pollution, detects global alias name collisions, and applies conflict renaming configs.
- **Build**:
  ```bash
  make cna_bundler
  ```
- **Usage**:
  ```bash
  ./bin/cna_bundler <projects-dir> -o <output.cna> [--config <config-file>]
  ```
  - `--config`: Config file specifying mapping rules to resolve collisions (format: `ProjectName:old_alias=new_alias`).

### 5. Dry-Run Script Validator (`cna_validator`)
The `cna_validator` performs dry-run syntax and semantic simulation of a consolidated `.cna` bundle.
- **How It Works**: It spins up a sandboxed SlpVM, registers all Cobalt Strike Aggressor native FFI hooks via `libaggressor`, simulates dry-run execution, verifies that referenced payloads (like BOF `.o` files) exist on disk, and reports validation outcomes safely.
- **Build**:
  ```bash
  make cna_validator
  ```
- **Usage**:
  ```bash
  ./bin/cna_validator <bundle.cna>
  ```

### 6. Interactive Aggressor REPL (`aggressor`)
The `aggressor` tool is an interactive shell and Cobalt Strike beacon emulation console.
- **How It Works**: It loads a consolidated CNA script, registers Aggressor functions, and starts a **dual-mode shell** featuring context-aware tab completions and inline greyed-out completions (readline hints):
  - **Aggressor Mode (Default)**: Direct evaluation of arbitrary Sleep statements, supporting multi-line balanced brackets/braces/parentheses and beautiful, syntax-colored value output.
  - **Beacon Emulation Mode**: Entered by typing `interact <beacon_id>`. Automatically provisions a realistic mock beacon environment (matching `x64` arch, OS Windows, fake IP/host) and redirects console commands directly to Aggressor script alias invocations.
- **Build**:
  ```bash
  make aggressor
  ```
- **Usage**:
  ```bash
  ./bin/aggressor <bundle.cna>
  ```
  - Type `interact 123` to enter Beacon Console Mode for beacon `123`. The REPL will output:
    ```text
    [*] Interacting with beacon 123. Type 'back' to return.
    ```
    The prompt changes to `beacon 123> ` and accepts all alias commands directly, emulating Cobalt Strike's console.
  - Type `back` to return to Aggressor scripting mode.

### 7. Performance Benchmarking Tool (`bench_slp`)
The `bench_slp` runs standard performance tests on the SlpVM core.
- **Build**:
  ```bash
  make bench_slp
  ```
- **Usage**:
  ```bash
  ./bin/bench_slp
  ```

---

## Cross-Compiling for Windows

Because the BOF Loader relies on native Windows APIs to execute payloads in reality, you will likely want to build the tool suite for Windows. This project provides a fully provisioned cross-compilation environment using **Nix**.

If you have Nix installed, you can drop into a shell containing the MinGW-w64 cross-compilers by running `nix develop`. Because the `Makefile` respects the `CC` and `CXX` environment variables, cross-compiling is as simple as:

**For 64-bit Windows (x86_64):**
```bash
nix develop -c make clean tools CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++
```

**For 32-bit Windows (i686):**
```bash
nix develop -c make clean tools CC=i686-w64-mingw32-gcc CXX=i686-w64-mingw32-g++
```

*Note: The generated binaries in the `bin/` directory will be standard Windows PE executables, even though they will lack the `.exe` extension on your host OS. You may wish to rename them before moving them to a Windows target.*

---

## CI/CD Pipeline Integration

The project uses `make` and `bin/cna_bundler` directly to build and bundle projects:
```bash
make tools
./bin/cna_bundler ./third_party -o ./dist/packaged_tools.cna
```

Two complete GitHub Actions workflows are located under `.github/workflows/`:
1. **C Core CI (`c-core-ci.yml`)**: Automates building all targets, running the full C++ unit test suite, and executing performance benchmarks natively on both **Ubuntu** and **macOS** runners.
2. **Create Release Suite (`release.yml`)**: Provisions the development environment using **Nix**, compiles native production binaries for Linux and macOS Universal, cross-compiles Windows binaries via MinGW, packages them into structured platform archives, and drafts a GitHub Release with the compiled assets.

---

## Testing and Benchmarks

Tests are written in C++ to leverage the `doctest.h` unit testing framework, but they test the C implementation. Test fixtures are located in `tests/fixtures/`.

To run all language and `libaggressor` unit tests:

```bash
make test
```

To build and run the performance benchmarks:

```bash
make bench
```

