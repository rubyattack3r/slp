# Makefile for Sleep C implementation core engine
#
# Out-of-source build: all .o and .d files go into build/, binaries into bin/.

# Default to system CC and CXX if not specified
ifeq ($(origin CC),default)
  CC := cc
endif
ifeq ($(origin CXX),default)
  CXX := c++
endif

# Detect if we are targeting Windows (either host or cross-compilation)
IS_WINDOWS := 0
ifeq ($(OS),Windows_NT)
  IS_WINDOWS := 1
else
  ifneq (,$(findstring mingw,$(CC)))
    IS_WINDOWS := 1
  endif
  ifneq (,$(findstring windows,$(CC)))
    IS_WINDOWS := 1
  endif
endif

ifeq ($(IS_WINDOWS),1)
  EXE_EXT := .exe
else
  EXE_EXT :=
endif

# ---------------------------------------------------------------------------
# Directories
# ---------------------------------------------------------------------------
BUILD_DIR = build
BIN_DIR   = bin

# ---------------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------------
SLP_WARNINGS = -Wall -Wextra -Wconversion -Wno-sign-conversion \
               -Wdouble-promotion -Wno-unused-parameter -Wno-unused-function

SLP_INCLUDES = -Iinclude -Ideps/bestline -Iextensions/stdlib -Itools/common

# JNI is opt-in. The ordinary build never probes for Java headers or links
# libjvm; the extension loads the selected runtime dynamically at startup.
JAVA_HOME ?= $(shell /usr/libexec/java_home 2>/dev/null)
ifeq ($(IS_WINDOWS),1)
  JNI_PLATFORM ?= win32
else ifeq ($(shell uname -s),Darwin)
  JNI_PLATFORM ?= darwin
else
  JNI_PLATFORM ?= linux
endif
JVM_INCLUDES = -Iextensions/jvm -I$(JAVA_HOME)/include \
               -I$(JAVA_HOME)/include/$(JNI_PLATFORM)

CFLAGS   = -g3 -O0 -std=c99 $(SLP_WARNINGS) $(SLP_INCLUDES) -fno-sanitize=undefined
CXXFLAGS = -g3 -std=c++11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
           -Iinclude -I. -Iextensions/stdlib -Itools/common -fno-sanitize=undefined
LDLIBS   = -lm
ifeq ($(IS_WINDOWS),1)
  LDLIBS += -lws2_32
  JVM_DYNAMIC_LDLIBS :=
else ifeq ($(shell uname -s),Linux)
  JVM_DYNAMIC_LDLIBS := -ldl
endif

UBSAN_FLAGS = -fsanitize=undefined
ASAN_FLAGS  = -fsanitize=address

ifeq ($(BUILD),debug)
  CFLAGS   += $(UBSAN_FLAGS)
  CXXFLAGS += $(UBSAN_FLAGS)
  LDFLAGS  += $(UBSAN_FLAGS)
endif
ifeq ($(BUILD),debug-asan)
  CFLAGS   += $(UBSAN_FLAGS) $(ASAN_FLAGS)
  CXXFLAGS += $(UBSAN_FLAGS) $(ASAN_FLAGS)
  LDFLAGS  += $(UBSAN_FLAGS) $(ASAN_FLAGS)
endif

