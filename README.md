# Sleepy C Implementation

This project is a C implementation of a parser, lexer, and eventually a VM for the `sleepy` programming language.

## Design Constraints

1. **Zero Dependencies**: The core C implementation has absolutely zero external dependencies. It does not even rely on standard `malloc` and `free` implicitly. 
2. **Allocator Approach**: All memory allocations are driven through a custom `SleepyAllocator` which involves a `userdata` pointer and a user-provided reallocation function. This allocator must be passed around everywhere.
3. **Architecture Reference**: The implementation structure takes inspiration from the [Wren](https://github.com/wren-lang/wren) C implementation, while the language semantics and lexer/parser logic are based on the [Sleepy](https://github.com/sleepy-lang/sleepy) reference implementation.

## Project Structure

- `include/sleepy/`: Public headers for incorporating the Sleepy C API into your applications.
- `src/`: Core implementation files for the Lexer, Parser, and VM.
- `tests/`: Unit tests written in C++ using `doctest.h` to verify the C implementation against the standard test fixtures from the `sleepy` reference implementation.

## Building and Usage

You can build the interactive REPL (Read-Eval-Print Loop) binary using `make`:

```bash
make sleepy
```

This will produce a `sleepy` binary in the `bin/` directory. You can start the interactive REPL:

```bash
./bin/sleepy
```

Or execute a script directly:

```bash
./bin/sleepy path/to/script.sl
```

---

## CNA Tooling Suite

This repository contains robust tools specifically built for bundling, renaming, and dry-run validating multi-project Cobalt Strike Aggressor Scripts (CNA).

### 1. CNA Bundler (`cna_bundler`)

The `cna_bundler` tool processes a directory of CNA projects, recursively resolves and inlines nested child script includes (such as `include("modules/child.cna")` or `include(script_resource("child.cna"))`), sanitizes internal subroutines and variables per-project using safe prefixes, detects alias name collisions across projects, and applies config-driven renaming to resolve conflicts.

#### Build
```bash
make cna_bundler
```
Produces `bin/cna_bundler`.

#### Usage
```bash
./bin/cna_bundler <projects-dir> [-o <output.cna>] [--config <config-file>]
```
- `<projects-dir>`: Directory containing subdirectory projects (each with a `.cna` entrypoint).
- `-o`: File path to write the bundled single `.cna` output.
- `--config`: Config file specifying custom alias renames to resolve collisions.

#### Rename Config Format (`rename_config.txt`)
To resolve collisions, write rules matching `ProjectName:old_alias=new_alias`:
```text
# Avoid collision on 'whoami'
CS-ProjectB:whoami=whoami_pb
```

---

### 2. CNA Dry-Run Validator (`cna_validator`)

The `cna_validator` parses and executes dry-run simulations of a consolidated `.cna` bundle. It registers mock native Aggressor Script routines (using `libaggressor`), verifies that referenced external files/BOF payloads (e.g. `.o` files) exist on disk via `script_resource()` and `openf()`, and intercepts unknown closures safely.

#### Build
```bash
make cna_validator
```
Produces `bin/cna_validator`.

#### Usage
```bash
./bin/cna_validator <bundle.cna> [--format <text|json|junit>]
```
- `<bundle.cna>`: Consolidated script to validate.
- `--format`: Specify the output reporting format:
  - `text` (default): Human-readable ANSI-colored CLI summary.
  - `json`: Machine-parseable JSON validation object (perfect for CI/CD assertions).
  - `junit`: Standard JUnit XML report, perfect for publishing to CI dashboards.

#### Clean JSON Output Example:
```json
{
  "status": "SUCCESS",
  "aliases_registered": 139,
  "aliases_tested": 139,
  "errors": []
}
```

---

### 3. Interactive Aggressor REPL (`aggressor`)

The `aggressor` tool is a standalone interactive shell for Aggressor Scripts. It loads a `.cna` bundle or script, registers all registered mock Aggressor Script routines, and provides a REPL interface where you can execute loaded aliases interactively with autocompletion. History logging is disabled for this shell to prevent workspace clutter.

#### Build
```bash
make aggressor
```
Produces `bin/aggressor`.

#### Usage
```bash
./bin/aggressor <bundle.cna>
```
Once loaded, type any registered alias with arguments (e.g., `sc_config arg1 arg2`) to execute it. Type `exit` or `quit` to leave.

---

### 4. Generic Extension Library (`libaggressor`)

Located in `extensions/aggressor/`, `libaggressor` decouples the core Sleepy C engine from Cobalt Strike domain logic. It supports multi-VM co-existence by storing all FFI hooks, overrides, and dispatch state inside a heap-allocated struct associated with each VM instance via FFI slots, completely avoiding single-VM static globals.

---

## CI/CD Pipeline Integration

The bundled bash script `bundle.sh` automates the build and bundling workflow:
```bash
./bundle.sh ./examples/cna-projects ./dist/packaged_tools.cna
```

A complete GitHub Actions workflow is located at `.github/workflows/bundle-workflow.yml`. It:
1. Provisions the development environment using **Nix** (`nix develop`).
2. Bundles the projects via `bundle.sh`.
3. Compiles the dry-run validator and asserts the bundle is correct.
4. Executes the doctest-based unit test suite.
5. Archives and uploads the validated production-ready CNA bundle.

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

