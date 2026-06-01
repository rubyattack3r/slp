# Test utility functions
assert typeOf(42) eq "number";
assert typeOf("hello") eq "string";
assert typeOf(true) eq "boolean";
assert typeOf($null) eq "null";
assert typeOf(@(1)) eq "array";
assert typeOf(%(a => 1)) eq "hash";
sub func { }
assert typeOf(&func) eq "function";

# byteAt
assert byteAt("ABC", 0) == 65;
assert byteAt("ABC", 1) == 66;

# tr
assert tr("hello", "el", "ip") eq "hippo";

# copy hash
%h1 = %(a => 1, b => 2);
%h2 = copy(%h1);
assert %h2["a"] == 1;
assert size(%h2) == 2;

# contains for hash
assert contains("a", %h1);
assert !contains("z", %h1);

println("Utility functions passed");
