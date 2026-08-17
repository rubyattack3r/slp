# Optional JVM provider

This extension installs a regular JVM behind SLP's provider-neutral Java
backend API. The core does not include `jni.h`, link `libjvm`, or discover Java
at startup. An embedder chooses whether to compile and install this extension.

## Build and test

Use a JDK to compile the native JNI seam and the small reflection bridge:

```sh
make jvm JAVA_HOME=/path/to/jdk
make test-jvm JAVA_HOME=/path/to/jdk
make test-jvm-loadable JAVA_HOME=/path/to/jdk SLEEP_JAR=/path/to/sleep.jar
```

The repository also supplies a separate Nix shell so the default C development
shell stays Java-free:

```sh
nix develop .#jvm -c make test-jvm
```

This produces:

- `bin/libslp-jvm.a`
- `bin/slp-jvm-bridge.jar`

Linux embedders also link `-ldl`. macOS and Windows resolve the loader APIs from
their normal system libraries.

## Embed

Install the provider after constructing the VM and before interpreting source:

```c
#include "slp_jvm.h"

SlpVM *vm = slp_vm_new(&allocator);
SlpJvmOptions options = {0};
SlpJvmError error;

if (slp_jvm_install(
        &allocator, vm, &options, &error) != SLP_JVM_OK) {
    /* Report error.message and either stop or continue without Java. */
}
```

With `options.runtime_dir == NULL`, the provider looks beside the running
executable:

```text
application/
├── bemu
└── runtime/
    ├── bin/ or lib/           JVM image files
    └── lib/
        ├── slp-jvm-bridge.jar
        └── sleep.jar             optional Loadable compatibility runtime
```

The application binary contains SLP and the JNI provider, while `runtime/`
contains the target-specific JVM image. Nothing is read from a host Java
installation. Set `runtime_dir` or `bridge_jar` explicitly when the files live
elsewhere. `classpath` appends application JARs to the startup class path; Sleep
`import ... from: jar` can add more JARs while running.

`sleep.jar` supplies the official `sleep.interfaces.Loadable`,
`sleep.interfaces.Function`, Scalar, and `ScriptInstance` types expected by
existing extensions. It is only required for `use()` compatibility; ordinary
Java interop remains usable without it. The bridge JAR has no compile-time
dependency on Sleep and detects a missing `sleep.jar` when `use()` is called.
The upstream JAR is available from the
[official Sleep download page](https://sleep.dashnine.org/download.html).

## Package a runtime

Create a self-contained runtime image with the JDK used for a platform build:

```sh
JAVA_HOME=/path/to/jdk \
    scripts/package-jvm-runtime.sh release-root
```

To include Loadable support in the same runtime folder:

```sh
JAVA_HOME=/path/to/jdk \
SLP_SLEEP_JAR=/path/to/sleep.jar \
    scripts/package-jvm-runtime.sh release-root
```

The script refuses to replace an existing destination. It uses `jlink` and
includes Java SE, `jdk.unsupported`, and elliptic-curve crypto by default for
broad third-party JAR compatibility. Set `SLP_JVM_MODULES` to a tested
comma-separated module list, or `ALL-MODULE-PATH`, when a product needs a
different footprint. Sleep is BSD-licensed; when redistributing its JAR, keep
the required license notices with the product.

Build one runtime per operating-system and CPU target. In particular, a
Windows x86 BEMU build needs a Windows x86 JVM; JVM libraries cannot be shared
between x86 and x64 or between operating systems.

## Current interop

The provider currently handles:

- package and direct-class imports, including `from:` JAR paths;
- class literals, constructors, instance/static methods, and public field reads;
- reflection overload selection, primitive numeric conversions, and varargs;
- null, boolean, number, long, double, string, Java object, class, and object
  array values;
- JNI global-reference release during SLP garbage collection;
- attachment to an already-created JVM when its class path already contains
  `slp-jvm-bridge.jar` and, for Loadables, `sleep.jar`;
- `use(^Class)` and `use('extension.jar', 'qualified.Class')` for classes that
  implement the official Sleep `Loadable` interface;
- one real `ScriptInstance` compatibility context per SLP VM, including script
  name and the destructive `flagError`/`checkError` channel;
- `Function` entries registered by `scriptLoaded`, with Sleep Scalar argument
  order and conversion into native SLP callables;
- bidirectional synchronization between SLP globals and the official
  `ScriptVariables` global container at every Loadable bridge boundary,
  including identity-preserving nested arrays and hashes;
- official `Predicate` and `Operator` registrations, including custom binary
  predicate names and overrides of built-in symbolic operators/predicates;
- official `Environment` and `FilterEnvironment` registrations. Java receives
  a real `sleep.engine.Block` subclass backed by the compiled SLP closure;
- official `PredicateEnvironment` registrations. Java receives a real
  `CheckEval`/`CheckAnd`/`CheckOr` tree whose leaves retain compiled SLP
  closures, so concrete-class inspection and Sleep short-circuiting work;
- SLP closures exposed to Java as real `sleep.bridges.SleepClosure` instances,
  with message (`$0`) and argument ordering preserved, and Java Sleep closures
  callable directly from SLP;
- `setUnloaded()` followed by reverse-order `scriptUnloaded()` callbacks while
  the SLP VM and Java object handles are still valid.

Arrays and hashes have stable bridge identities. Existing Sleep `ScalarArray`
and `ScalarHash` objects are updated in place at each bridge boundary, so Java
references retained by a Loadable stay live, aliases remain aliases in both
directions, and cyclic collection graphs round-trip safely. SLP mutations are
visible to retained Java references before the next Java callback executes;
Java mutations are visible to SLP before that callback returns.

`use()` and syntax registered by that Loadable may appear in the same
`slp_vm_interpret()` source. The parser recognizes the unambiguous custom
predicate and predicate-environment shapes, while runtime dispatch still
validates that the Loadable actually registered them.
