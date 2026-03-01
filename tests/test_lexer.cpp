#include "doctest.h"
#include <string.h>

extern "C" {
#include "sleepy_common.h"
#include "sleepy_core.h"
#include "sleepy_lexer.h"
#include <stdlib.h>

void *test_reallocate(void *memory, size_t newSize, void *userData) {
  (void)userData;
  if (newSize == 0) {
    free(memory);
    return NULL;
  }
  return realloc(memory, newSize);
}

static SleepyAllocator test_allocator = {test_reallocate, NULL};
}

TEST_CASE("Lexer identifies basic control keywords and formatting") {
  const char *source = "if (x == 10) { return true; }";
  SleepyLexer lexer;
  sleepy_lexer_init(&lexer, source, &test_allocator);

  SleepyToken token;

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_IF);

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_LEFT_PAREN);

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_ID);
  CHECK_EQ(token.length, 1);
  CHECK_EQ(token.start[0], 'x');

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_EQ);

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_NUMBER);
  CHECK_EQ(token.length, 2);
  CHECK_EQ(strncmp(token.start, "10", 2), 0);

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_RIGHT_PAREN);

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_LEFT_BRACE);

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_RETURN);

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_TRUE);

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_SEMICOLON);

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_RIGHT_BRACE);

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_EOF);
}

TEST_CASE("Lexer identifies strings, literals, and combinations of hex and "
          "fractional numbers") {
  const char *source = "\"hello\" 3.14 0x1A 'lit'";
  SleepyLexer lexer;
  sleepy_lexer_init(&lexer, source, &test_allocator);

  SleepyToken token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_STRING);
  CHECK_EQ(strncmp(token.start, "\"hello\"", 7), 0);
  CHECK_EQ(token.length, 7);

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_DOUBLE);

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_NUMBER);
  CHECK_EQ(strncmp(token.start, "0x1A", 4), 0);

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_LITERAL);

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_EOF);
}

TEST_CASE("Lexer successfully lexes multiple operators") {
  const char *source = "+ += - -= * *= / /= ** **= ++ -- => <= ==";
  SleepyLexer lexer;
  sleepy_lexer_init(&lexer, source, &test_allocator);

  SleepyToken token;

  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_PLUS);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_PLUSEQUAL);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_MINUS);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_MINUSEQUAL);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_STAR);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_TIMESEQUAL);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_SLASH);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_DIVEQUAL);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_EXP);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_EXPEQUAL);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_INC);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_DEC);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_ARROW);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_LE);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_EQ);
  token = sleepy_lexer_scan_token(&lexer);
  CHECK_EQ(token.type, SLEEPY_TOKEN_EOF);
}
