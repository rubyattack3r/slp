# Test utility functions
assert typeOf(42) is ^sleep.engine.types.IntValue;
assert typeOf("hello") is ^sleep.engine.types.StringValue;
assert typeOf(true) is ^sleep.engine.types.IntValue;
assert typeOf(false) is ^sleep.engine.types.NullValue;
assert typeOf($null) is ^sleep.engine.types.NullValue;
assert typeOf(@(1)) is ^sleep.engine.types.ListContainer;
assert typeOf(%(a => 1)) is ^sleep.engine.types.HashContainer;
sub func { }
assert typeOf(&func) is ^sleep.engine.types.ObjectValue;

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