# ---------------------------------------------------------------------------
# Sources and objects (out-of-source build)
# ---------------------------------------------------------------------------
SRC = $(wildcard src/*.c)
ifeq ($(IS_WINDOWS),1)
  SRC += src/platform/regex/regcomp.c \
         src/platform/regex/regexec.c \
         src/platform/regex/tre-mem.c
endif
OBJ = $(patsubst src/%.c,$(BUILD_DIR)/src/%.o,$(SRC))

TOOLS_COMMON_OBJ = $(BUILD_DIR)/tools/common/utils.o $(BUILD_DIR)/tools/common/console.o

STDLIB_OBJ = $(BUILD_DIR)/extensions/stdlib/stdlib.o
JVM_OBJ = $(BUILD_DIR)/extensions/jvm/slp_jvm.o
JVM_BRIDGE_JAR = $(BIN_DIR)/slp-jvm-bridge.jar
JVM_LOADABLE_FIXTURE_JAR = $(BIN_DIR)/slp-jvm-test-loadable.jar
SLEEP_JAR ?=

TEST_SRC = $(wildcard tests/*.cpp)
TEST_OBJ = $(patsubst tests/%.cpp,$(BUILD_DIR)/tests/%.o,$(TEST_SRC))

BESTLINE_OBJ = $(BUILD_DIR)/deps/bestline/bestline.o

# ---------------------------------------------------------------------------
# Phony targets
# ---------------------------------------------------------------------------
.PHONY: all clean test test-amalgamation format-check bench amalgamate \
        amalgamate-single slp slp_lib tools test_runner bench_slp slp_fmt \
        slpd debug debug-asan jvm jvm-check jvm-smoke test-jvm \
        jvm-loadable-smoke sleep-jar-check test-jvm-loadable

all: slp_lib slp tools

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
slp_lib: $(BIN_DIR)/libslp.a

$(BIN_DIR)/libslp.a: $(OBJ)
	@mkdir -p $(BIN_DIR)
	ar rcs $@ $(OBJ)

# ---------------------------------------------------------------------------
# Pattern rules (out-of-source)
# ---------------------------------------------------------------------------
$(BUILD_DIR)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/tools/common/%.o: tools/common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/extensions/stdlib/%.o: extensions/stdlib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/extensions/jvm/%.o: extensions/jvm/%.c | jvm-check
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(JVM_INCLUDES) -MMD -MP -c $< -o $@

$(BUILD_DIR)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/deps/bestline/bestline.o: deps/bestline/bestline.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -MMD -MP -c $< -o $@

# Header-dependency tracking
-include $(shell find $(BUILD_DIR) -name '*.d' 2>/dev/null)

# ---------------------------------------------------------------------------
# Interpreter (slp)
# ---------------------------------------------------------------------------
slp: $(BIN_DIR)/slp$(EXE_EXT)

$(BIN_DIR)/slp$(EXE_EXT): $(OBJ) $(STDLIB_OBJ) $(TOOLS_COMMON_OBJ) tools/slp.c $(BESTLINE_OBJ)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tools/slp.c $(BESTLINE_OBJ) $(OBJ) $(STDLIB_OBJ) $(TOOLS_COMMON_OBJ) $(LDLIBS)

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
tools: slp_fmt slpd bench_slp

slp_fmt: $(BIN_DIR)/slp_fmt$(EXE_EXT)

$(BIN_DIR)/slp_fmt$(EXE_EXT): $(OBJ) $(BUILD_DIR)/tools/common/utils.o tools/slp_fmt.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tools/slp_fmt.c $(OBJ) $(BUILD_DIR)/tools/common/utils.o $(LDLIBS)

slpd: $(BIN_DIR)/slpd$(EXE_EXT)

$(BIN_DIR)/slpd$(EXE_EXT): $(OBJ) $(BUILD_DIR)/tools/common/utils.o tools/slpd.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ tools/slpd.c $(OBJ) $(BUILD_DIR)/tools/common/utils.o $(LDLIBS)

bench_slp: $(BIN_DIR)/bench_slp$(EXE_EXT)

$(BIN_DIR)/bench_slp$(EXE_EXT): $(OBJ) $(BUILD_DIR)/tools/common/utils.o tools/bench_slp.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -O2 -o $@ tools/bench_slp.c $(OBJ) $(BUILD_DIR)/tools/common/utils.o $(LDLIBS)

# ---------------------------------------------------------------------------
# Test runner
# ---------------------------------------------------------------------------
test_runner: $(BIN_DIR)/test_runner$(EXE_EXT)

$(BIN_DIR)/test_runner$(EXE_EXT): $(OBJ) $(STDLIB_OBJ) $(TEST_OBJ) $(TOOLS_COMMON_OBJ) $(BESTLINE_OBJ)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: test_runner
	ASAN_OPTIONS=abort_on_error=1:halt_on_error=1 \
	UBSAN_OPTIONS=abort_on_error=1:halt_on_error=1 \
	./$(BIN_DIR)/test_runner$(EXE_EXT)
	./tests/test_amalgamation.sh

# ---------------------------------------------------------------------------
# Optional regular-JVM provider
# ---------------------------------------------------------------------------
jvm: $(BIN_DIR)/libslp-jvm.a $(JVM_BRIDGE_JAR)

jvm-check:
	@test -n "$(JAVA_HOME)" || { echo "JAVA_HOME is required for make jvm" >&2; exit 1; }
	@test -f "$(JAVA_HOME)/include/jni.h" || { echo "jni.h was not found under JAVA_HOME=$(JAVA_HOME)" >&2; exit 1; }
	@test -x "$(JAVA_HOME)/bin/javac" || { echo "javac was not found under JAVA_HOME=$(JAVA_HOME)" >&2; exit 1; }

sleep-jar-check:
	@test -n "$(SLEEP_JAR)" || { echo "SLEEP_JAR is required for the Loadable compatibility test" >&2; exit 1; }
	@test -f "$(SLEEP_JAR)" || { echo "Sleep runtime not found: $(SLEEP_JAR)" >&2; exit 1; }

$(BIN_DIR)/libslp-jvm.a: $(JVM_OBJ)
	@mkdir -p $(BIN_DIR)
	ar rcs $@ $^

JVM_BRIDGE_JAVA = $(wildcard extensions/jvm/java/dev/slp/jvm/*.java)
JVM_BRIDGE_STUBS = $(wildcard extensions/jvm/java-stubs/sleep/*/*.java) \
	$(wildcard extensions/jvm/java-stubs/sleep/*/*/*.java)

