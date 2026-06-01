# Test math functions
assert abs(-5) == 5;
assert abs(5) == 5;
assert abs(0) == 0;

assert ceil(1.1) == 2;
assert ceil(-1.1) == -1;
assert floor(1.9) == 1;
assert floor(-1.9) == -2;

assert sqrt(4) == 2;
assert sqrt(9) == 3;

assert round(1.5) == 2;
assert round(1.4) == 1;

assert int(3.7) == 3;
assert int(-3.7) == -3;

# Test double/cast
assert double("3.14") > 3.13;
assert double("3.14") < 3.15;

assert cast("42", "int") == 42;
assert cast(42, "string") eq "42";

# Test min/max
assert min(1, 2) == 1;
assert max(1, 2) == 2;

println("Math functions passed");
