# Test match operators with various patterns
$str = "hello world 123";

# Basic match
assert ($str =~ "hello");
assert ($str =~ "world");
assert ($str =~ "123");
assert ($str =~ "[0-9]+");
assert ($str =~ "hello.*123");
assert !($str =~ "^world");

# Not match
assert ($str !=~ "xyz");
assert ($str !=~ "^world");
assert ($str !=~ "xyzzy");
assert !($str !=~ "hello");

# Edge cases with different regex patterns
assert ($str =~ "hello world");
assert ($str =~ "^hello");
assert ($str =~ "123$");

println("Match operators passed");
