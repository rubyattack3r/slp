CC ?= gcc
CXX ?= g++

CFLAGS = -g -O0 -std=c99 -Wall -Wextra -Iinclude -Ideps/bestline -Iextensions/aggressor -Iextensions/stdlib -Iextensions/bof -Itools/common
CXXFLAGS = -std=c++11 -Wall -Wextra -Iinclude -I. -Iextensions/aggressor -Iextensions/stdlib -Iextensions/bof -Itools/common

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)

# Tools Common
TOOLS_COMMON_SRC = tools/common/utils.c
TOOLS_COMMON_OBJ = tools/common/utils.o

# Extension: libaggressor
AGGRESSOR_SRC = extensions/aggressor/aggressor.c
AGGRESSOR_OBJ = extensions/aggressor/aggressor.o

# Extension: stdlib
STDLIB_SRC = extensions/stdlib/stdlib.c
STDLIB_OBJ = extensions/stdlib/stdlib.o

# Extension: bof
BOF_SRC = extensions/bof/bof.c extensions/bof/coff.c
BOF_OBJ = extensions/bof/bof.o extensions/bof/coff.o

TEST_SRC = $(wildcard tests/*.cpp)
TEST_OBJ = $(TEST_SRC:.cpp=.o)

.PHONY: all clean test bench amalgamate slp cna_bundler cna_validator aggressor tools slp_lib libaggressor test_runner bench_slp slp_fmt slpd validate

all: slp_lib libaggressor libbof slp tools amalgamate

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

# Static library of the BOF loader
libbof: bin/libbof.a

bin/libbof.a: $(BOF_OBJ)
	@mkdir -p bin
	ar rcs $@ $(BOF_OBJ)


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test runner compiles C++ test files and links with C object files
test_runner: bin/test_runner

bin/test_runner: $(OBJ) $(AGGRESSOR_OBJ) $(STDLIB_OBJ) $(TEST_OBJ) $(TOOLS_COMMON_OBJ)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $^

tests/%.o: tests/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: test_runner
	./bin/test_runner

bench: bench_slp
	./bin/bench_slp

bench_slp: bin/bench_slp

bin/bench_slp: $(OBJ) $(TOOLS_COMMON_OBJ) tools/bench_slp.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -O2 -o $@ tools/bench_slp.c $(OBJ) $(TOOLS_COMMON_OBJ)

slp: bin/slp

bin/slp: $(OBJ) $(STDLIB_OBJ) $(TOOLS_COMMON_OBJ) tools/slp.c deps/bestline/bestline.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/slp.c deps/bestline/bestline.c $(OBJ) $(STDLIB_OBJ) $(TOOLS_COMMON_OBJ)

cna_bundler: bin/cna_bundler

bin/cna_bundler: $(OBJ) $(STDLIB_OBJ) $(TOOLS_COMMON_OBJ) tools/cna_bundler.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/cna_bundler.c $(OBJ) $(STDLIB_OBJ) $(TOOLS_COMMON_OBJ)

$(AGGRESSOR_OBJ): $(AGGRESSOR_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(STDLIB_OBJ): $(STDLIB_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

$(TOOLS_COMMON_OBJ): $(TOOLS_COMMON_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

cna_validator: bin/cna_validator

bin/cna_validator: $(OBJ) $(AGGRESSOR_OBJ) $(STDLIB_OBJ) $(TOOLS_COMMON_OBJ) tools/cna_validator.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/cna_validator.c $(OBJ) $(AGGRESSOR_OBJ) $(STDLIB_OBJ) $(TOOLS_COMMON_OBJ)

aggressor: bin/aggressor

bin/aggressor: $(OBJ) $(AGGRESSOR_OBJ) $(STDLIB_OBJ) $(BOF_OBJ) $(TOOLS_COMMON_OBJ) tools/aggressor.c deps/bestline/bestline.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/aggressor.c deps/bestline/bestline.c $(OBJ) $(AGGRESSOR_OBJ) $(STDLIB_OBJ) $(BOF_OBJ) $(TOOLS_COMMON_OBJ)

slp_fmt: bin/slp_fmt

bin/slp_fmt: $(OBJ) $(TOOLS_COMMON_OBJ) tools/slp_fmt.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/slp_fmt.c $(OBJ) $(TOOLS_COMMON_OBJ)

slpd: bin/slpd

bin/slpd: $(OBJ) $(TOOLS_COMMON_OBJ) tools/slpd.c
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ tools/slpd.c $(OBJ) $(TOOLS_COMMON_OBJ)

tools: cna_bundler cna_validator aggressor slp_fmt slpd bench_slp

validate: tools
	@mkdir -p dist
	./bin/cna_bundler ./third_party -o ./dist/packaged_tools.cna
	./bin/cna_validator ./dist/packaged_tools.cna

clean:
	rm -rf src/*.o tests/*.o extensions/**/*.o extensions/*.o deps/**/*.o tools/common/*.o
	rm -rf bin/ dist/ *.cna .aggressor_history .slp_history test_no_rewrite

amalgamate:
	./scripts/amalgamate.sh

amalgamate-single:
	./scripts/amalgamate.sh --single

CFLAGS += -Wno-unused-variable
