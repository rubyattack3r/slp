# Test variables with and without sigils
$a = 1;
assert($a == 1);

@arr = @(1, 2, 3);
assert(size(@arr) == 3);
assert(@arr[0] == 1);

%h = %(1 => "a", 2 => "b");
assert(size(%h) == 2);
assert(%h[1] == "a");

# Sigilless bare identifiers
test = "hello";
assert(test == "hello");

test_num = 42;
assert(test_num == 42);

test_arr = @(4, 5);
assert(size(test_arr) == 2);
assert(test_arr[1] == 5);

test_hash = %("key" => "value");
assert(size(test_hash) == 1);
assert(test_hash["key"] == "value");

# Mutating variables
test = test_num + 8;
assert(test == 50);

$a = $a + test_arr[0];
assert($a == 5);