$(JVM_BRIDGE_JAR): $(JVM_BRIDGE_JAVA) $(JVM_BRIDGE_STUBS) | jvm-check
	@mkdir -p $(BUILD_DIR)/extensions/jvm/classes $(BIN_DIR)
	$(JAVA_HOME)/bin/javac -Xlint:-options --release 8 -sourcepath extensions/jvm/java-stubs:extensions/jvm/java -d $(BUILD_DIR)/extensions/jvm/classes $(JVM_BRIDGE_JAVA)
	cd $(BUILD_DIR)/extensions/jvm/classes && $(JAVA_HOME)/bin/jar cf $(abspath $@) dev/slp/jvm

$(JVM_LOADABLE_FIXTURE_JAR): extensions/jvm/tests/java/dev/slp/jvm/test/TestLoadable.java | jvm-check sleep-jar-check
	@mkdir -p $(BUILD_DIR)/extensions/jvm/test-loadable-classes $(BIN_DIR)
	$(JAVA_HOME)/bin/javac -Xlint:-options --release 8 -cp "$(SLEEP_JAR)" -d $(BUILD_DIR)/extensions/jvm/test-loadable-classes $<
	cd $(BUILD_DIR)/extensions/jvm/test-loadable-classes && $(JAVA_HOME)/bin/jar cf $(abspath $@) dev/slp/jvm/test

jvm-smoke: $(BIN_DIR)/jvm_smoke$(EXE_EXT)

$(BIN_DIR)/jvm_smoke$(EXE_EXT): $(OBJ) $(STDLIB_OBJ) $(JVM_OBJ) extensions/jvm/tests/jvm_smoke.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(JVM_INCLUDES) $(LDFLAGS) -o $@ extensions/jvm/tests/jvm_smoke.c $(OBJ) $(STDLIB_OBJ) $(JVM_OBJ) $(LDLIBS) $(JVM_DYNAMIC_LDLIBS)

jvm-loadable-smoke: $(BIN_DIR)/jvm_loadable_smoke$(EXE_EXT)

$(BIN_DIR)/jvm_loadable_smoke$(EXE_EXT): $(OBJ) $(STDLIB_OBJ) $(JVM_OBJ) extensions/jvm/tests/jvm_loadable_smoke.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $(JVM_INCLUDES) $(LDFLAGS) -o $@ extensions/jvm/tests/jvm_loadable_smoke.c $(OBJ) $(STDLIB_OBJ) $(JVM_OBJ) $(LDLIBS) $(JVM_DYNAMIC_LDLIBS)

test-jvm: jvm jvm-smoke
	./$(BIN_DIR)/jvm_smoke$(EXE_EXT) "$(JAVA_HOME)" "$(abspath $(JVM_BRIDGE_JAR))"

test-jvm-loadable: jvm jvm-loadable-smoke $(JVM_LOADABLE_FIXTURE_JAR)
	./$(BIN_DIR)/jvm_loadable_smoke$(EXE_EXT) "$(JAVA_HOME)" "$(abspath $(JVM_BRIDGE_JAR))" "$(abspath $(SLEEP_JAR))" "$(abspath $(JVM_LOADABLE_FIXTURE_JAR))" "$(abspath $(BUILD_DIR)/jvm-loadable-unloaded.marker)"

test-amalgamation:
	./tests/test_amalgamation.sh

format-check: slp_fmt
	find tests/fixtures tests/fixtures_vm -name '*.sl' | grep -vE 'argerr.sl|errors[1-5].sl|hoeserror.sl|keyvalueerr.sl|noterm.sl|noterm2.sl|scalref.sl|sillysyntax.sl|warn.sl' | xargs -n1 ./bin/slp_fmt > /dev/null

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
bench: bench_slp
	./$(BIN_DIR)/bench_slp$(EXE_EXT)

# ---------------------------------------------------------------------------
# Sanitizer shortcuts
# ---------------------------------------------------------------------------
debug:
	$(MAKE) BUILD=debug all test

debug-asan:
	$(MAKE) BUILD=debug-asan all test

# ---------------------------------------------------------------------------
# Amalgamation
# ---------------------------------------------------------------------------
amalgamate:
	./scripts/amalgamate.sh

amalgamate-single:
	./scripts/amalgamate.sh --single

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) dist/ .slp_history
