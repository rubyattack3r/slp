$a = array(1, 2, 3);
push($a, 4);
assert(size($a) == 4);
assert($a[3] == 4);
$v = pop($a);
assert($v == 4);
assert(size($a) == 3);
