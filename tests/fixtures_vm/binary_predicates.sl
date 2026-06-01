# Test binary predicates with various types
# Numeric comparisons via predicates
assert (1 lt 2);
assert !(2 lt 1);
assert (2 gt 1);
assert !(1 gt 2);

# String comparison
assert ("abc" lt "abd");
assert !("abc" lt "abb");
assert ("abd" gt "abc");
assert !("abb" gt "abc");

# String equality
assert ("abc" eq "abc");
assert !("abc" eq "def");
assert ("abc" ne "def");
assert !("abc" ne "abc");

# Identity with arrays
$a = @(1, 2);
$b = $a;
assert ($a is $b);

$c = @(1, 2);
assert !($a is $c);

# ismatch requires full string match
assert ("hello" ismatch "hello");
assert !("hello" ismatch "ell");
assert !("hello" ismatch "lo");

# hasmatch allows partial match
assert ("hello" hasmatch "ell");
assert ("hello" hasmatch "lo");
assert !("hello" hasmatch "xyz");

println("Binary predicates passed");
