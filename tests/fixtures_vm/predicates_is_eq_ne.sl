assert("hello" eq "hello");
assert(!("hello" eq "world"));

assert("hello" ne "world");
assert(!("hello" ne "hello"));

$a = "test";
$b = "test";
assert($a eq $b);
assert(!($a ne $b));

assert($null is $null);
assert(!("a" is "b"));
assert("a" is "a");
assert(1 is 1);
assert(!(1 is 2));

$x = @(1, 2);
$y = $x;
assert($x is $y);

$z = @(1, 2);
assert(!($x is $z)); # Arrays are distinct objects

assert(true is true);
assert(!(true is false));

# Coerced comparisons including $null
assert($null eq $null);
assert($null eq "");
assert("" eq $null);
assert(!($null ne $null));
assert(!($null ne ""));
assert(!("" ne $null));

# Number coercion
assert(0 eq "0");
assert(0 eq 0);
assert("42" eq 42);
assert(42 eq "42");
assert(1 lt 2);
assert("1" lt "2");
assert(1 lt "2");
assert("1" lt 2);
assert(2 gt 1);
assert("2" gt "1");
assert(2 gt "1");
assert("2" gt 1);

