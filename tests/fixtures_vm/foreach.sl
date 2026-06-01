$a = array("a", "b", "c");
$sum = "";
foreach $idx => $val ($a) {
    $sum = $sum . $idx . ":" . $val;
}
assert($sum == "0:a1:b2:c");

$sum = "";
foreach $val ($a) {
    $sum = $sum . $val;
}
assert($sum == "abc");

%h = %();
%h["foo"] = "bar";
foreach $k => $v (%h) {
    assert($k == "foo");
    assert($v == "bar");
}
