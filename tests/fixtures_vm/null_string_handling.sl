# Test null string concatenation, interpolation, and joining behavior
# to ensure it behaves like standard Sleep (treating $null as "")

# 1. String Concatenation with $null
assert("hello" . $null eq "hello");
assert($null . "world" eq "world");
assert($null . $null eq "");

# 2. String Interpolation inside Double Quotes
$val = $null;
assert("hello $val" eq "hello ");
assert("$val world" eq " world");
assert("$val" eq "");

# 3. Joining Array elements containing $null
$joined = join(" ", @("a", $null, "b"));
assert($joined eq "a  b");

println("null_string_handling.sl passed!");
