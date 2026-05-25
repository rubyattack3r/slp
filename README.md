# Sleep C Implementation

This project is a complete, native C implementation of a parser, lexer, and stack-based VM for the [`sleep` programming language](https://sleep.dashnine.org/).

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
The `aggressor` tool is an interactive shell and Cobalt Strike beacon emulation console with a built-in cross-platform BOF (Beacon Object File) loader.
- **How It Works**: It loads a consolidated CNA script, registers Aggressor functions, and starts a **dual-mode shell** featuring context-aware tab completions and inline greyed-out completions (readline hints):
  - **Aggressor Mode (Default)**: Direct evaluation of arbitrary Sleep statements, supporting multi-line balanced brackets/braces/parentheses and beautiful, syntax-colored value output.
  - **Beacon Emulation Mode**: Entered by typing `interact <beacon_id>`. Automatically provisions a realistic mock beacon environment (matching `x64` arch, OS Windows, fake IP/host) and redirects console commands directly to Aggressor script alias invocations. If an alias uses `beacon_inline_execute`, the built-in BOF loader executes the payload (natively on Windows, or mock-dry-run on POSIX).
- **Build**:
  ```bash
  make aggressor
  ```
- **Interactive Usage**:
  ```bash
  ./bin/aggressor <bundle.cna>
  ```
  - Type `interact 123` to enter Beacon Console Mode for beacon `123`. The REPL will output:
    ```text
    [*] Interacting with beacon 123. Type 'back' to return.
    ```
    The prompt changes to `beacon 123> ` and accepts all alias commands directly, emulating Cobalt Strike's console.
  - Type `back` to return to Aggressor scripting mode.

- **Non-Interactive (Headless) Usage**:
  You can execute specific beacon commands directly from the command line, supplying trailing arguments to the script via `@ARGV`.
  ```bash
  # Execute a single beacon alias command (-i) against a mock beacon and exit
  ./bin/aggressor bundle.cna -i "whoami"

  # Execute a raw Sleep statement (-c) and exit
  ./bin/aggressor bundle.cna -c "println('hello');"

  # Increase verbosity (-v or -vv) to see task logs and internal DISPATCH traces
  ./bin/aggressor bundle.cna -v -i "run args"

  # Pass trailing command line arguments to the script inside the @ARGV array
  ./bin/aggressor bundle.cna -i "whoami" -- arg1 arg2
  ```

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
2. **Rolling Build (`rolling.yml`)**: Provisions the development environment using **Nix**, compiles native production binaries for Linux and macOS Universal, cross-compiles Windows binaries via MinGW, packages them into structured platform archives, and continuously updates a `rolling` GitHub Release containing the bleeding-edge compiled assets on every push to the `master` branch.

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

## Embedding the Library

The `sleepy` VM is designed to be easily embedded directly into your own C/C++ applications. It requires you to define a custom allocator for zero-dependency memory management. 

### Basic Embedding

Here is a minimal example demonstrating how to initialize the VM, load the standard library, and evaluate a script:

```c
#include <stdlib.h>
#include "slp_common.h"
#include "slp_vm.h"
#include "slp_stdlib.h"

// 1. Define a basic reallocation-based allocator
static void *my_alloc(void *ptr, size_t size, void *ud) {
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}

int main() {
    SlpAllocator allocator = {my_alloc, NULL};

    // 2. Create a VM instance
    SlpVM *vm = slp_vm_new(&allocator);
    
    // 3. Load standard library functions (println, etc.)
    slp_stdlib_init(vm);
    
    // 4. Evaluate your Sleep script
    const char *source = "println(\"Hello from Embedded Sleep!\");";
    SlpResult res = slp_vm_interpret(vm, source);
    
    // 5. Clean up
    slp_vm_free(vm);
    return res == SLP_OK ? 0 : 1;
}
```

### Embedding with `libaggressor`

If you are evaluating Cobalt Strike Aggressor Scripts (`.cna`), you must also initialize `libaggressor`. This library dynamically registers all native Aggressor API hooks into the VM and provides a routing framework (via fallbacks and explicit C overrides) so your application can define how commands like `bexecute_assembly` or `blog` actually behave in your specific environment.

```c
#include <stdlib.h>
#include "slp_common.h"
#include "slp_vm.h"
#include "aggressor.h"

static void *my_alloc(void *ptr, size_t size, void *ud) {
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}

// Custom override for an existing Aggressor function (e.g., 'blog')
static SlpValue my_blog_override(SlpVM *vm, SlpValue *args, int argc, void *user_data) {
    if (argc >= 2 && SLP_IS_STRING(args[1])) {
        printf("[C2 Log]: %s\n", SLP_AS_STRING(args[1])->chars);
    }
    return SLP_NULL_VAL;
}

// A completely custom native function not present in standard Aggressor
static SlpValue my_custom_api(SlpVM *vm, SlpValue *args, int argc) {
    printf("[Custom API]: Executed!\n");
    return SLP_BOOL_VAL(true);
}

int main() {
    SlpAllocator allocator = {my_alloc, NULL};
    SlpVM *vm = slp_vm_new(&allocator);
    
    // Initialize libaggressor with an optional context/fallback configuration
    AggressorConfig config = {
        .fallback = NULL,       // Use the safe default no-op fallback for unknown functions
        .user_data = NULL       // Optional context pointer passed into all overrides
    };
    AggressorState *ag_state = aggressor_init(vm, &config);

    // 1. Override specific native Aggressor functions
    aggressor_set(ag_state, "blog", my_blog_override);

    // 2. Define your own entirely custom functions
    slp_vm_define_native(vm, "my_custom_api", my_custom_api);

    // Evaluate the Aggressor script
    const char *script = 
        "blog(\"123\", \"This is intercepted by my_blog_override in C!\");"
        "my_custom_api();";
        
    slp_vm_interpret(vm, script);

    // Clean up
    aggressor_deinit(ag_state);
    slp_vm_free(vm);
    return 0;
}
```

To link against the library, compile your application with the static libraries (`libslp.a` and `libaggressor.a`) located in the `bin/` directory and ensure the `include/` directory is in your header search paths.

## Alternatives

For another implementation of the Sleep programming language, please see [EvanMcBroom/sleepy](https://github.com/EvanMcBroom/sleepy).

