#include "doctest.h"
#include <string.h>

extern "C" {
#include "slp_common.h"
#include "slp_core.h"
#include "slp_lexer.h"
#include "slp_parser.h"
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
  SLP_FREE(&test_allocator, root->as.block.statements);
  SLP_FREE(&test_allocator, expr);
  SLP_FREE(&test_allocator, root);
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
  SLP_FREE(&test_allocator, root->as.block.statements);
  SLP_FREE(&test_allocator, expr);
  SLP_FREE(&test_allocator, root);
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
  SLP_FREE(&test_allocator, root->as.block.statements);
  SLP_FREE(&test_allocator, expr);
  SLP_FREE(&test_allocator, root);
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

  SLP_FREE(&test_allocator, root->as.block.statements);

  // Need a mock way to free tree, but just the root for now to prevent massive
  // leaks in test runner
  SLP_FREE(&test_allocator, expr->as.unaryop.operand);
  SLP_FREE(&test_allocator, expr);
  SLP_FREE(&test_allocator, root);
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

  SLP_FREE(&test_allocator, root->as.block.statements);

  SLP_FREE(&test_allocator, expr->as.binop.left);
  SLP_FREE(&test_allocator, expr->as.binop.right);
  SLP_FREE(&test_allocator, expr);

  SLP_FREE(&test_allocator, root);
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

  // To avoid memory leak in test
  SLP_FREE(&test_allocator, root->as.block.statements);
  SLP_FREE(&test_allocator, expr);
  SLP_FREE(&test_allocator, root);
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

  SLP_FREE(&test_allocator, root->as.block.statements);
  SLP_FREE(&test_allocator, expr);
  SLP_FREE(&test_allocator, root);
}
