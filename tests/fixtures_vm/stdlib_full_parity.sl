# 1. Unary and Binary Predicates Tests
assert -isupper "A";
assert !(-isupper "a");
assert -islower "a";
assert !(-islower "A");
assert -isletter "abc";
assert !(-isletter "abc1");
assert !(-istainted "abc");

assert "abc" iswm "a*";
assert "abc" iswm "*b*";
assert !("abc" iswm "*d");

assert "bc" in "abc";
assert !("d" in "abc");
assert 2 in @(1, 2, 3);
assert !(4 in @(1, 2, 3));

assert "a" isa "string";
assert 42 isa "number";
assert @() isa "array";
assert %() isa "hash";

# 2. Digests & Cryptography
assert digest("hello") == "5d41402abc4b2a76b9719d911017c592";
assert digest("hello", "SHA-256") == "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";
assert checksum("hello") == 907060870;

# 3. Functional programming
sub double_val {
    return $1 * 2;
}
@arr = @(1, 2, 3);
@mapped = map(&double_val, @arr);
assert size(@mapped) == 3;
assert @mapped[0] == 2;
assert @mapped[1] == 4;
assert @mapped[2] == 6;

sub is_even {
    return $1 % 2 == 0;
}
@filtered = filter(&is_even, @arr);
assert size(@filtered) == 1;
assert @filtered[0] == 2;

sub add_vals {
    return $1 + $2;
}
$reduced = reduce(&add_vals, @arr);
assert $reduced == 6;

# 4. Splicing & Array Utilities
@a = @(1, 2, 3, 4);
@removed = splice(@a, 1, 2, @(9, 10));
assert size(@a) == 4;
assert @a[0] == 1;
assert @a[1] == 9;
assert @a[2] == 10;
assert @a[3] == 4;
assert size(@removed) == 2;
assert @removed[0] == 2;
assert @removed[1] == 3;

@sub = subarray(@a, 1, 3);
assert size(@sub) == 2;
assert @sub[0] == 9;
assert @sub[1] == 10;

@sub2 = sublist(@a, 2, 4);
assert size(@sub2) == 2;
assert @sub2[0] == 10;
assert @sub2[1] == 4;

@q = @();
pushl(@q, 1);
pushl(@q, 2);
assert @q[0] == 2;
assert @q[1] == 1;
$p = popl(@q);
assert $p == 2;
assert @q[0] == 1;

@src1 = @(1, 2);
@src2 = @(3, 4);
assert addAll(@src1, @src2) == true;
assert size(@src1) == 4;
assert @src1[2] == 3;

@src3 = @(1, 2, 3, 4);
@to_remove = @(2, 4);
assert removeAll(@src3, @to_remove) == true;
assert size(@src3) == 2;
assert @src3[0] == 1;
assert @src3[1] == 3;

@src4 = @(1, 2, 3, 4);
@to_retain = @(2, 4, 5);
assert retainAll(@src4, @to_retain) == true;
assert size(@src4) == 2;
assert @src4[0] == 2;
assert @src4[1] == 4;

sub find_three {
    return $1 == 3;
}
$found = search(@(1, 2, 3, 4), &find_three);
assert $found == 3;

# 5. Number parsing
assert parseNumber("123") == 123;
assert parseNumber("0x1a") == 26;
assert parseNumber("032") == 26;
assert parseNumber("12.34") == 12.34;

# 6. Ordered hash
$oh = ohash();
assert $oh isa "hash";

# 7. Sizeof
assert sizeof("hello") == 5;
assert sizeof(@(1, 2, 3)) == 3;
assert sizeof(%(a => 1)) == 1;

# 8. File I/O & eof
$h = openf(">temp_parity_test.txt");
assert $h isa "io_handle";
assert writeb($h, "Hello Parity World\n") == true;
closef($h);

$h2 = openf("temp_parity_test.txt");
assert $h2 isa "io_handle";
assert !(-eof $h2);
$line = readln($h2);
assert $line == "Hello Parity World";
assert available($h2) == 1;
closef($h2);
deleteFile("temp_parity_test.txt");

# 9. Network socket stubs (ensure calling does not crash)
$sock = connect("127.0.0.1", 9999);

# 10. Process streams
$proc = exec("echo hello");
if ($proc != $null) {
    $line = readln($proc);
    assert $line == "hello";
    closef($proc);
}

println("Full stdlib parity tests passed!");
