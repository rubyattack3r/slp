#include "sleepy_lexer.h"
#include "sleepy_utils.h"
#include <stdio.h>

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_at_end(SleepyLexer *lexer) { return *lexer->current == '\0'; }

static char advance(SleepyLexer *lexer) {
  lexer->current++;
  return lexer->current[-1];
}

static char peek(SleepyLexer *lexer) { return *lexer->current; }

static char peek_next(SleepyLexer *lexer) {
  if (is_at_end(lexer))
    return '\0';
  return lexer->current[1];
}

static bool match(SleepyLexer *lexer, char expected) {
  if (is_at_end(lexer))
    return false;
  if (*lexer->current != expected)
    return false;
  lexer->current++;
  return true;
}

static SleepyToken make_token(SleepyLexer *lexer, SleepyTokenType type) {
  SleepyToken token;
  token.type = type;
  token.start = lexer->start;
  token.length = (size_t)(lexer->current - lexer->start);
  token.line = lexer->line;
  return token;
}

static SleepyToken error_token(SleepyLexer *lexer, const char *message) {
  SleepyToken token;
  token.type = SLEEPY_TOKEN_ERROR;
  token.start = message;
  size_t len = 0;
  while (message[len] != '\0') {
    len++;
  }
  token.length = len;
  token.line = lexer->line;
  return token;
}

static void skip_whitespace(SleepyLexer *lexer) {
  for (;;) {
    char c = peek(lexer);
    switch (c) {
    case ' ':
    case '\r':
    case '\t':
      advance(lexer);
      break;
    case '\n':
      lexer->line++;
      advance(lexer);
      break;
    case '#': // Comment
      while (peek(lexer) != '\n' && !is_at_end(lexer))
        advance(lexer);
      break;
    default:
      return;
    }
  }
}

static SleepyTokenType check_keyword(SleepyLexer *lexer, int start, int length,
                                     const char *rest, SleepyTokenType type) {
  if (lexer->current - lexer->start == start + length) {
    // Compare string piece by piece
    for (int i = 0; i < length; i++) {
      if (lexer->start[start + i] != rest[i]) {
        return SLEEPY_TOKEN_ID;
      }
    }
    return type;
  }
  return SLEEPY_TOKEN_ID;
}

#define CHECK_KEYWORD(start, length, rest, type)                               \
  check_keyword(lexer, start, length, rest, type)

