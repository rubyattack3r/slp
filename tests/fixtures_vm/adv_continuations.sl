# Comprehensive Continuation & Coroutine Integration Test

# Part 1: Basic callcc and nested callcc
$val1 = $null;
$val2 = $null;

sub test_nested_callcc {
    $x = callcc {
        $c1 = $1;
        $y = callcc {
            $c2 = $1;
            [$c1: "from_inner"];
            assert 1 == 0 : "should not reach here";
        };
        return $y;
    };
    return $x;
}

$res = test_nested_callcc();
assert $res eq "from_inner" : "Nested callcc returned " . $res;

# Part 2: Continuation inside loops
# A continuation that resets or skips loop iterations
$loop_count = 0;
$loop_cont = $null;

sub loop_test {
    for ($i = 0; $i < 5; $i = $i + 1) {
        $loop_count = $loop_count + 1;
        if ($i == 2) {
            $r = callcc {
                $loop_cont = $1;
                return "initial";
            };
            if ($r eq "jumped") {
                # Skip to next or do something
                return "done_jump";
            }
        }
    }
    return "normal";
}

$jumped = 0;
$l_res = loop_test();

if ($jumped == 0) {
    # First execution path
    assert $l_res eq "normal";
    assert $loop_count == 5 : "Loop count was " . $loop_count;
    
    $jumped = 1;
    [$loop_cont: "jumped"];
} else {
    # Second execution path (restored from continuation)
    assert $l_res eq "done_jump";
    assert $loop_count == 5 : "Loop count after jump was " . $loop_count;
}

# Part 3: Coroutines and state persistence
# A coroutine with a loop that maintains a local count
sub counter_gen {
    $count = 10;
    while ($count < 13) {
        yield $count;
        $count = $count + 1;
    }
    return 100;
}

assert counter_gen() == 10;
assert counter_gen() == 11;
assert counter_gen() == 12;
assert counter_gen() == 100;
# Resets on completion, starts over:
assert counter_gen() == 10;
assert counter_gen() == 11;

# Part 4: Coroutine with arguments
sub arg_gen {
    # Arguments inside the coroutine should be preserved
    yield $1;
    yield $2;
    return $3;
}

assert arg_gen("a", "b", "c") eq "a";
assert arg_gen() eq "b" : "second call should return b";
assert arg_gen() eq "c" : "third call should return c";
assert arg_gen("x", "y", "z") eq "x" : "should reset and return x";

println("adv_continuations.sl passed!");
