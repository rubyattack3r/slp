# Sleep 2.1 local, closure, and global scope precedence.
global('$global_x');
$global_x = "global";

sub mutate_local {
    local('$global_x $fresh @local_array %local_hash');
    assert $global_x is $null;
    assert $fresh is $null;
    assert size(@local_array) == 0;
    assert size(%local_hash) == 0;
    $global_x = "local";
    $fresh = "temporary";
}

mutate_local();
assert $global_x eq "global";
assert $fresh is $null;

# Each normal call gets an isolated local scope, including recursive calls.
sub recurse_local {
    local('$depth');
    $depth = $1;
    if ($1 > 0) {
        recurse_local($1 - 1);
    }
    assert $depth == $1;
}

recurse_local(4);

# Only the top local level participates in lookup. popl() restores the
# previous level and may initialize it with key/value arguments.
sub nested_locals {
    local('$value $copied');
    $value = "outer";
    pushl($value => "inner");
    assert $value eq "inner";
    local('$only_inner');
    $only_inner = 7;
    popl($copied => $value);
    assert $value eq "outer";
    assert $copied eq "inner";
    assert $only_inner is $null;
}

nested_locals();

# Named arguments initialize local variables and do not enter @_.
sub named_arguments {
    assert $first eq "James";
    assert $team eq "ramrod";
    assert size(@_) == 0;
}

named_arguments($first => "James", $team => "ramrod");

# Positional and named variables retain their caller scalar identity.
$by_reference = "before";
sub mutate_positional {
    $1 = "after";
}
mutate_positional($by_reference);
assert $by_reference eq "after";

sub mutate_named {
    $named = "named-after";
}
$named_source = "named-before";
mutate_named($named => $named_source);
assert $named_source eq "named-after";

# Pass-by-name is compiled as $local_name => $local_name.
sub receive_by_name {
    $local_value = "changed-by-name";
}
sub send_by_name {
    local('$local_value');
    $local_value = "before-by-name";
    receive_by_name(\$local_value);
    assert $local_value eq "changed-by-name";
}
send_by_name();

# Inline functions execute against the caller's local and closure scopes.
inline mutate_inline {
    assert $inline_value eq "before";
    $inline_value = "after";
    local('$inline_created');
    $inline_created = 42;
}

sub call_inline {
    local('$inline_value');
    $inline_value = "before";
    mutate_inline();
    assert $inline_value eq "after";
    assert $inline_created == 42;
}

call_inline();

# A closure's this-scope persists between calls.
sub next_counter {
    this('$counter');
    $counter++;
    return $counter;
}

assert next_counter() == 1;
assert next_counter() == 2;

println("Scope conformance passed!");
