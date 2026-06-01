#include "slp_lexer.h"
#include "slp_utils.h"
#include <stdbool.h>
#include <stdio.h>

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_at_end(SlpLexer *lexer) { return *lexer->current == '\0'; }

static char lexer_advance(SlpLexer *lexer) {
  lexer->current++;
  return lexer->current[-1];
}

static char peek(SlpLexer *lexer) { return *lexer->current; }

static char peek_next(SlpLexer *lexer) {
  if (is_at_end(lexer))
    return '\0';
  return lexer->current[1];
}

static bool lexer_match(SlpLexer *lexer, char expected) {
  if (is_at_end(lexer))
    return false;
  if (*lexer->current != expected)
    return false;
  lexer->current++;
  return true;
}

static SlpToken make_token(SlpLexer *lexer, SlpTokenType type) {
  SlpToken token;
  token.type = type;
  token.start = lexer->start;
  token.length = (size_t)(lexer->current - lexer->start);
  token.line = lexer->line;
  return token;
}

static SlpToken error_token(SlpLexer *lexer, const char *message) {
  SlpToken token;
  token.type = SLP_TOKEN_ERROR;
  token.start = message;
  size_t len = 0;
  while (message[len] != '\0') {
    len++;
  }
  token.length = len;
  token.line = lexer->line;
  return token;
}

static void skip_whitespace(SlpLexer *lexer) {
  for (;;) {
    char c = peek(lexer);
    switch (c) {
    case ' ':
    case '\r':
    case '\t':
      lexer_advance(lexer);
      break;
    case '\n':
      lexer->line++;
      lexer_advance(lexer);
      break;
    case '#': // Comment
      while (peek(lexer) != '\n' && !is_at_end(lexer))
        lexer_advance(lexer);
      break;
    default:
      return;
    }
  }
}

static SlpTokenType check_keyword(SlpLexer *lexer, int start, int length,
                                     const char *rest, SlpTokenType type) {
  if (lexer->current - lexer->start == start + length) {
    // Compare string piece by piece
    for (int i = 0; i < length; i++) {
      if (lexer->start[start + i] != rest[i]) {
        return SLP_TOKEN_ID;
      }
    }
    return type;
  }
  return SLP_TOKEN_ID;
}

#define CHECK_KEYWORD(start, length, rest, type)                               \
  check_keyword(lexer, start, length, rest, type)

