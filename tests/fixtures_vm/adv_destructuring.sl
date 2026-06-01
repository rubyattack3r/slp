# Advanced Destructuring Assignment Integration Test

# Part 1: Flat Tuple Unpacking
($a, $b, $c) = array(10, 20, 30);
assert $a == 10;
assert $b == 20;
assert $c == 30;

# Part 2: Destructuring with Mixed Sigils
($x, @y, %z) = array("hello", array(1, 2, 3), %("key" => "val"));
assert $x eq "hello";
assert size(@y) == 3;
assert @y[1] == 2;
assert %z["key"] eq "val";

# Part 3: Size Mismatch & Padding
# Under-allocation: tuple is larger than array, pads with null
($p, $q, $r) = array("first", "second");
assert $p eq "first";
assert $q eq "second";
assert $r == $null;

# Over-allocation: tuple is smaller than array, discards excess
($s, $t) = array("one", "two", "three", "four");
assert $s eq "one";
assert $t eq "two";

# Part 4: Destructuring inside Subroutines and Conditionals
sub test_destruct {
    ($first, $second) = @_;
    assert $first eq "arg1";
    assert $second eq "arg2";
    return array($second, $first);
}

($ret1, $ret2) = test_destruct("arg1", "arg2");
assert $ret1 eq "arg2";
assert $ret2 eq "arg1";

# Part 5: In-place Swapping
# Can we swap variables in a single tuple assignment?
# Wait! In ($a, $b) = array($b, $a):
# The RHS expression array($b, $a) is evaluated FIRST and pushed onto the stack.
# Then ($a, $b) is destructured. So it should swap!
$val_a = "aaa";
$val_b = "bbb";
($val_a, $val_b) = array($val_b, $val_a);
assert $val_a eq "bbb";
assert $val_b eq "aaa";

println("adv_destructuring.sl passed!");
