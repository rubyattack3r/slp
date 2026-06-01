# Test array functions
$a = @(3, 1, 4, 1, 5);

# sort (ascending)
sort($a);
assert $a[0] == 1;
assert $a[1] == 1;
assert $a[4] == 5;

# reverse
reverse($a);
assert $a[0] == 5;

# copy
$b = copy($a);
assert size($b) == 5;
$b[0] = 99;
assert $a[0] == 5;

# shift
$v = shift($a);
assert $v == 5;
assert size($a) == 4;

# sum
assert sum(@(1, 2, 3, 4, 5)) == 15;
assert sum(@()) == 0;

# flatten
$nested = @(@(1, 2), @(3, 4), 5);
$flat = flatten($nested);
assert size($flat) == 5;
assert $flat[0] == 1;
assert $flat[4] == 5;

# concat
$c = @(1, 2);
$d = @(3, 4);
concat($c, $d);
assert size($c) == 4;
assert $c[3] == 4;

# contains
assert contains(3, @(1, 2, 3));
assert !contains(4, @(1, 2, 3));

# add
$e = @(1);
add($e, 2);
assert size($e) == 2;
assert $e[1] == 2;

# removeAt
removeAt($e, 0);
assert size($e) == 1;
assert $e[0] == 2;

# clear
clear($e);
assert size($e) == 0;

println("Array functions passed");