static SlpTokenType identifier_type(SlpLexer *lexer) {
  // Check for built-in binary predicates
  const char *preds[] = {"cmp", "eq",   "gt",      "hasmatch", "is",
                         "isa", "isin", "ismatch", "iswm",     "in",
                         "lt",  "ne",   NULL};
  for (int i = 0; preds[i] != NULL; i++) {
    size_t len = slp_utils_strlen(preds[i]);
    if ((size_t)(lexer->current - lexer->start) == len) {
      bool word_match = true;
      for (size_t j = 0; j < len; j++) {
        if (lexer->start[j] != preds[i][j]) {
          word_match = false;
          break;
        }
      }
      if (word_match)
        return SLP_TOKEN_BUILTIN_BINARY_PREDICATE_BRIDGE;
    }
  }

  switch (lexer->start[0]) {
  case 'a':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 's':
        return CHECK_KEYWORD(2, 4, "sert", SLP_TOKEN_ASSERT);
      case 'd':
        return CHECK_KEYWORD(2, 4, "dAll", SLP_TOKEN_ADDALL);
      }
    }
    break;
  case 'b':
    return CHECK_KEYWORD(1, 4, "reak", SLP_TOKEN_BREAK);
  case 'c':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'a':
        if (lexer->current - lexer->start > 2) {
          switch (lexer->start[2]) {
          case 'l':
            return CHECK_KEYWORD(3, 3, "lcc", SLP_TOKEN_CALLCC);
          case 't':
            return CHECK_KEYWORD(3, 2, "ch", SLP_TOKEN_CATCH);
          }
        }
        break;
      case 'o':
        if (lexer->current - lexer->start > 2 && lexer->start[2] == 'p' &&
            lexer->current - lexer->start == 4 && lexer->start[3] == 'y')
          return SLP_TOKEN_COPY;
        return CHECK_KEYWORD(2, 6, "ntinue", SLP_TOKEN_CONTINUE);
      }
    }
    break;
  case 'd':
    return CHECK_KEYWORD(1, 3, "one", SLP_TOKEN_DONE);
  case 'e':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'l':
        // Only return ELSE if it's exactly "else"
        if (lexer->current - lexer->start == 4 && lexer->start[2] == 's' &&
            lexer->start[3] == 'e')
          return SLP_TOKEN_ELSE;
        break;
      }
    }
    break;
  case 'f':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'a':
        return CHECK_KEYWORD(2, 3, "lse", SLP_TOKEN_FALSE);
      case 'o':
        if (lexer->current - lexer->start > 2) {
          switch (lexer->start[2]) {
          case 'r':
            if (lexer->current - lexer->start == 3)
              return SLP_TOKEN_FOR;
            return CHECK_KEYWORD(3, 4, "each", SLP_TOKEN_FOREACH);
          }
        }
        break;
      case 'r':
        return CHECK_KEYWORD(2, 2, "om", SLP_TOKEN_FROM);
      }
    }
    break;
  case 'h':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'a':
        // Only return HALT if it's exactly "halt"
        if (lexer->current - lexer->start == 4 && lexer->start[2] == 'l' &&
            lexer->start[3] == 't')
          return SLP_TOKEN_HALT;
        break;
      }
    }
    break;
  case 'i':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'f':
        return CHECK_KEYWORD(2, 0, "", SLP_TOKEN_IF);
      case 'm':
        return CHECK_KEYWORD(2, 4, "port", SLP_TOKEN_IMPORT);
      case 'l':
        return CHECK_KEYWORD(2, 4, "ine", SLP_TOKEN_INLINE);
      case 'n':
        return SLP_TOKEN_ID;
      }
    }
    break;
  case 'p':
    return SLP_TOKEN_ID;
  case 'r':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'e':
        if (lexer->current - lexer->start > 2) {
          switch (lexer->start[2]) {
          case 't':
            if (lexer->current - lexer->start == 9)
              return CHECK_KEYWORD(3, 6, "ainAll", SLP_TOKEN_RETAINALL);
            return CHECK_KEYWORD(3, 3, "urn", SLP_TOKEN_RETURN);
          case 'm':
            return CHECK_KEYWORD(3, 6, "oveAll", SLP_TOKEN_REMOVEALL);
          }
        }
        break;
      }
    }
    break;
  case 's':
    return CHECK_KEYWORD(1, 2, "ub", SLP_TOKEN_SUB);
  case 't':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'h':
        return CHECK_KEYWORD(2, 3, "row", SLP_TOKEN_THROW);
      case 'r':
        if (lexer->current - lexer->start == 3) {
          if (lexer->start[2] == 'y')
            return SLP_TOKEN_TRY;
        } else {
          return CHECK_KEYWORD(2, 2, "ue", SLP_TOKEN_TRUE);
        }
      }
    }
    break;
  case 'w':
    return CHECK_KEYWORD(1, 4, "hile", SLP_TOKEN_WHILE);
  case 'x':
    // 'x' by itself can be the repetition operator, but only in certain
    // contexts If it's followed by a number, string, literal, or identifier
    // (but not '=' or '=>'), it's the operator
    if (lexer->current - lexer->start == 1) {
      // Look ahead past whitespace to determine if this is the operator
      const char *lookahead = lexer->current;
      while (*lookahead == ' ' || *lookahead == '\t' || *lookahead == '\r' ||
             *lookahead == '\n') {
        lookahead++;
      }
      char next = *lookahead;
      // Don't treat as operator if followed by '=' (for '=>' or 'x=')
      if (next != '=' &&
          (is_digit(next) || next == '"' || next == '\'' || next == '`' ||
           next == '_' || is_alpha(next) || next == '$' || next == '@' ||
           next == '%' || next == '(' || next == '[' || next == '{')) {
        return SLP_TOKEN_LOWER_X;
      }
    }
    break;
  case 'y':
    return CHECK_KEYWORD(1, 4, "ield", SLP_TOKEN_YIELD);
  }

  return SLP_TOKEN_ID;
}

