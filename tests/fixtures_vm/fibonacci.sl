$a = 0;
$b = 1;
$i = 0;
while ($i < 10) {
    $c = $a + $b;
    $a = $b;
    $b = $c;
    $i = $i + 1;
}
assert($a == 55);
assert($b == 89);