static SleepyTokenType identifier_type(SleepyLexer *lexer) {
  switch (lexer->start[0]) {
  case 'a':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 's':
        return CHECK_KEYWORD(2, 4, "sert", SLEEPY_TOKEN_ASSERT);
      case 'd':
        return CHECK_KEYWORD(2, 4, "dAll", SLEEPY_TOKEN_ADDALL);
      }
    }
    break;
  case 'b':
    return CHECK_KEYWORD(1, 4, "reak", SLEEPY_TOKEN_BREAK);
  case 'c':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'a':
        if (lexer->current - lexer->start > 2) {
          switch (lexer->start[2]) {
          case 'l':
            return CHECK_KEYWORD(3, 3, "lcc", SLEEPY_TOKEN_CALLCC);
          case 't':
            return CHECK_KEYWORD(3, 2, "ch", SLEEPY_TOKEN_CATCH);
          }
        }
        break;
      case 'o':
        if (lexer->current - lexer->start > 2 && lexer->start[2] == 'p' &&
            lexer->current - lexer->start == 4 && lexer->start[3] == 'y')
          return SLEEPY_TOKEN_COPY;
        return CHECK_KEYWORD(2, 6, "ntinue", SLEEPY_TOKEN_CONTINUE);
      }
    }
    break;
  case 'd':
    return CHECK_KEYWORD(1, 3, "one", SLEEPY_TOKEN_DONE);
  case 'e':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'l':
        // Only return ELSE if it's exactly "else"
        if (lexer->current - lexer->start == 4 &&
            lexer->start[2] == 's' && lexer->start[3] == 'e')
          return SLEEPY_TOKEN_ELSE;
        break;
      }
    }
    break;
  case 'f':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'a':
        return CHECK_KEYWORD(2, 3, "lse", SLEEPY_TOKEN_FALSE);
      case 'o':
        if (lexer->current - lexer->start > 2) {
          switch (lexer->start[2]) {
          case 'r':
            if (lexer->current - lexer->start == 3)
              return SLEEPY_TOKEN_FOR;
            return CHECK_KEYWORD(3, 4, "each", SLEEPY_TOKEN_FOREACH);
          }
        }
        break;
      case 'r':
        return CHECK_KEYWORD(2, 2, "om", SLEEPY_TOKEN_FROM);
      }
    }
    break;
  case 'h':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'a':
        // Only return HALT if it's exactly "halt"
        if (lexer->current - lexer->start == 4 &&
            lexer->start[2] == 'l' && lexer->start[3] == 't')
          return SLEEPY_TOKEN_HALT;
        break;
      }
    }
    break;
  case 'i':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'f':
        return CHECK_KEYWORD(2, 0, "", SLEEPY_TOKEN_IF);
      case 'm':
        return CHECK_KEYWORD(2, 4, "port", SLEEPY_TOKEN_IMPORT);
      case 'l':
        return CHECK_KEYWORD(2, 4, "ine", SLEEPY_TOKEN_INLINE);
      case 'n':
        return SLEEPY_TOKEN_ID;
      }
    }
    break;
  case 'p':
    return SLEEPY_TOKEN_ID;
  case 'r':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'e':
        if (lexer->current - lexer->start > 2) {
          switch (lexer->start[2]) {
          case 't':
            if (lexer->current - lexer->start == 9)
              return CHECK_KEYWORD(3, 6, "ainAll", SLEEPY_TOKEN_RETAINALL);
            return CHECK_KEYWORD(3, 3, "urn", SLEEPY_TOKEN_RETURN);
          case 'm':
            return CHECK_KEYWORD(3, 6, "oveAll", SLEEPY_TOKEN_REMOVEALL);
          }
        }
        break;
      }
    }
    break;
  case 's':
    return CHECK_KEYWORD(1, 2, "ub", SLEEPY_TOKEN_SUB);
  case 't':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'h':
        return CHECK_KEYWORD(2, 3, "row", SLEEPY_TOKEN_THROW);
      case 'r':
        if (lexer->current - lexer->start == 3) {
          if (lexer->start[2] == 'y')
            return SLEEPY_TOKEN_TRY;
        } else {
          return CHECK_KEYWORD(2, 2, "ue", SLEEPY_TOKEN_TRUE);
        }
      }
    }
    break;
  case 'w':
    return CHECK_KEYWORD(1, 4, "hile", SLEEPY_TOKEN_WHILE);
  case 'x':
    // 'x' by itself can be the repetition operator, but only in certain contexts
    // If it's followed by a number, string, literal, or identifier (but not '=' or '=>'), it's the operator
    if (lexer->current - lexer->start == 1) {
      char next = peek(lexer);
      // Don't treat as operator if followed by '=' (for '=>' or 'x=')
      if (next != '=' && (is_digit(next) || next == '"' || next == '\'' ||
          next == '_' || is_alpha(next) || next == '$' || next == '@' || next == '%')) {
        return SLEEPY_TOKEN_LOWER_X;
      }
    }
    break;
  case 'y':
    return CHECK_KEYWORD(1, 4, "ield", SLEEPY_TOKEN_YIELD);
  }

  // Check for built-in binary predicates
  const char *preds[] = {"cmp", "eq",   "gt",      "hasmatch", "is",
                         "isa", "isin", "ismatch", "iswm",     "in",
                         "lt",  "ne",   NULL};
  for (int i = 0; preds[i] != NULL; i++) {
    size_t len = sleepy_utils_strlen(preds[i]);
    if ((size_t)(lexer->current - lexer->start) == len) {
      bool word_match = true;
      for (size_t j = 0; j < len; j++) {
        if (lexer->start[j] != preds[i][j]) {
          word_match = false;
          break;
        }
      }
      if (word_match)
        return SLEEPY_TOKEN_BUILTIN_BINARY_PREDICATE_BRIDGE;
    }
  }

  return SLEEPY_TOKEN_ID;
}

static SleepyToken scan_identifier(SleepyLexer *lexer) {
  while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) || peek(lexer) == '-' ||
         peek(lexer) == '+') {
    advance(lexer);
  }
  return make_token(lexer, identifier_type(lexer));
}

static SleepyToken scan_number(SleepyLexer *lexer) {
  if (*lexer->start == '0' && (peek(lexer) == 'x' || peek(lexer) == 'X')) {
    advance(lexer); // consume 'x'
    while (is_digit(peek(lexer)) ||
           (peek(lexer) >= 'a' && peek(lexer) <= 'f') ||
           (peek(lexer) >= 'A' && peek(lexer) <= 'F')) {
      advance(lexer);
    }
  } else {
    while (is_digit(peek(lexer)))
      advance(lexer);

    // Look for a fractional part.
    if (peek(lexer) == '.' && is_digit(peek_next(lexer))) {
      // Consume the "."
      advance(lexer);
      while (is_digit(peek(lexer)))
        advance(lexer);
      return make_token(lexer, SLEEPY_TOKEN_DOUBLE);
    }
  }

  if (peek(lexer) == 'L' || peek(lexer) == 'l') {
    advance(lexer);
    return make_token(lexer, SLEEPY_TOKEN_LONG);
  }

  return make_token(lexer, SLEEPY_TOKEN_NUMBER);
}

