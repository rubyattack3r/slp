# Test unary predicates on edge cases
$r1 = -istrue true;
$r2 = -istrue false;
$r3 = -istrue "";
$r4 = -istrue "0";

assert $r1;
assert !$r2;
assert $r3;
assert $r4;

# Test -isboolean
$r5 = -isboolean true;
$r6 = -isboolean false;
$r7 = -isboolean 0;
assert $r5;
assert $r6;
assert !$r7;

# Test filesystem predicates on non-existent paths
assert !(-exists "/nonexistent/path/to/file.txt");
assert !(-isdir "/nonexistent/path");
assert !(-isfile "/nonexistent/path/to/file.txt");
assert !(-canread "/nonexistent/path/to/file.txt");
assert !(-canwrite "/nonexistent/path/to/file.txt");

println("Unary predicate edge cases passed");
