# Test string functions
assert strlen("hello") == 5;
assert strlen("") == 0;

assert substr("hello world", 0, 5) eq "hello";
assert substr("hello world", 6) eq "world";
assert substr("hello", 0, 3) eq "hel";

assert lc("Hello WORLD") eq "hello world";
assert uc("Hello WORLD") eq "HELLO WORLD";

assert chr(65) eq "A";
assert chr(97) eq "a";
assert asc("A") == 65;
assert asc("a") == 97;

assert charAt("hello", 0) eq "h";
assert charAt("hello", 4) eq "o";

assert left("hello", 3) eq "hel";
assert left("hello", 10) eq "hello";
assert right("hello", 3) eq "llo";
assert right("hello", 10) eq "hello";

assert mid("hello world", 6, 5) eq "world";
assert mid("hello", 1, 3) eq "ell";

assert find("hello world", "world") == 6;
assert find("hello world", "xyz") == -1;
assert find("hello", "hel") == 0;

assert replace("hello world", "world", "sleep") eq "hello sleep";
assert replace("aaa", "a", "b") eq "bbb";

assert join(", ", @("a", "b", "c")) eq "a, b, c";
assert join("", @("x", "y")) eq "xy";

$a = split(",", "a,b,c");
assert size($a) == 3;
assert $a[0] eq "a";
assert $a[1] eq "b";
assert $a[2] eq "c";

println("String functions passed");