static SleepyToken scan_string(SleepyLexer *lexer, char quote,
                               SleepyTokenType string_type) {
  while (peek(lexer) != quote && !is_at_end(lexer)) {
    if (peek(lexer) == '\n')
      lexer->line++;
    if (peek(lexer) == '\\') {
      advance(lexer); // skip the escape
    }
    advance(lexer);
  }

  if (is_at_end(lexer))
    return error_token(lexer, "Unterminated string.");

  // The closing quote.
  advance(lexer);
  return make_token(lexer, string_type);
}

void sleepy_lexer_init(SleepyLexer *lexer, const char *source,
                       SleepyAllocator *allocator) {
  lexer->start = source;
  lexer->current = source;
  lexer->line = 1;
  lexer->allocator = allocator;
}

SleepyToken sleepy_lexer_scan_token(SleepyLexer *lexer) {
  skip_whitespace(lexer);
  lexer->start = lexer->current;

  if (is_at_end(lexer))
    return make_token(lexer, SLEEPY_TOKEN_EOF);

  char c = advance(lexer);

  if (is_alpha(c))
    return scan_identifier(lexer);
  if (is_digit(c))
    return scan_number(lexer);

  switch (c) {
  case '(':
    return make_token(lexer, SLEEPY_TOKEN_LEFT_PAREN);
  case ')':
    return make_token(lexer, SLEEPY_TOKEN_RIGHT_PAREN);
  case '{':
    return make_token(lexer, SLEEPY_TOKEN_LEFT_BRACE);
  case '}':
    return make_token(lexer, SLEEPY_TOKEN_RIGHT_BRACE);
  case '[':
    return make_token(lexer, SLEEPY_TOKEN_LEFT_BRACKET);
  case ']':
    return make_token(lexer, SLEEPY_TOKEN_RIGHT_BRACKET);
  case ';':
    return make_token(lexer, SLEEPY_TOKEN_SEMICOLON);
  case ',':
    return make_token(lexer, SLEEPY_TOKEN_COMMA);
  case ':':
    return make_token(lexer, SLEEPY_TOKEN_COLON);
  case '@':
    if (is_alpha(peek(lexer)) || is_digit(peek(lexer)) || peek(lexer) == '_') {
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_')
        advance(lexer);
    }
    return make_token(lexer, SLEEPY_TOKEN_AT);
  case '\\':
    if (peek(lexer) == '&') {
      advance(lexer); // consume &
      if (is_alpha(peek(lexer)) || peek(lexer) == '_') {
        while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
               peek(lexer) == '_')
          advance(lexer);
        return make_token(lexer, SLEEPY_TOKEN_ADDRESS);
      }
    }
    if (peek(lexer) == '$' || peek(lexer) == '@' || peek(lexer) == '%') {
      advance(lexer); // consume symbol
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_')
        advance(lexer);
      return make_token(lexer, SLEEPY_TOKEN_ARG_PASSED_BY_NAME);
    }
    return make_token(lexer, SLEEPY_TOKEN_BACKSLASH);
  case '%':
    if (is_alpha(peek(lexer)) || is_digit(peek(lexer)) || peek(lexer) == '_') {
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_')
        advance(lexer);
    }
    return make_token(lexer, SLEEPY_TOKEN_PERCENT);

  case '.':
    if (match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_CATEQUAL);
    return make_token(lexer, SLEEPY_TOKEN_DOT);
  case '-':
    if (match(lexer, '-'))
      return make_token(lexer, SLEEPY_TOKEN_DEC);
    if (match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_MINUSEQUAL);
    if (is_alpha(peek(lexer))) {
      // It's a unary predicate like -isarray or -f
      // Consume the identifier after the dash
      advance(lexer);
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) || peek(lexer) == '_')
        advance(lexer);
      return make_token(lexer, SLEEPY_TOKEN_UNARY_PREDICATE_BRIDGE);
    }
    return make_token(lexer, SLEEPY_TOKEN_MINUS);
  case '+':
    if (match(lexer, '+'))
      return make_token(lexer, SLEEPY_TOKEN_INC);
    if (match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_PLUSEQUAL);
    return make_token(lexer, SLEEPY_TOKEN_PLUS);
  case '/':
    if (match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_DIVEQUAL);
    return make_token(lexer, SLEEPY_TOKEN_SLASH);
  case '*':
    if (match(lexer, '*')) {
      if (match(lexer, '='))
        return make_token(lexer, SLEEPY_TOKEN_EXPEQUAL);
      return make_token(lexer, SLEEPY_TOKEN_EXP);
    }
    if (match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_TIMESEQUAL);
    return make_token(lexer, SLEEPY_TOKEN_STAR);
  case '^':
    if (match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_XOREQUAL);
    if (is_alpha(peek(lexer)) || peek(lexer) == '_' || peek(lexer) == '$') {
      // Consume the class name part
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_' || peek(lexer) == '$' || peek(lexer) == '.') {
        // If the next char is '.' followed by a quote, stop - that's member access
        if (peek(lexer) == '.' && (peek_next(lexer) == '"' || peek_next(lexer) == '\''))
          break;
        advance(lexer);
      }
      return make_token(lexer, SLEEPY_TOKEN_CLASS_LITERAL);
    }
    return make_token(lexer, SLEEPY_TOKEN_CARET);
  case '|':
    if (match(lexer, '|'))
      return make_token(lexer, SLEEPY_TOKEN_LOR);
    if (match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_OREQUAL);
    return make_token(lexer, SLEEPY_TOKEN_PIPE);
  case '&':
    if (match(lexer, '&'))
      return make_token(lexer, SLEEPY_TOKEN_LAND);
    if (match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_ANDEQUAL);
    if (is_alpha(peek(lexer)) || peek(lexer) == '_') {
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_')
        advance(lexer);
      return make_token(lexer, SLEEPY_TOKEN_ADDRESS);
    }
    return make_token(lexer, SLEEPY_TOKEN_AMPERSAND);
  case '!':
    if (match(lexer, '=')) {
      if (match(lexer, '~'))
        return make_token(lexer, SLEEPY_TOKEN_NEQI);
      return make_token(lexer, SLEEPY_TOKEN_NE);
    }
    return make_token(lexer, SLEEPY_TOKEN_BANG);
  case '=':
    if (match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_EQ);
    if (match(lexer, '~'))
      return make_token(lexer, SLEEPY_TOKEN_EQI);
    if (match(lexer, '>'))
      return make_token(lexer, SLEEPY_TOKEN_ARROW);
    return make_token(lexer, SLEEPY_TOKEN_EQUAL);
  case '<':
    if (match(lexer, '=')) {
      if (match(lexer, '>'))
        return make_token(lexer, SLEEPY_TOKEN_SPACESHIP);
      return make_token(lexer, SLEEPY_TOKEN_LE);
    }
    if (match(lexer, '<')) {
      if (match(lexer, '='))
        return make_token(lexer, SLEEPY_TOKEN_LSHIFTEQUAL);
      return make_token(lexer, SLEEPY_TOKEN_LSHIFT);
    }
    return make_token(lexer, SLEEPY_TOKEN_LESS);
  case '>':
    if (match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_GE);
    if (match(lexer, '>')) {
      if (match(lexer, '='))
        return make_token(lexer, SLEEPY_TOKEN_RSHIFTEQUAL);
      return make_token(lexer, SLEEPY_TOKEN_RSHIFT);
    }
    return make_token(lexer, SLEEPY_TOKEN_GREATER);

  case '"':
    return scan_string(lexer, '"', SLEEPY_TOKEN_STRING);
  case '\'':
    return scan_string(lexer, '\'', SLEEPY_TOKEN_LITERAL);
  case '`':
    return scan_string(lexer, '`', SLEEPY_TOKEN_BACKTICK_EXPR);

  case '$':
    if (match(lexer, 'n') && match(lexer, 'u') && match(lexer, 'l') &&
        match(lexer, 'l')) {
      return make_token(lexer, SLEEPY_TOKEN_NULL);
    }
    // Check for special scalar $+
    if (peek(lexer) == '+') {
      advance(lexer);
      return make_token(lexer, SLEEPY_TOKEN_SCALAR);
    }
    // Otherwise parse normal SCALAR
    while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) || peek(lexer) == '_')
      advance(lexer);
    return make_token(lexer, SLEEPY_TOKEN_SCALAR);
  }

  return error_token(lexer, "Unexpected character.");
}

const char *sleepy_lexer_copy_lexeme(SleepyLexer *lexer, SleepyToken *token) {
  char *lexeme = (char *)lexer->allocator->reallocate(
      NULL, token->length + 1, lexer->allocator->user_data);
  if (lexeme == NULL)
    return NULL;
  sleepy_utils_memcpy(lexeme, token->start, token->length);
  lexeme[token->length] = '\0';
  return lexeme;
}
