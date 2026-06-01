sub gen {
    yield 1;
    yield 2;
    yield 3;
    return 4;
}

println(gen());
println(gen());
println(gen());
println(gen());
println(gen());
