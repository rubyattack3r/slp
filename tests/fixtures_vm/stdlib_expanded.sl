# 1. Test pack & unpack
$packed = pack("iS", 123456, 42);
assert typeof($packed) == "string";
assert size($packed) == 6;

@unpacked = unpack("iS", $packed);
assert size(@unpacked) == 2;
assert @unpacked[0] == 123456;
assert @unpacked[1] == 42;

# Test little-endian vs big-endian pack/unpack
$p_be = pack("+i", 9999);
$p_le = pack("-i", 9999);
assert $p_be != $p_le;

@up_be = unpack("+i", $p_be);
@up_le = unpack("-i", $p_le);
assert @up_be[0] == 9999;
assert @up_le[0] == 9999;

# Test string packing
$p_str = pack("z*", "hello world");
@up_str = unpack("z*", $p_str);
assert @up_str[0] == "hello world";

# Test hex packing
$p_hex = pack("h*", "deadbeef");
@up_hex = unpack("h*", $p_hex);
assert @up_hex[0] == "deadbeef";

# 2. Test filesystem functions
$test_dir = "test_mkdir_tmp";
deleteFile($test_dir); # Clean up if left over
assert mkdir($test_dir) == true;

assert lof(".") > 0;
assert lastModified(".") > 0;

$cur_dir = cwd();
assert typeof($cur_dir) == "string";

# Test getFileName & getFileParent
assert getFileName("/path/to/my_file.txt") == "my_file.txt";
assert getFileParent("/path/to/my_file.txt") == "/path/to";
assert getFileName("simple.txt") == "simple.txt";
assert getFileParent("simple.txt") == $null;

assert deleteFile($test_dir) == true;

# 3. Test formatDate
$time = 1716630000000; # Some epoch ms
$formatted = formatDate($time, "yyyy-MM-dd HH:mm:ss");
assert typeof($formatted) == "string";

# Let's check parseDate
$parsed = parseDate("yyyy-MM-dd HH:mm:ss", "2024-05-25 10:15:30");
assert $parsed > 0;

println("Expanded stdlib tests passed!");
