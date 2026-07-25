# callcc captures the continuation at the callcc expression. Invoking it
# resumes the owner, and the owner's eventual return value comes back to the
# callcc handler.
sub foo {
   callcc {
       assert $1 ne $null : "continuation is null";
       $owner_result = [$1: "returned"];
       assert $owner_result eq "done"
           : "owner returned " . $owner_result;
       return "handler done";
   };
   return "done";
}

$val = foo();
assert $val eq "handler done" : "value was " . $val;

# Test that callcc result can be captured directly
sub bar {
    $x = callcc {
        return [$1: 42];
    };
    return $x;
}

$r = bar();
assert $r == 42 : "bar returned " . $r;
