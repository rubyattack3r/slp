# Test class literal
$cls = ^String;
assert $cls is ^java.lang.String : "class literal should resolve default imports";
assert "$cls" eq "class java.lang.String" : "class literal description failed";
