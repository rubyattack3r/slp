# Fringe String, Regex, and Containment Integration Test

# Part 1: Transliteration (tr) and replaceAt
$s1 = "hello world";
$res_tr = tr($s1, "el", "31");
assert $res_tr eq "h311o wor1d" : "tr output was " . $res_tr;

$res_rep = replaceAt($s1, 6, "craziness");
assert $res_rep eq "hello crazi" : "replaceAt output was " . $res_rep;

# replaceAt edge cases: index out of bounds
$res_rep2 = replaceAt($s1, 100, "abc");
assert $res_rep2 eq $s1;
$res_rep3 = replaceAt($s1, -1, "abc");
assert $res_rep3 eq $s1;

# Part 2: Substrings and Indexing (left, right, mid, indexOf, lindexOf)
$str = "abracadabra";
assert left($str, 4) eq "abra";
assert left($str, 0) eq "";
assert left($str, 100) eq "abracadabra";

assert right($str, 4) eq "abra";
assert right($str, 0) eq "";
assert right($str, 100) eq "abracadabra";

assert mid($str, 3, 5) eq "acada" : "mid was " . mid($str, 3, 5);
assert mid($str, 8, 10) eq "bra";
assert mid($str, -1, 3) eq "abr"; # start < 0 becomes 0

assert indexOf($str, "rac") == 2;
assert indexOf($str, "cad", 2) == 4;
assert indexOf($str, "cad", 5) == -1;
assert lindexOf($str, "a") == 10;
assert lindexOf($str, "bra") == 8;

# Part 3: Regex and Wildcard Match (ismatch, hasmatch, iswm)
$check = "antigravity";
assert $check iswm "anti*";
assert $check iswm "*grav*";
assert $check iswm "a?ti*ty";
assert !($check iswm "anti");

assert $check ismatch "antigravity";
assert !($check ismatch "grav"); # ismatch requires full match

assert $check hasmatch "grav";   # hasmatch searches substring
assert $check hasmatch "anti";

# Subgroup capture in hasmatch
if ("user:antigravity" hasmatch "user:(.*)") {
    $m = matched();
    assert size($m) == 1;
    assert $m[0] == "antigravity" : "captured subgroup was " . $m[0];
}

# Part 4: containment (in) operator
$test_arr = array("foo", "bar", "baz");
assert "bar" in $test_arr;
assert !("qux" in $test_arr);

$test_hash = %("x" => 1, "y" => 2);
assert "x" in $test_hash;
assert !("z" in $test_hash);

$test_str = "supercalifragilistic";
assert "fragil" in $test_str;
assert !("antigravity" in $test_str);

# Part 5: Edge cases of split and replace
# Empty pattern split splits into characters
$split_chars = split("", "abc");
assert size($split_chars) == 3;
assert $split_chars[0] eq "a";
assert $split_chars[1] eq "b";
assert $split_chars[2] eq "c";

# Normal split
$split_words = split(",", "x,y,,z");
assert size($split_words) == 4 : "Split size was " . size($split_words);
assert $split_words[0] eq "x";
assert $split_words[1] eq "y";
assert $split_words[2] eq "";
assert $split_words[3] eq "z";

# Replace edge case: multiple matches
$rep_test = "bananas";
assert replace($rep_test, "a", "o") eq "bononos";
assert replace($rep_test, "an", "en") eq "benenas";
assert replace($rep_test, "xyz", "abc") eq "bananas";

println("fringe_strings.sl passed!");
