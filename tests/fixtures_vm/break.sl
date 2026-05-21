$sum = 0;
for ($i = 1; $i <= 100; $i = $i + 1) {
    if ($i == 11) {
        break;
    }
    $sum = $sum + $i;
}
assert($sum == 55);
