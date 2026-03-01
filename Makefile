CC = gcc
CXX = g++

CFLAGS = -std=c99 -Wall -Wextra -Iinclude
CXXFLAGS = -std=c++11 -Wall -Wextra -Iinclude -I.

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)

TEST_SRC = $(wildcard tests/*.cpp)
TEST_OBJ = $(TEST_SRC:.cpp=.o)

.PHONY: all clean test

all: sleepy_lib

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

clean:
	rm -f src/*.o tests/*.o test_runner libsleepy.a
