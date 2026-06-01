# Test shell backticks
$output = `echo "hello backtick"`;
assert size($output) > 0 : "backticks failed";
