# Object expression test
sub my_obj {
    assert $0 eq "msg" : "msg failed";
    assert $1 == 42 : "arg failed";
    return "called";
}
$res = [&my_obj msg: 42];
assert $res eq "called" : "res failed";
