#include "doctest.h"
#include <string.h>
#include <string>

extern "C" {
#include "slp_common.h"
#include "slp_core.h"
#include "slp_lexer.h"
#include "slp_parser.h"
#include "slp_ast.h"
#include <stdlib.h>

void *test_reallocate_parser(void *memory, size_t newSize, void *userData) {
  (void)userData;
  if (newSize == 0) {
    free(memory);
    return NULL;
  }
  return realloc(memory, newSize);
}

static SlpAllocator test_allocator = {test_reallocate_parser, NULL};
}

TEST_CASE("Parser initializes correctly") {
  const char *source = "if (x == 10) { return true; }";
  SlpParser parser;
  slp_parser_init(&parser, source, &test_allocator);

  CHECK_EQ(parser.had_error, false);
  CHECK_EQ(parser.panic_mode, false);
}

TEST_CASE("Parser parses a true literal correctly") {
  const char *source = "true";
  SlpParser parser;
  slp_parser_init(&parser, source, &test_allocator);

  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLP_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SlpASTNode *expr = root->as.block.statements[0];
  CHECK_EQ(expr->type, SLP_AST_BOOLEAN);
  CHECK_EQ(expr->as.boolean, true);
  slp_parser_free_node(root, &test_allocator);
}

TEST_CASE("Parser parses a false literal correctly") {
  const char *source = "false";
  SlpParser parser;
  slp_parser_init(&parser, source, &test_allocator);

  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLP_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SlpASTNode *expr = root->as.block.statements[0];
  CHECK_EQ(expr->type, SLP_AST_BOOLEAN);
  CHECK_EQ(expr->as.boolean, false);
  slp_parser_free_node(root, &test_allocator);
}

TEST_CASE("Parser parses a null literal correctly") {
  const char *source = "$null";
  SlpParser parser;
  slp_parser_init(&parser, source, &test_allocator);

  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLP_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SlpASTNode *expr = root->as.block.statements[0];
  CHECK_EQ(expr->type, SLP_AST_NULL);
  slp_parser_free_node(root, &test_allocator);
}

TEST_CASE("Parser parses unary operators correctly") {
  const char *source = "!true";
  SlpParser parser;
  slp_parser_init(&parser, source, &test_allocator);

  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLP_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SlpASTNode *expr = root->as.block.statements[0];
  CHECK_EQ(expr->type, SLP_AST_UNARYOP);
  CHECK_EQ(expr->as.unaryop.op.type, SLP_TOKEN_BANG);
  REQUIRE(expr->as.unaryop.operand != NULL);
  CHECK_EQ(expr->as.unaryop.operand->type, SLP_AST_BOOLEAN);
  CHECK_EQ(expr->as.unaryop.operand->as.boolean, true);
  slp_parser_free_node(root, &test_allocator);
}

TEST_CASE("Parser parses binary operators correctly") {
  const char *source = "1 + 2";
  SlpParser parser;
  slp_parser_init(&parser, source, &test_allocator);

  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLP_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SlpASTNode *expr = root->as.block.statements[0];

  CHECK_EQ(expr->type, SLP_AST_BINOP);
  CHECK_EQ(expr->as.binop.op.type, SLP_TOKEN_PLUS);

  REQUIRE(expr->as.binop.left != NULL);
  CHECK_EQ(expr->as.binop.left->type, SLP_AST_NUMBER);

  REQUIRE(expr->as.binop.right != NULL);
  CHECK_EQ(expr->as.binop.right->type, SLP_AST_NUMBER);
  slp_parser_free_node(root, &test_allocator);
}

TEST_CASE("Parser parses scalar variables correctly") {
  const char *source = "$myVar";
  SlpParser parser;
  slp_parser_init(&parser, source, &test_allocator);

  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLP_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SlpASTNode *expr = root->as.block.statements[0];
  CHECK_EQ(expr->type, SLP_AST_SCALAR);
  slp_parser_free_node(root, &test_allocator);
}

TEST_CASE("Parser parses hash variables correctly") {
  const char *source = "%myHash";
  SlpParser parser;
  slp_parser_init(&parser, source, &test_allocator);

  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLP_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SlpASTNode *expr = root->as.block.statements[0];
  CHECK_EQ(expr->type, SLP_AST_HASHTABLE);
  slp_parser_free_node(root, &test_allocator);
}

TEST_CASE("Parser parses basic double-quoted string correctly") {
  const char *source = "\"hello world\"";
  SlpParser parser;
  slp_parser_init(&parser, source, &test_allocator);

  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLP_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SlpASTNode *expr = root->as.block.statements[0];

  CHECK_EQ(expr->type, SLP_AST_STRING);
  CHECK_EQ(strcmp(expr->as.string_val, "hello world"), 0);

  slp_parser_free_node(root, &test_allocator);
}

