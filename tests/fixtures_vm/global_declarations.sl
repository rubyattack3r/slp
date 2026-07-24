global('@items %state $scalar');
global('%other', '@more');

assert @items isa "array";
assert %state isa "hash";
assert %other isa "hash";
assert @more isa "array";

push(@items, "first");
push(@more, "second");
%state["refresh_token"] = "baka";
%other["region"] = "aho";

assert @items[0] == "first";
assert @more[0] == "second";
assert %state["refresh_token"] == "baka";
assert %other["region"] == "aho";

$scalar = "preserved";
global('$scalar');
assert $scalar == "preserved";
