CC ?= gcc
CXX ?= g++

CFLAGS = -g -O0 -std=c99 -Wall -Wextra -Iinclude -Ideps/bestline -Iextensions/aggressor
CXXFLAGS = -std=c++11 -Wall -Wextra -Iinclude -I. -Iextensions/aggressor

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)

# Extension: libaggressor
AGGRESSOR_SRC = extensions/aggressor/aggressor.c
AGGRESSOR_OBJ = extensions/aggressor/aggressor.o

TEST_SRC = $(wildcard tests/*.cpp)
TEST_OBJ = $(TEST_SRC:.cpp=.o)

.PHONY: all clean test bench amalgamate sleepy cna_bundler cna_validator aggressor tools sleepy_lib libaggressor test_runner bench_sleepy sleepy_fmt sleepyd

all: sleepy_lib libaggressor sleepy tools amalgamate

# Static library of the C implementation
sleepy_lib: bin/libsleepy.a

bin/libsleepy.a: $(OBJ)
	@mkdir -p bin
	ar rcs $@ $(OBJ)

# Static library of the Aggressor extensions
libaggressor: bin/libaggressor.a

bin/libaggressor.a: $(AGGRESSOR_OBJ)
	@mkdir -p bin
	ar rcs $@ $(AGGRESSOR_OBJ)


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test runner compiles C++ test files and links with C object files
test_runner: bin/test_runner

bin/test_runner: $(OBJ) $(AGGRESSOR_OBJ) $(TEST_OBJ)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $^

tests/%.o: tests/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: test_runner
	./bin/test_runner

bench: bench_sleepy
	./bin/bench_sleepy

bench_sleepy: bin/bench_sleepy

bin/bench_sleepy: $(OBJ) tools/bench_sleepy.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -O2 -o $@ tools/bench_sleepy.c $(OBJ)

sleepy: bin/sleepy

bin/sleepy: $(OBJ) tools/repl.c deps/bestline/bestline.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/repl.c deps/bestline/bestline.c $(OBJ)

cna_bundler: bin/cna_bundler

bin/cna_bundler: $(OBJ) tools/cna_bundler.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/cna_bundler.c $(OBJ)

$(AGGRESSOR_OBJ): $(AGGRESSOR_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

cna_validator: bin/cna_validator

bin/cna_validator: $(OBJ) $(AGGRESSOR_OBJ) tools/cna_validator.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/cna_validator.c $(OBJ) $(AGGRESSOR_OBJ)

aggressor: bin/aggressor

bin/aggressor: $(OBJ) $(AGGRESSOR_OBJ) tools/aggressor.c deps/bestline/bestline.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/aggressor.c deps/bestline/bestline.c $(OBJ) $(AGGRESSOR_OBJ)

sleepy_fmt: bin/sleepy_fmt

bin/sleepy_fmt: $(OBJ) tools/sleepy_fmt.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/sleepy_fmt.c $(OBJ)

sleepyd: bin/sleepyd

bin/sleepyd: $(OBJ) tools/sleepyd.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/sleepyd.c $(OBJ)

tools: cna_bundler cna_validator aggressor sleepy_fmt sleepyd bench_sleepy

clean:
	rm -rf src/*.o tests/*.o extensions/**/*.o extensions/*.o deps/**/*.o
	rm -rf bin/ dist/ *.cna .aggressor_history .sleepy_history test_no_rewrite

amalgamate:
	./scripts/amalgamate.sh

amalgamate-single:
	./scripts/amalgamate.sh --single

CFLAGS += -Wno-unused-variable
