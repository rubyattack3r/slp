($a, $b) = array(1, 2);
assert $a == 1;
assert $b == 2;

($x, $y, $z) = array("letter x", "letter y", "letter z");
assert $x eq "letter x";
assert $y eq "letter y";
assert $z eq "letter z";

($bid, $token, $cat, $name) = array("1", "2", "3", "4");
assert $bid eq "1";
assert $token eq "2";
assert $cat eq "3";
assert $name eq "4";
