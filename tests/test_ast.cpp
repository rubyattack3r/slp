// No main here, use test_main.cpp
#include "doctest.h"
extern "C" {
#include "sleepy_ast.h"
#include "sleepy_parser.h"
#include "sleepy_utils.h"
}
#include <string.h>

#include <stdlib.h>

static void *test_reallocate(void *ptr, size_t new_size, void *user_data) {
  if (new_size == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, new_size);
}

// Helper to parse and then format back to verify identity or normalization
static char *roundtrip(const char *source) {
  SleepyAllocator allocator;
  allocator.reallocate = test_reallocate;
  allocator.user_data = NULL;

  SleepyParser parser;
  sleepy_parser_init(&parser, source, &allocator);

  SleepyASTNode *node = sleepy_parser_parse(&parser);
  if (!node)
    return NULL;

  char *formatted = sleepy_ast_format(node, &allocator);
  if (formatted) {
    printf("DEBUG: roundtrip output: [%s]\n", formatted);
  } else {
    printf("DEBUG: roundtrip output: NULL\n");
  }

  // We don't free AST for simplicity in tests, but in real usage we would.
  return formatted;
}

TEST_CASE("AST Serialization: Literals and Basic Expressions") {
  SUBCASE("Boolean") {
    char *res = roundtrip("true");
    CHECK(strcmp(res, "true") == 0);
    // Freeing logic (not implemented in roundtrip for brevity, but let's be
    // clean)
  }

  SUBCASE("Long") {
    char *res = roundtrip("123L");
    CHECK(strcmp(res, "123L") == 0);
  }

  SUBCASE("String") {
    char *res = roundtrip("\"hello\"");
    CHECK(strcmp(res, "\"hello\"") == 0);
  }

  SUBCASE("Scalar") {
    char *res = roundtrip("$a");
    CHECK(strcmp(res, "$a") == 0);
  }
}

TEST_CASE("AST Serialization: Control Flow") {
  SUBCASE("If statement") {
    const char *src = "if ($a == 5) {\n    println(\"five\");\n}";
    char *res = roundtrip(src);
    // Note: The unparser might have specific spacing/formatting
    CHECK(!!(res != NULL));
    CHECK(!!(strstr(res, "if ($a == 5)") != NULL));
    CHECK(!!(strstr(res, "println(\"five\")") != NULL));
  }

  SUBCASE("While loop") {
    const char *src = "while ($true) {\n    break;\n}";
    char *res = roundtrip(src);
    CHECK(!!(res != NULL));
    CHECK(!!(strstr(res, "while ($true)") != NULL));
    CHECK(!!(strstr(res, "break") != NULL));
  }
}

TEST_CASE("AST Serialization: Function Calls") {
  SUBCASE("Simple call") {
    char *res = roundtrip("foo(1, 2, 3)");
    // Our parser currently maps numbers to SLEEPY_AST_NUMBER which unparser
    // doesn't handle yet
    CHECK(!!(strstr(res, "foo(1, 2, 3)") != NULL));
  }
}

TEST_CASE("AST Serialization: Environment Bridges") {
  SUBCASE("Alias") {
    const char *src = "alias foo {\n    println(\"hello\");\n}";
    char *res = roundtrip(src);
    CHECK(!!(res != NULL));
    CHECK(!!(strstr(res, "alias foo") != NULL));
    CHECK(!!(strstr(res, "println(\"hello\")") != NULL));
  }
}
