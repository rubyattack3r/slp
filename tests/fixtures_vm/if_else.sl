$x = 0;
if (true) {
    $x = 1;
}
assert($x == 1);

if (false) {
    $x = 2;
} else {
    $x = 3;
}
assert($x == 3);

if (1 > 2) {
    $x = 4;
} else if (2 > 1) {
    $x = 5;
} else {
    $x = 6;
}
assert($x == 5);
