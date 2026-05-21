CC = gcc
CXX = g++

CFLAGS = -std=c99 -Wall -Wextra -Iinclude -Ideps/bestline
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

clean:
	rm -rf src/*.o tests/*.o test_runner bench_sleepy sleepy libsleepy.a dist/

amalgamate:
	./scripts/amalgamate.sh

amalgamate-single:
	./scripts/amalgamate.sh --single
