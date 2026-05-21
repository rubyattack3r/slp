sub my_func {
    println("Original: " . $1);
}

my_func("test");

$orig = &my_func;
&my_func = lambda({
    [$orig: "Override: " . $1];
}, $orig => $orig);

my_func("test");
