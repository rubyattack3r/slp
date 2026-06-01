# Advanced Scoping, Inline, and Local Integration Test

# Part 1: Nested inline function calls and dynamic variable sharing
$shared_var = "original";

inline inner_inline {
    assert $shared_var eq "modified_by_outer" : "inner_inline should see outer_inline modifications";
    $shared_var = "modified_by_inner";
}

inline outer_inline {
    assert $shared_var eq "original" : "outer_inline should see original variable";
    $shared_var = "modified_by_outer";
    inner_inline();
    assert $shared_var eq "modified_by_inner" : "outer_inline should see inner_inline changes";
}

sub start_sub {
    outer_inline();
}

start_sub();
assert $shared_var eq "modified_by_inner";

# Part 2: Parameter stack isolation
# Subroutine and inline function frames must have isolated parameter arrays (@_ and $1..$9)
sub sub_with_params {
    assert $1 eq "sub_arg1";
    assert $2 eq "sub_arg2";
    
    inline_with_params("inline_arg1", "inline_arg2");
    
    # After returning, the parameters of this frame must be restored perfectly
    assert $1 eq "sub_arg1";
    assert $2 eq "sub_arg2";
}

inline inline_with_params {
    assert $1 eq "inline_arg1" : "inline $1 was " . $1;
    assert $2 eq "inline_arg2";
    
    # Access parent subroutine parameter via upvalues or globals?
    # Wait, in C Sleep, parent subroutine local parameters ($1 etc.) are local to the parent's frame slots.
    # So they are not accessible in the inline frame slots. Let's make sure this isolation is correct.
}

sub_with_params("sub_arg1", "sub_arg2");

# Part 3: Nested inline scopes and return behavior
# An inline function returning a value
inline inline_calc {
    $temp = $1 + $2;
    return $temp * 2;
}

sub run_calc {
    $res = inline_calc(3, 4);
    assert $res == 14;
}

run_calc();

println("adv_scoping.sl passed!");
