$global_var = "test";
sub my_func {
    local('$local_var');
    $local_var = $global_var;
    println($local_var);
}
