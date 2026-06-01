# Test the x operator (string repetition / OP_REPEAT)
assert ("ab" x 3) eq "ababab";
assert ("ha" x 0) eq "";
assert ("x" x 1) eq "x";
assert ("hi" x 5) eq "hihihihihi";

println("Repeat operator passed");
