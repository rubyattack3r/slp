# SLP

SLP is a small, native C99 implementation of the
[Sleep programming language](https://sleep.dashnine.org/) designed for
embedding in C applications. It has no external runtime dependencies. Features
that require a real JVM, such as loading arbitrary Java classes, JARs, or
`Loadable` extensions, are not supported.

## Build it

You need `make` and a C/C++ compiler:

```sh
make
```

The build puts these files in `bin/`:

- `slp` — interpreter and REPL
- `libslp.a` — static library for embedding
- `slp_fmt` — source formatter
- `slpd` — bytecode disassembler
- `bench_slp` — benchmarks

## Run Sleep

Start the REPL:

```sh
./bin/slp
```

Run a script:

```sh
./bin/slp script.sl
```

Run source directly:

```sh
./bin/slp -e 'println("hello");'
./bin/slp -x '6 * 7'
```

Use `./bin/slp --help` for the full CLI.

## Embed it

Public headers live in `include/`. Link your application with `bin/libslp.a`;
add `extensions/stdlib/stdlib.c` if you want the standard Sleep functions.

The usual flow is:

1. Provide an `SlpAllocator`.
2. Create an `SlpVM`.
3. Register the standard library and any application functions.
4. Run code with `slp_vm_interpret()`.
5. Free the VM.

All runtime allocations go through the host-provided `SlpAllocator`, allowing
the application to track, limit, or customize SLP's memory use.

Include `slp_embed.h` when native functions need to read Sleep arguments or
retain and call a Sleep closure:

```c
static SlpValue call_callback(
    SlpVM *vm, SlpValue *values, int count) {
    SlpArgs args;
    SlpCallable *callback = NULL;
    SlpValue result = SLP_NULL_VAL;

    slp_args_init(&args, vm, values, count);
    if (!slp_args_next_callable(&args, &callback))
        return SLP_NULL_VAL;

    SlpResult status =
        slp_callable_call(callback, NULL, 0, &result);
    slp_callable_release(callback);
    return status == SLP_OK ? result : SLP_NULL_VAL;
}
```

`SlpArgs` provides typed argument reads. A failed read does not consume the
argument. `SlpCallable` keeps a closure alive across garbage collections and
must be released before its VM is freed. The embedding header also provides
value conversion and VM-owned array/hash helpers.

`slp_args_unpack()` remains available for existing format-string-based native
functions, but new integrations should use `SlpArgs`.

See `tests/test_embed.cpp` and `tests/test_embed_callable.cpp` for complete,
working examples.

## Use the amalgamation

Generate a copyable header/source pair:

```sh
make amalgamate
```

For the core runtime, copy `dist/slp.h` and `dist/slp.c` into your project:

```sh
cc app.c slp.c -I. -lm
```

The standard library stays optional. To use it, also copy
`dist/slp_stdlib.h` and `dist/slp_stdlib.c`, compile both sources, and register
it after creating the VM:

```c
#include "slp.h"
#include "slp_stdlib.h"

SlpVM *vm = slp_vm_new(&allocator);
slp_stdlib_init(vm);
```

```sh
cc app.c slp.c slp_stdlib.c -I. -lm
```

For a header-only integration, run:

```sh
make amalgamate-single
```

Define the implementation macros in exactly one C file:

```c
#define SLP_IMPLEMENTATION
#define SLP_STDLIB_IMPLEMENTATION
#include "slp.h"
#include "slp_stdlib.h"
```

Other files include the headers normally, without defining either macro. If
the application does not need the standard library, omit
`SLP_STDLIB_IMPLEMENTATION` and `slp_stdlib.h`.

Windows builds must also link `ws2_32`.

## Other tools

Format a script to stdout, update it in place, or write another file:

```sh
./bin/slp_fmt script.sl
./bin/slp_fmt -w script.sl
./bin/slp_fmt -o formatted.sl script.sl
```

Inspect the bytecode generated for a script:

```sh
./bin/slpd script.sl
```

Run the benchmark:

```sh
make bench
```

## Test it

```sh
make test
```

The suite covers the lexer, parser, compiler, VM, garbage collector, standard
library, formatter, embedding API, and the upstream Sleep fixtures. The
reference-output fixtures and compatibility ledger live under `tests/` and
are checked by the normal test run.

Run only the generated-library compile and execution checks with:

```sh
make test-amalgamation
```

SLP implements portable Sleep behavior directly in C. It supports native
equivalents for the Java collection and wrapper behavior used by Sleep
scripts, but it does not include a JVM. Loading arbitrary Java classes, JARs,
or Java `Loadable` extensions is therefore outside its scope.

## Project layout

- `include/` — public C API
- `src/` — parser, compiler, VM, values, and garbage collector
- `extensions/stdlib/` — standard Sleep functions
- `tools/` — interpreter, formatter, disassembler, and benchmark
- `tests/` — unit tests and reference fixtures

## License

The C implementation is available under the [MIT License](LICENSE). Test
fixtures derived from the original Sleep project retain their
[CC BY-SA 3.0 US license](tests/LICENSE).
