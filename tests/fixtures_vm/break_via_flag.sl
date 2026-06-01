$done = false;
$i = 0;
while (!$done) {
    $i = $i + 1;
    if ($i == 5) {
        $done = true;
    }
}
assert($i == 5);
