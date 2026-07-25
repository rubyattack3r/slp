sub test_callcc {
    $x = callcc {
        println("Inside block");
        $owner_result = [$1: 42];
        println("Owner returned " . $owner_result);
        return -1;
    };
    println("After block, x is " . $x);
    return $x + 1;
}

$res = test_callcc();
assert $res == -1;
