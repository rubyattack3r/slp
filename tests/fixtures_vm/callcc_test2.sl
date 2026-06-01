sub test_callcc {
    $x = callcc {
        println("Inside block");
        [$1: 42];
        println("Should not print");
        return -1;
    };
    println("After block, x is " . $x);
    return $x + 1;
}

$res = test_callcc();
assert $res == 43;
