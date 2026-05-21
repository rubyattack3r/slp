$sum = 0;
for ($i = 1; $i <= 10; $i = $i + 1) {
    if ($i == 5) {
        continue;
    }
    $sum = $sum + $i;
}
assert($sum == 50);
