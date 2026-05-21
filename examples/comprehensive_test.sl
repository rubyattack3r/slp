# comprehensive_test.sl
# A script to verify that all major language features are fully functional.

println("--- Starting Comprehensive Test ---");

# 1. Variables and Types
$num = 42;
$str = "hello";
$bool = true;
bare_var = $null;

assert(typeof($num) == "number");
assert(typeof($str) == "string");
assert(typeof($bool) == "boolean");
assert(typeof(bare_var) == "null");

# 2. Arrays and Hashes
@arr = @(1, 2, 3);
push(@arr, 4);
assert(size(@arr) == 4);
assert(pop(@arr) == 4);

%h = %( "a" => 1, "b" => 2 );
assert(%h["a"] == 1);
assert(size(%h) == 2);
@k = keys(%h);
assert(size(@k) == 2);
remove(%h, "a");
assert(size(%h) == 1);
assert(%h["a"] == $null);

# 3. Control Flow
$count = 0;
for ($i = 0; $i < 5; $i = $i + 1) {
    if ($i == 2) { continue; }
    if ($i == 4) { break; }
    $count = $count + 1;
}
assert($count == 3);

# 4. Foreach loops
$sum = 0;
foreach $val (@(10, 20, 30)) {
    $sum = $sum + $val;
}
assert($sum == 60);

$hash_sum = 0;
%test_hash = %( 10 => "x", 20 => "y" );
foreach $k => $v (%test_hash) {
    $hash_sum = $hash_sum + $k;
}
assert($hash_sum == 30);

# 5. Functions and Recursion
sub fib {
    if ($1 <= 1) {
        return $1;
    }
    return fib($1 - 1) + fib($1 - 2);
}
assert(fib(10) == 55);

# 6. Try / Catch
$caught = false;
try {
    throw "Error Occurred";
} catch $e {
    $caught = true;
    assert($e == "Error Occurred");
}
assert($caught == true);

println("--- All Tests Passed Successfully! ---");
