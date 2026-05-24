CC ?= gcc
CXX ?= g++

CFLAGS = -g -O0 -std=c99 -Wall -Wextra -Iinclude -Ideps/bestline -Iextensions/aggressor -Iextensions/stdlib
CXXFLAGS = -std=c++11 -Wall -Wextra -Iinclude -I. -Iextensions/aggressor -Iextensions/stdlib

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)

# Extension: libaggressor
AGGRESSOR_SRC = extensions/aggressor/aggressor.c
AGGRESSOR_OBJ = extensions/aggressor/aggressor.o

# Extension: stdlib
STDLIB_SRC = extensions/stdlib/stdlib.c
STDLIB_OBJ = extensions/stdlib/stdlib.o

TEST_SRC = $(wildcard tests/*.cpp)
TEST_OBJ = $(TEST_SRC:.cpp=.o)

.PHONY: all clean test bench amalgamate slp cna_bundler cna_validator aggressor tools slp_lib libaggressor test_runner bench_slp slp_fmt slpd

all: slp_lib libaggressor slp tools amalgamate

# Static library of the C implementation
slp_lib: bin/libslp.a

bin/libslp.a: $(OBJ)
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

bin/test_runner: $(OBJ) $(AGGRESSOR_OBJ) $(STDLIB_OBJ) $(TEST_OBJ)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $^

tests/%.o: tests/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: test_runner
	./bin/test_runner

bench: bench_slp
	./bin/bench_slp

bench_slp: bin/bench_slp

bin/bench_slp: $(OBJ) tools/bench_slp.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -O2 -o $@ tools/bench_slp.c $(OBJ)

slp: bin/slp

bin/slp: $(OBJ) $(STDLIB_OBJ) tools/slp.c deps/bestline/bestline.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/slp.c deps/bestline/bestline.c $(OBJ) $(STDLIB_OBJ)

cna_bundler: bin/cna_bundler

bin/cna_bundler: $(OBJ) $(STDLIB_OBJ) tools/cna_bundler.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/cna_bundler.c $(OBJ) $(STDLIB_OBJ)

$(AGGRESSOR_OBJ): $(AGGRESSOR_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(STDLIB_OBJ): $(STDLIB_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

cna_validator: bin/cna_validator

bin/cna_validator: $(OBJ) $(AGGRESSOR_OBJ) $(STDLIB_OBJ) tools/cna_validator.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/cna_validator.c $(OBJ) $(AGGRESSOR_OBJ) $(STDLIB_OBJ)

aggressor: bin/aggressor

bin/aggressor: $(OBJ) $(AGGRESSOR_OBJ) $(STDLIB_OBJ) tools/aggressor.c deps/bestline/bestline.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/aggressor.c deps/bestline/bestline.c $(OBJ) $(AGGRESSOR_OBJ) $(STDLIB_OBJ)

slp_fmt: bin/slp_fmt

bin/slp_fmt: $(OBJ) tools/slp_fmt.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/slp_fmt.c $(OBJ)

slpd: bin/slpd

bin/slpd: $(OBJ) tools/slpd.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/slpd.c $(OBJ)

tools: cna_bundler cna_validator aggressor slp_fmt slpd bench_slp

clean:
	rm -rf src/*.o tests/*.o extensions/**/*.o extensions/*.o deps/**/*.o
	rm -rf bin/ dist/ *.cna .aggressor_history .slp_history test_no_rewrite

amalgamate:
	./scripts/amalgamate.sh

amalgamate-single:
	./scripts/amalgamate.sh --single

CFLAGS += -Wno-unused-variable
