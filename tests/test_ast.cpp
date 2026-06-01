// No main here, use test_main.cpp
#include "doctest.h"
extern "C" {
#include "slp_ast.h"
#include "slp_parser.h"
#include "slp_utils.h"
}
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void *test_reallocate(void *ptr, size_t new_size, void *user_data) {
  (void)user_data;
  if (new_size == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, new_size);
}

// Helper to parse and then format back to verify identity or normalization
static char *roundtrip(const char *source) {
  SlpAllocator allocator;
  allocator.reallocate = test_reallocate;
  allocator.user_data = NULL;

  SlpParser parser;
  slp_parser_init(&parser, source, &allocator);

  SlpASTNode *node = slp_parser_parse(&parser);
  if (!node)
    return NULL;

  char *formatted = slp_ast_format(node, &allocator);
  if (formatted) {
    printf("DEBUG: roundtrip output: [%s]\n", formatted);
  } else {
    printf("DEBUG: roundtrip output: NULL\n");
  }

  slp_parser_free_node(node, &allocator);
  return formatted;
}

TEST_CASE("AST Serialization: Literals and Basic Expressions") {
  SUBCASE("Boolean") {
    char *res = roundtrip("true");
    CHECK(strcmp(res, "true;") == 0);
    // Freeing logic (not implemented in roundtrip for brevity, but let's be
    // clean)
    test_reallocate(res, 0, NULL);
}

  SUBCASE("Long") {
    char *res = roundtrip("123L");
    CHECK(strcmp(res, "123L;") == 0);
    test_reallocate(res, 0, NULL);
}

  SUBCASE("String") {
    char *res = roundtrip("\"hello\"");
    CHECK(strcmp(res, "\"hello\";") == 0);
    test_reallocate(res, 0, NULL);
}

  SUBCASE("String Escaping") {
    char *res = roundtrip("\"hello \\\"world\\\"\\\\\"");
    CHECK(strcmp(res, "\"hello \\\"world\\\"\\\\\";") == 0);
    test_reallocate(res, 0, NULL);
}

  SUBCASE("Scalar") {
    char *res = roundtrip("$a");
    CHECK(strcmp(res, "$a;") == 0);
    test_reallocate(res, 0, NULL);
}

  SUBCASE("Hashtable and Array assignments") {
    const char *src = "%hash = %();\n@array = @();";
    char *res = roundtrip(src);
    CHECK(!!(res != NULL));
    CHECK(!!(strstr(res, "%hash = %()") != NULL));
    CHECK(!!(strstr(res, "@array = @()") != NULL));
    test_reallocate(res, 0, NULL);
}

  SUBCASE("Tuple assignment") {
    const char *src = "($a, $b) = @(1, 2);";
    char *res = roundtrip(src);
    CHECK(!!(res != NULL));
    CHECK(!!(strstr(res, "($a, $b) = ") != NULL));
    test_reallocate(res, 0, NULL);
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
    test_reallocate(res, 0, NULL);
  }

  SUBCASE("While loop") {
    const char *src = "while ($true) {\n    break;\n}";
    char *res = roundtrip(src);
    CHECK(!!(res != NULL));
    CHECK(!!(strstr(res, "while ($true)") != NULL));
    CHECK(!!(strstr(res, "break") != NULL));
    test_reallocate(res, 0, NULL);
  }

  SUBCASE("Foreach loop") {
    const char *src = "foreach $a => $b (@arr) {\n    break;\n}";
    char *res = roundtrip(src);
    CHECK(!!(res != NULL));
    CHECK(!!(strstr(res, "foreach $a => $b") != NULL)); // tests the `$` prefix fix
    test_reallocate(res, 0, NULL);
  }
}

TEST_CASE("AST Serialization: Function Calls") {
  SUBCASE("Simple call") {
    char *res = roundtrip("foo(1, 2, 3)");
    // Our parser currently maps numbers to SLP_AST_NUMBER which unparser
    // doesn't handle yet
    CHECK(!!(strstr(res, "foo(1, 2, 3)") != NULL));
    test_reallocate(res, 0, NULL);
}

  SUBCASE("Named arguments call") {
    char *res = roundtrip("foo(name => \"val\", b => 2)");
    CHECK(!!(res != NULL));
    CHECK(!!(strstr(res, "name => \"val\"") != NULL));
    test_reallocate(res, 0, NULL);
}
}

TEST_CASE("AST Serialization: Environment Bridges") {
  SUBCASE("Alias") {
    const char *src = "alias foo {\n    println(\"hello\");\n}";
    char *res = roundtrip(src);
    CHECK(!!(res != NULL));
    CHECK(!!(strstr(res, "alias foo") != NULL));
    CHECK(!!(strstr(res, "println(\"hello\")") != NULL));
    test_reallocate(res, 0, NULL);
  }
}

TEST_CASE("AST Serialization: Advanced and Edge Cases") {
  SUBCASE("For Loop") {
    const char *src = "for ($i = 0; $i < 10; $i = $i + 1) {\n    println($i);\n}";
    char *res = roundtrip(src);
    CHECK(!!(res != NULL));
    CHECK(!!(strstr(res, "for ($i = 0; $i < 10; $i = $i + 1)") != NULL));
    CHECK(!!(strstr(res, "println($i)") != NULL));
    test_reallocate(res, 0, NULL);
  }

  SUBCASE("Try-Catch Block") {
    const char *src = "try {\n    foo();\n} catch $ex {\n    bar($ex);\n}";
    char *res = roundtrip(src);
    CHECK(!!(strstr(res, "try") != NULL));
    CHECK(!!(strstr(res, "catch $ex") != NULL));
    CHECK(!!(strstr(res, "bar($ex)") != NULL));
    test_reallocate(res, 0, NULL);
  }

  SUBCASE("Nested Binary Operations") {
    const char *src = "1 + 2 * 3;";
    char *res = roundtrip(src);
    CHECK(!!(res != NULL));
    CHECK(!!(strstr(res, "1 + 2 * 3") != NULL));
    test_reallocate(res, 0, NULL);
  }
}
