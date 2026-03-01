#include "doctest.h"
#include <string.h>

extern "C" {
#include "sleepy_common.h"
#include "sleepy_core.h"
#include "sleepy_lexer.h"
#include "sleepy_parser.h"
#include <stdlib.h>

void *test_reallocate_parser(void *memory, size_t newSize, void *userData) {
  (void)userData;
  if (newSize == 0) {
    free(memory);
    return NULL;
  }
  return realloc(memory, newSize);
}

static SleepyAllocator test_allocator = {test_reallocate_parser, NULL};
}

TEST_CASE("Parser initializes correctly") {
  const char *source = "if (x == 10) { return true; }";
  SleepyParser parser;
  sleepy_parser_init(&parser, source, &test_allocator);

  CHECK_EQ(parser.had_error, false);
  CHECK_EQ(parser.panic_mode, false);
}

TEST_CASE("Parser parses a true literal correctly") {
  const char *source = "true";
  SleepyParser parser;
  sleepy_parser_init(&parser, source, &test_allocator);

  SleepyASTNode *root = sleepy_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLEEPY_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SleepyASTNode *expr = root->as.block.statements[0];
  CHECK_EQ(expr->type, SLEEPY_AST_BOOLEAN);
  CHECK_EQ(expr->as.boolean, true);
  SLEEPY_FREE(&test_allocator, root->as.block.statements);
  SLEEPY_FREE(&test_allocator, expr);
  SLEEPY_FREE(&test_allocator, root);
}

TEST_CASE("Parser parses a false literal correctly") {
  const char *source = "false";
  SleepyParser parser;
  sleepy_parser_init(&parser, source, &test_allocator);

  SleepyASTNode *root = sleepy_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLEEPY_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SleepyASTNode *expr = root->as.block.statements[0];
  CHECK_EQ(expr->type, SLEEPY_AST_BOOLEAN);
  CHECK_EQ(expr->as.boolean, false);
  SLEEPY_FREE(&test_allocator, root->as.block.statements);
  SLEEPY_FREE(&test_allocator, expr);
  SLEEPY_FREE(&test_allocator, root);
}

TEST_CASE("Parser parses a null literal correctly") {
  const char *source = "$null";
  SleepyParser parser;
  sleepy_parser_init(&parser, source, &test_allocator);

  SleepyASTNode *root = sleepy_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLEEPY_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SleepyASTNode *expr = root->as.block.statements[0];
  CHECK_EQ(expr->type, SLEEPY_AST_NULL);
  SLEEPY_FREE(&test_allocator, root->as.block.statements);
  SLEEPY_FREE(&test_allocator, expr);
  SLEEPY_FREE(&test_allocator, root);
}

TEST_CASE("Parser parses unary operators correctly") {
  const char *source = "!true";
  SleepyParser parser;
  sleepy_parser_init(&parser, source, &test_allocator);

  SleepyASTNode *root = sleepy_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLEEPY_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SleepyASTNode *expr = root->as.block.statements[0];
  CHECK_EQ(expr->type, SLEEPY_AST_UNARYOP);
  CHECK_EQ(expr->as.unaryop.op.type, SLEEPY_TOKEN_BANG);
  REQUIRE(expr->as.unaryop.operand != NULL);
  CHECK_EQ(expr->as.unaryop.operand->type, SLEEPY_AST_BOOLEAN);
  CHECK_EQ(expr->as.unaryop.operand->as.boolean, true);

  SLEEPY_FREE(&test_allocator, root->as.block.statements);

  // Need a mock way to free tree, but just the root for now to prevent massive
  // leaks in test runner
  SLEEPY_FREE(&test_allocator, expr->as.unaryop.operand);
  SLEEPY_FREE(&test_allocator, expr);
  SLEEPY_FREE(&test_allocator, root);
}

TEST_CASE("Parser parses binary operators correctly") {
  const char *source = "1 + 2";
  SleepyParser parser;
  sleepy_parser_init(&parser, source, &test_allocator);

  SleepyASTNode *root = sleepy_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLEEPY_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SleepyASTNode *expr = root->as.block.statements[0];

  CHECK_EQ(expr->type, SLEEPY_AST_BINOP);
  CHECK_EQ(expr->as.binop.op.type, SLEEPY_TOKEN_PLUS);

  REQUIRE(expr->as.binop.left != NULL);
  CHECK_EQ(expr->as.binop.left->type, SLEEPY_AST_NUMBER);

  REQUIRE(expr->as.binop.right != NULL);
  CHECK_EQ(expr->as.binop.right->type, SLEEPY_AST_NUMBER);

  SLEEPY_FREE(&test_allocator, root->as.block.statements);

  SLEEPY_FREE(&test_allocator, expr->as.binop.left);
  SLEEPY_FREE(&test_allocator, expr->as.binop.right);
  SLEEPY_FREE(&test_allocator, expr);

  SLEEPY_FREE(&test_allocator, root);
}

TEST_CASE("Parser parses scalar variables correctly") {
  const char *source = "$myVar";
  SleepyParser parser;
  sleepy_parser_init(&parser, source, &test_allocator);

  SleepyASTNode *root = sleepy_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLEEPY_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SleepyASTNode *expr = root->as.block.statements[0];
  CHECK_EQ(expr->type, SLEEPY_AST_SCALAR);

  // To avoid memory leak in test
  SLEEPY_FREE(&test_allocator, root->as.block.statements);
  SLEEPY_FREE(&test_allocator, expr);
  SLEEPY_FREE(&test_allocator, root);
}

TEST_CASE("Parser parses hash variables correctly") {
  const char *source = "%myHash";
  SleepyParser parser;
  sleepy_parser_init(&parser, source, &test_allocator);

  SleepyASTNode *root = sleepy_parser_parse(&parser);
  CHECK_EQ(parser.had_error, false);
  REQUIRE(root != NULL);
  REQUIRE(root->type == SLEEPY_AST_SCRIPT);
  REQUIRE(root->as.block.count == 1);
  SleepyASTNode *expr = root->as.block.statements[0];
  CHECK_EQ(expr->type, SLEEPY_AST_HASHTABLE);

  SLEEPY_FREE(&test_allocator, root->as.block.statements);
  SLEEPY_FREE(&test_allocator, expr);
  SLEEPY_FREE(&test_allocator, root);
}
