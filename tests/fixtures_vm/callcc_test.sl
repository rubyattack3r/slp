# callcc captures the continuation at the callcc expression.
# Invoking the continuation restores the VM state and pushes the value,
# then execution continues after the callcc expression.
sub foo {
   callcc {
       assert $1 ne $null : "continuation is null";
       [$1: "returned"];
       assert 1 == 0 : "this should not execute";
   };
   return "done";
}

$val = foo();
# The continuation restores to after callcc, so "done" is returned
assert $val eq "done" : "value was " . $val;

# Test that callcc result can be captured directly
sub bar {
    $x = callcc {
        [$1: 42];
    };
    return $x;
}

$r = bar();
assert $r == 42 : "bar returned " . $r;
