$a = array(1, 2);
assert(typeof($a) == "array");
assert(typeof(1) == "number");
assert(typeof(true) == "boolean");
assert(typeof("s") == "string");
assert(typeof($null) == "null");
sub foo { }
assert(typeof(&foo) == "function");

%h = %(1 => "a", 2 => "b");
assert(typeof(%h) == "hash");
assert(size(%h) == 2);
@k = keys(%h);
assert(size(@k) == 2);
assert((@k[0] == 1 && @k[1] == 2) || (@k[0] == 2 && @k[1] == 1));

assert(remove(%h, 1) == true);
assert(size(%h) == 1);
assert(%h[1] == $null);
assert(%h[2] == "b");
assert(remove(%h, 3) == false);