static SlpToken scan_identifier(SlpLexer *lexer) {
  while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) || peek(lexer) == '-' ||
         peek(lexer) == '+') {
    lexer_advance(lexer);
  }
  return make_token(lexer, identifier_type(lexer));
}

static SlpToken scan_number(SlpLexer *lexer) {
  if (*lexer->start == '0' && (peek(lexer) == 'x' || peek(lexer) == 'X')) {
    lexer_advance(lexer); // consume 'x'
    while (is_digit(peek(lexer)) ||
           (peek(lexer) >= 'a' && peek(lexer) <= 'f') ||
           (peek(lexer) >= 'A' && peek(lexer) <= 'F')) {
      lexer_advance(lexer);
    }
  } else {
    while (is_digit(peek(lexer)))
      lexer_advance(lexer);

    // Look for a fractional part.
    if (peek(lexer) == '.' && is_digit(peek_next(lexer))) {
      // Consume the "."
      lexer_advance(lexer);
      while (is_digit(peek(lexer)))
        lexer_advance(lexer);
    }

    // Check for scientific notation (e.g., e-22 or E+12)
    if (peek(lexer) == 'e' || peek(lexer) == 'E') {
      char next = peek_next(lexer);
      char next2 = (next != '\0') ? lexer->current[2] : '\0';
      if (is_digit(next) || ((next == '+' || next == '-') && is_digit(next2))) {
        lexer_advance(lexer); // consume 'e'
        if (peek(lexer) == '+' || peek(lexer) == '-') {
          lexer_advance(lexer); // consume sign
        }
        while (is_digit(peek(lexer))) {
          lexer_advance(lexer);
        }
        return make_token(lexer, SLP_TOKEN_DOUBLE);
      }
    }

    // Check if we parsed a double (contains a dot '.')
    bool has_dot = false;
    for (const char *curr = lexer->start; curr < lexer->current; curr++) {
      if (*curr == '.') {
        has_dot = true;
        break;
      }
    }
    if (has_dot) {
      return make_token(lexer, SLP_TOKEN_DOUBLE);
    }
  }

  if (peek(lexer) == 'L' || peek(lexer) == 'l') {
    lexer_advance(lexer);
    return make_token(lexer, SLP_TOKEN_LONG);
  }

  return make_token(lexer, SLP_TOKEN_NUMBER);
}

static SlpToken scan_string(SlpLexer *lexer, char quote,
                               SlpTokenType string_type) {
  while (peek(lexer) != quote && !is_at_end(lexer)) {
    if (peek(lexer) == '\n')
      lexer->line++;
    if (peek(lexer) == '\\') {
      lexer_advance(lexer); // skip the escape
    }
    lexer_advance(lexer);
  }

  if (is_at_end(lexer))
    return error_token(lexer, "Unterminated string.");

  // The closing quote.
  lexer_advance(lexer);
  return make_token(lexer, string_type);
}

void slp_lexer_init(SlpLexer *lexer, const char *source,
                       SlpAllocator *allocator) {
  lexer->start = source;
  lexer->current = source;
  lexer->line = 1;
  lexer->allocator = allocator;
}

