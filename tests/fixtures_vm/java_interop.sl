# Tests to ensure that Java Interop features gracefully fallback
# to $null in the C-port without crashing the VM.

# OP_IMPORT
import java.util.*;
assert true : "Import should not crash the VM";

# OP_CLASS_LITERAL
$cls = ^java.util.LinkedList;
assert $cls eq "java.util.LinkedList" : "Class literal should resolve to a string";

# OP_OBJ_EXPR (Object Instantiation / Method Invocation)
$obj = [new LinkedList];
assert $obj is $null : "Unresolved Java classes should instantiate to $null";

$call_result = [$obj add: "item 1"];
assert $call_result is $null : "Method calls on $null should safely return $null";

# Unsupported Builtin Functions (should evaluate to $null and be callable)
$inst_result = newInstance(^Runnable, { return "hello"; });
assert $inst_result is $null : "newInstance should safely resolve to $null";

$cast_result = cast(@(1, 2, 3), ^List);
assert size($cast_result) == 3 : "cast should gracefully return the original value if unrecognized";

# Use statement
$use_result = use("somefile.jar", "SomeClass");
assert $use_result is $null : "use should safely resolve to $null";
