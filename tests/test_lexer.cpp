#include "doctest.h"
#include <string.h>

extern "C" {
#include "slp_common.h"
#include "slp_core.h"
#include "slp_lexer.h"
#include <stdlib.h>

void *test_reallocate(void *memory, size_t newSize, void *userData) {
  (void)userData;
  if (newSize == 0) {
    free(memory);
    return NULL;
  }
  return realloc(memory, newSize);
}

static SlpAllocator test_allocator = {test_reallocate, NULL};
}

TEST_CASE("Lexer identifies basic control keywords and formatting") {
  const char *source = "if (x == 10) { return true; }";
  SlpLexer lexer;
  slp_lexer_init(&lexer, source, &test_allocator);

  SlpToken token;

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_IF);

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_LEFT_PAREN);

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_ID);
  CHECK_EQ(token.length, 1);
  CHECK_EQ(token.start[0], 'x');

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_EQ);

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_NUMBER);
  CHECK_EQ(token.length, 2);
  CHECK_EQ(strncmp(token.start, "10", 2), 0);

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_RIGHT_PAREN);

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_LEFT_BRACE);

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_RETURN);

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_TRUE);

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_SEMICOLON);

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_RIGHT_BRACE);

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_EOF);
}

TEST_CASE("Lexer identifies strings, literals, and combinations of hex and "
          "fractional numbers") {
  const char *source = "\"hello\" 3.14 0x1A 'lit'";
  SlpLexer lexer;
  slp_lexer_init(&lexer, source, &test_allocator);

  SlpToken token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_STRING);
  CHECK_EQ(strncmp(token.start, "\"hello\"", 7), 0);
  CHECK_EQ(token.length, 7);

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_DOUBLE);

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_NUMBER);
  CHECK_EQ(strncmp(token.start, "0x1A", 4), 0);

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_LITERAL);

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_EOF);
}

TEST_CASE("Lexer reports an error token for an unterminated string") {
  const char *source = "\"no closing quote";
  SlpLexer lexer;
  slp_lexer_init(&lexer, source, &test_allocator);
  SlpToken token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_ERROR);
}

TEST_CASE("Lexer reports an error token for an unterminated literal") {
  const char *source = "'no closing tick";
  SlpLexer lexer;
  slp_lexer_init(&lexer, source, &test_allocator);
  SlpToken token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_ERROR);
}

TEST_CASE("Lexer successfully lexes multiple operators") {
  const char *source = "+ += - -= * *= / /= ** **= ++ -- => <= ==";
  SlpLexer lexer;
  slp_lexer_init(&lexer, source, &test_allocator);

  SlpToken token;

  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_PLUS);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_PLUSEQUAL);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_MINUS);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_MINUSEQUAL);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_STAR);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_TIMESEQUAL);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_SLASH);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_DIVEQUAL);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_EXP);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_EXPEQUAL);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_INC);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_DEC);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_ARROW);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_LE);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_EQ);
  token = slp_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLP_TOKEN_EOF);
}
