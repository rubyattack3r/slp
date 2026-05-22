CC ?= gcc
CXX ?= g++

CFLAGS = -g -O0 -std=c99 -Wall -Wextra -Iinclude -Ideps/bestline
CXXFLAGS = -std=c++11 -Wall -Wextra -Iinclude -I.

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)

TEST_SRC = $(wildcard tests/*.cpp)
TEST_OBJ = $(TEST_SRC:.cpp=.o)

.PHONY: all clean test bench amalgamate sleepy

all: sleepy_lib sleepy amalgamate

# For now, sleepy_lib is a static library of the C implementation
sleepy_lib: $(OBJ)
	ar rcs libsleepy.a $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test runner compiles C++ test files and links with C object files
test_runner: $(OBJ) $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

tests/%.o: tests/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: test_runner
	./test_runner

bench: bench_sleepy
	./bench_sleepy

bench_sleepy: $(OBJ) bench/bench_sleepy.c
	$(CC) $(CFLAGS) -O2 -o $@ bench/bench_sleepy.c $(OBJ)

sleepy: $(OBJ) tools/repl.c deps/bestline/bestline.c
	$(CC) $(CFLAGS) -o $@ tools/repl.c deps/bestline/bestline.c $(OBJ)

cna_bundle: $(OBJ) tools/cna_bundle.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o bin/cna_bundle tools/cna_bundle.c $(OBJ)

cna_validator: $(OBJ) tools/cna_validator.c deps/bestline/bestline.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o bin/cna_validator tools/cna_validator.c deps/bestline/bestline.c $(OBJ)

tools: cna_bundle cna_validator

aggressor_repl: $(OBJ) tools/aggressor_repl.c deps/bestline/bestline.c
	$(CC) $(CFLAGS) -o $@ tools/aggressor_repl.c deps/bestline/bestline.c $(OBJ)

clean:
	rm -rf src/*.o tests/*.o test_runner bench_sleepy sleepy cna_bundle aggressor_repl libsleepy.a dist/ bin/

amalgamate:
	./scripts/amalgamate.sh

amalgamate-single:
	./scripts/amalgamate.sh --single

CFLAGS += -Wno-unused-variable