TEST_CASE("Parser desugars double-quoted string with variable correctly") {
  const char *source = "\"hello $name\"";
  SlpParser parser;
  slp_parser_init(&parser, source, &test_allocator);

  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLP_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SlpASTNode *expr = root->as.block.statements[0];

  // Should desugar to: "hello " . $name
  CHECK_EQ(expr->type, SLP_AST_BINOP);
  CHECK_EQ(expr->as.binop.op.type, SLP_TOKEN_DOT);

  SlpASTNode *left = expr->as.binop.left;
  REQUIRE(left != NULL);
  CHECK_EQ(left->type, SLP_AST_STRING);
  CHECK_EQ(strcmp(left->as.string_val, "hello "), 0);

  SlpASTNode *right = expr->as.binop.right;
  REQUIRE(right != NULL);
  CHECK_EQ(right->type, SLP_AST_SCALAR);
  CHECK_EQ(strcmp(right->as.string_val, "name"), 0);

  slp_parser_free_node(root, &test_allocator);
}

TEST_CASE("Parser desugars double-quoted string with inline concatenation correctly") {
  const char *source = "\"aadprt. $+ $barch $+ .o\"";
  SlpParser parser;
  slp_parser_init(&parser, source, &test_allocator);

  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLP_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SlpASTNode *expr = root->as.block.statements[0];

  // Should desugar to left-associative tree: ("aadprt." . $barch) . ".o"
  CHECK_EQ(expr->type, SLP_AST_BINOP);
  CHECK_EQ(expr->as.binop.op.type, SLP_TOKEN_DOT);

  SlpASTNode *right = expr->as.binop.right;
  REQUIRE(right != NULL);
  CHECK_EQ(right->type, SLP_AST_STRING);
  CHECK_EQ(strcmp(right->as.string_val, ".o"), 0);

  SlpASTNode *left_binop = expr->as.binop.left;
  REQUIRE(left_binop != NULL);
  CHECK_EQ(left_binop->type, SLP_AST_BINOP);
  CHECK_EQ(left_binop->as.binop.op.type, SLP_TOKEN_DOT);

  SlpASTNode *var_node = left_binop->as.binop.right;
  REQUIRE(var_node != NULL);
  CHECK_EQ(var_node->type, SLP_AST_SCALAR);
  CHECK_EQ(strcmp(var_node->as.string_val, "barch"), 0);

  SlpASTNode *prefix_node = left_binop->as.binop.left;
  REQUIRE(prefix_node != NULL);
  CHECK_EQ(prefix_node->type, SLP_AST_STRING);
  CHECK_EQ(strcmp(prefix_node->as.string_val, "aadprt."), 0);

  slp_parser_free_node(root, &test_allocator);
}

// ---------------------------------------------------------------------------
// Recursion / size guards (Phase 1: Tier-1 safety)
// ---------------------------------------------------------------------------

TEST_CASE("Parser: deeply nested parentheses error instead of crashing") {
  // Far beyond SLP_PARSER_MAX_DEPTH: would exhaust the C stack without a guard.
  std::string source;
  for (int i = 0; i < 5000; i++) source += '(';
  source += '1';
  for (int i = 0; i < 5000; i++) source += ')';
  source += ';';

  SlpParser parser;
  slp_parser_init(&parser, source.c_str(), &test_allocator);
  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK(parser.had_error == true);
  if (root) slp_parser_free_node(root, &test_allocator);
}

TEST_CASE("Parser: deeply nested blocks error instead of crashing") {
  std::string source;
  for (int i = 0; i < 5000; i++) source += '{';
  for (int i = 0; i < 5000; i++) source += '}';

  SlpParser parser;
  slp_parser_init(&parser, source.c_str(), &test_allocator);
  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK(parser.had_error == true);
  if (root) slp_parser_free_node(root, &test_allocator);
}

TEST_CASE("Parser: string with too many interpolations errors, no overflow") {
  std::string source = "\"";
  for (int i = 0; i < 700; i++) source += "$a"; // 700 interpolated segments
  source += "\";";

  SlpParser parser;
  slp_parser_init(&parser, source.c_str(), &test_allocator);
  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK(parser.had_error == true);
  if (root) slp_parser_free_node(root, &test_allocator);
}

TEST_CASE("Parser parses optional semicolons and newlines correctly") {
  const char *source = 
    "local('$a')\n"
    "$a = 1\n"
    "local('$b')\n"
    "{\n"
    "  $b = 2\n"
    "}\n";
  SlpParser parser;
  slp_parser_init(&parser, source, &test_allocator);

  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  slp_parser_free_node(root, &test_allocator);
}

TEST_CASE("Parser parses optional commas in argument lists correctly") {
  const char *source = "bof_pack($1, \"zz\", $2 $2);";
  SlpParser parser;
  slp_parser_init(&parser, source, &test_allocator);

  SlpASTNode *root = slp_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  slp_parser_free_node(root, &test_allocator);
}