SlpToken slp_lexer_scan_token(SlpLexer *lexer) {
  skip_whitespace(lexer);
  lexer->start = lexer->current;

  if (is_at_end(lexer))
    return make_token(lexer, SLP_TOKEN_EOF);

  char c = lexer_advance(lexer);

  if (is_alpha(c))
    return scan_identifier(lexer);
  if (is_digit(c))
    return scan_number(lexer);

  switch (c) {
  case '(':
    return make_token(lexer, SLP_TOKEN_LEFT_PAREN);
  case ')':
    return make_token(lexer, SLP_TOKEN_RIGHT_PAREN);
  case '{':
    return make_token(lexer, SLP_TOKEN_LEFT_BRACE);
  case '}':
    return make_token(lexer, SLP_TOKEN_RIGHT_BRACE);
  case '[':
    return make_token(lexer, SLP_TOKEN_LEFT_BRACKET);
  case ']':
    return make_token(lexer, SLP_TOKEN_RIGHT_BRACKET);
  case ';':
    return make_token(lexer, SLP_TOKEN_SEMICOLON);
  case ',':
    return make_token(lexer, SLP_TOKEN_COMMA);
  case ':':
    return make_token(lexer, SLP_TOKEN_COLON);
  case '@':
    if (is_alpha(peek(lexer)) || is_digit(peek(lexer)) || peek(lexer) == '_') {
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_')
        lexer_advance(lexer);
    }
    return make_token(lexer, SLP_TOKEN_AT);
  case '\\':
    if (peek(lexer) == '&') {
      lexer_advance(lexer); // consume &
      if (is_alpha(peek(lexer)) || peek(lexer) == '_') {
        while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
               peek(lexer) == '_')
          lexer_advance(lexer);
        return make_token(lexer, SLP_TOKEN_ADDRESS);
      }
    }
    if (peek(lexer) == '$' || peek(lexer) == '@' || peek(lexer) == '%') {
      lexer_advance(lexer); // consume symbol
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_')
        lexer_advance(lexer);
      return make_token(lexer, SLP_TOKEN_ARG_PASSED_BY_NAME);
    }
    return make_token(lexer, SLP_TOKEN_BACKSLASH);
  case '%':
    if (is_alpha(peek(lexer)) || is_digit(peek(lexer)) || peek(lexer) == '_') {
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_')
        lexer_advance(lexer);
    }
    return make_token(lexer, SLP_TOKEN_PERCENT);

  case '.':
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLP_TOKEN_CATEQUAL);
    return make_token(lexer, SLP_TOKEN_DOT);
  case '-':
    if (lexer_match(lexer, '-'))
      return make_token(lexer, SLP_TOKEN_DEC);
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLP_TOKEN_MINUSEQUAL);
    if (is_alpha(peek(lexer))) {
      // It's a unary predicate like -isarray or -f
      // Consume the identifier after the dash
      lexer_advance(lexer);
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_')
        lexer_advance(lexer);
      return make_token(lexer, SLP_TOKEN_UNARY_PREDICATE_BRIDGE);
    }
    return make_token(lexer, SLP_TOKEN_MINUS);
  case '+':
    if (lexer_match(lexer, '+'))
      return make_token(lexer, SLP_TOKEN_INC);
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLP_TOKEN_PLUSEQUAL);
    return make_token(lexer, SLP_TOKEN_PLUS);
  case '/':
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLP_TOKEN_DIVEQUAL);
    return make_token(lexer, SLP_TOKEN_SLASH);
  case '*':
    if (lexer_match(lexer, '*')) {
      if (lexer_match(lexer, '='))
        return make_token(lexer, SLP_TOKEN_EXPEQUAL);
      return make_token(lexer, SLP_TOKEN_EXP);
    }
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLP_TOKEN_TIMESEQUAL);
    return make_token(lexer, SLP_TOKEN_STAR);
  case '^':
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLP_TOKEN_XOREQUAL);
    if (is_alpha(peek(lexer)) || peek(lexer) == '_' || peek(lexer) == '$') {
      // Consume the class name part
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_' || peek(lexer) == '$' || peek(lexer) == '.') {
        // If the next char is '.' followed by a quote, stop - that's member
        // access
        if (peek(lexer) == '.' &&
            (peek_next(lexer) == '"' || peek_next(lexer) == '\''))
          break;
        lexer_advance(lexer);
      }
      return make_token(lexer, SLP_TOKEN_CLASS_LITERAL);
    }
    return make_token(lexer, SLP_TOKEN_CARET);
  case '|':
    if (lexer_match(lexer, '|'))
      return make_token(lexer, SLP_TOKEN_LOR);
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLP_TOKEN_OREQUAL);
    return make_token(lexer, SLP_TOKEN_PIPE);
  case '&':
    if (lexer_match(lexer, '&'))
      return make_token(lexer, SLP_TOKEN_LAND);
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLP_TOKEN_ANDEQUAL);
    if (is_alpha(peek(lexer)) || peek(lexer) == '_') {
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_')
        lexer_advance(lexer);
      return make_token(lexer, SLP_TOKEN_ADDRESS);
    }
    return make_token(lexer, SLP_TOKEN_AMPERSAND);
  case '!':
    if (lexer_match(lexer, '=')) {
      if (lexer_match(lexer, '~'))
        return make_token(lexer, SLP_TOKEN_NEQI);
      return make_token(lexer, SLP_TOKEN_NE);
    }
    return make_token(lexer, SLP_TOKEN_BANG);
  case '=':
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLP_TOKEN_EQ);
    if (lexer_match(lexer, '~'))
      return make_token(lexer, SLP_TOKEN_EQI);
    if (lexer_match(lexer, '>'))
      return make_token(lexer, SLP_TOKEN_ARROW);
    return make_token(lexer, SLP_TOKEN_EQUAL);
  case '<':
    if (lexer_match(lexer, '=')) {
      if (lexer_match(lexer, '>'))
        return make_token(lexer, SLP_TOKEN_SPACESHIP);
      return make_token(lexer, SLP_TOKEN_LE);
    }
    if (lexer_match(lexer, '<')) {
      if (lexer_match(lexer, '='))
        return make_token(lexer, SLP_TOKEN_LSHIFTEQUAL);
      return make_token(lexer, SLP_TOKEN_LSHIFT);
    }
    return make_token(lexer, SLP_TOKEN_LESS);
  case '>':
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLP_TOKEN_GE);
    if (lexer_match(lexer, '>')) {
      if (lexer_match(lexer, '='))
        return make_token(lexer, SLP_TOKEN_RSHIFTEQUAL);
      return make_token(lexer, SLP_TOKEN_RSHIFT);
    }
    return make_token(lexer, SLP_TOKEN_GREATER);

  case '"':
    return scan_string(lexer, '"', SLP_TOKEN_STRING);
  case '\'':
    return scan_string(lexer, '\'', SLP_TOKEN_LITERAL);
  case '`':
    return scan_string(lexer, '`', SLP_TOKEN_BACKTICK_EXPR);

  case '$':
    if (lexer_match(lexer, 'n') && lexer_match(lexer, 'u') &&
        lexer_match(lexer, 'l') && lexer_match(lexer, 'l')) {
      return make_token(lexer, SLP_TOKEN_NULL);
    }
    // Check for special scalar $+
    if (peek(lexer) == '+') {
      lexer_advance(lexer);
      return make_token(lexer, SLP_TOKEN_SCALAR);
    }
    // Otherwise parse normal SCALAR
    while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) || peek(lexer) == '_')
      lexer_advance(lexer);
    return make_token(lexer, SLP_TOKEN_SCALAR);
  }

  return error_token(lexer, "Unexpected character.");
}

const char *slp_lexer_copy_lexeme(SlpLexer *lexer, SlpToken *token) {
  char *lexeme = (char *)lexer->allocator->reallocate(
      NULL, token->length + 1, lexer->allocator->user_data);
  if (lexeme == NULL)
    return NULL;
  slp_utils_memcpy(lexeme, token->start, token->length);
  lexeme[token->length] = '\0';
  return lexeme;
}
