# Test truthy/falsy
if (-istrue 1) { println("1 is true"); }
if (!(-istrue 0)) { println("0 is false"); }
if (!(-istrue $null)) { println("null is false"); }

# Test types
$arr = @(1, 2);
if (-isarray $arr) { println("isarray works"); }
if (!(-isarray "not array")) { println("not array works"); }

$h = %(a => 1);
if (-ishash $h) { println("ishash works"); }
if (!(-ishash $arr)) { println("not hash works"); }

$n = 123;
if (-isnumber $n) { println("isnumber works"); }
if (!(-isnumber "abc")) { println("not number works"); }

sub my_func { return 1; }
if (-isfunction &my_func) { println("isfunction works"); }
if (!(-isfunction $n)) { println("not function works"); }

# Test filesystem (some basic ones that should always work)
# Assuming src/slp_vm.c exists based on the repo structure
if (-exists "src/slp_vm.c") { println("exists works"); }
if (-isfile "src/slp_vm.c") { println("isfile works"); }
if (-canread "src/slp_vm.c") { println("canread works"); }

if (!(-exists "nonexistent_file_12345.txt")) { println("not exists works"); }
if (-isdir "src") { println("isdir works"); }
