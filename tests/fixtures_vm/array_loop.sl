$a = array(10, 20, 30);
$sum = 0;
for ($i = 0; $i < size($a); $i = $i + 1) {
    $sum = $sum + $a[$i];
}
assert($sum == 60);
