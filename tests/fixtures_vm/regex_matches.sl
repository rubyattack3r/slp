$str = "hello world";
if ($str hasmatch "world") {
    println("hasmatch works");
}

if ($str ismatch "world") {
    println("ismatch works false positive!");
}

if ($str ismatch "hello world") {
    println("ismatch works");
}

if ($str =~ ".*world.*") {
    println("OP_MATCH works");
}

if ($str !=~ "foo") {
    println("OP_NOT_MATCH works");
}

# Test capture subgroups!
if ($str =~ "hello (.*)") {
    $m = matched();
    assert size($m) == 1;
    assert $m[0] == "world";
}

if ($str hasmatch "he(l)(lo)") {
    $m = matched();
    assert size($m) == 2;
    assert $m[0] == "l";
    assert $m[1] == "lo";
}

if ("foo bar baz" =~ "(foo) (bar) (baz)") {
    $m = matched();
    assert size($m) == 3;
    assert $m[0] == "foo";
    assert $m[1] == "bar";
    assert $m[2] == "baz";
}
