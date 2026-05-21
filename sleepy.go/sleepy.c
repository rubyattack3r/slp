/*
 * Sleepy Amalgamated Source
 * Generated automatically.
 */
#include "sleepy.h"

/* --- File: ../src/sleepy_utils.c --- */

void sleepy_string_buffer_init(SleepyStringBuffer *buffer,
                               SleepyAllocator *allocator) {
  buffer->data = NULL;
  buffer->length = 0;
  buffer->capacity = 0;
  buffer->allocator = allocator;
}

void sleepy_string_buffer_clear(SleepyStringBuffer *buffer) {
  buffer->length = 0;
}

void sleepy_string_buffer_free(SleepyStringBuffer *buffer) {
  SLEEPY_FREE_ARRAY(buffer->allocator, char, buffer->data, buffer->capacity);
  sleepy_string_buffer_init(buffer, buffer->allocator);
}

void sleepy_string_buffer_append_char(SleepyStringBuffer *buffer, char c) {
  // We need space for the char PLUS the null terminator
  if (buffer->capacity < buffer->length + 2) {
    size_t new_capacity = buffer->capacity == 0 ? 16 : buffer->capacity * 2;
    while (new_capacity < buffer->length + 2) {
      new_capacity *= 2;
    }
    buffer->data =
        (char *)SLEEPY_REALLOC(buffer->allocator, buffer->data, new_capacity);
    buffer->capacity = new_capacity;
  }

  buffer->data[buffer->length++] = c;
  buffer->data[buffer->length] = '\0';
}

void sleepy_string_buffer_append_string(SleepyStringBuffer *buffer,
                                        const char *str, size_t length) {
  if (length == 0)
    return;
  // We need space for the string PLUS the null terminator
  if (buffer->capacity < buffer->length + length + 1) {
    size_t new_capacity = buffer->capacity == 0 ? 16 : buffer->capacity * 2;
    while (new_capacity < buffer->length + length + 1) {
      new_capacity *= 2;
    }
    buffer->data =
        (char *)SLEEPY_REALLOC(buffer->allocator, buffer->data, new_capacity);
    buffer->capacity = new_capacity;
  }

  for (size_t i = 0; i < length; i++) {
    buffer->data[buffer->length + i] = str[i];
  }
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
}

void *sleepy_utils_memcpy(void *dest, const void *src, size_t n) {
  char *d = (char *)dest;
  const char *s = (const char *)src;
  while (n--) {
    *d++ = *s++;
  }
  return dest;
}

size_t sleepy_utils_strlen(const char *s) {
  size_t len = 0;
  while (s[len] != '\0') {
    len++;
  }
  return len;
}

char *sleepy_utils_strcpy(char *dest, const char *src) {
  char *d = dest;
  while ((*d++ = *src++) != '\0') {
    // nothing
  }
  return dest;
}

/* --- File: ../src/sleepy_lexer.c --- */
#include <stdbool.h>
#include <stdio.h>

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static bool is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_at_end(SleepyLexer *lexer) { return *lexer->current == '\0'; }

static char lexer_advance(SleepyLexer *lexer) {
  lexer->current++;
  return lexer->current[-1];
}

static char peek(SleepyLexer *lexer) { return *lexer->current; }

static char peek_next(SleepyLexer *lexer) {
  if (is_at_end(lexer))
    return '\0';
  return lexer->current[1];
}

static bool lexer_match(SleepyLexer *lexer, char expected) {
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
        if (lexer->current - lexer->start == 4 && lexer->start[2] == 's' &&
            lexer->start[3] == 'e')
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
        if (lexer->current - lexer->start == 4 && lexer->start[2] == 'l' &&
            lexer->start[3] == 't')
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
        return SLEEPY_TOKEN_LOWER_X;
      }
    }
    break;
  case 'y':
    return CHECK_KEYWORD(1, 4, "ield", SLEEPY_TOKEN_YIELD);
  }

  return SLEEPY_TOKEN_ID;
}

static SleepyToken scan_identifier(SleepyLexer *lexer) {
  while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) || peek(lexer) == '-' ||
         peek(lexer) == '+') {
    lexer_advance(lexer);
  }
  return make_token(lexer, identifier_type(lexer));
}

static SleepyToken scan_number(SleepyLexer *lexer) {
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
      return make_token(lexer, SLEEPY_TOKEN_DOUBLE);
    }
  }

  if (peek(lexer) == 'L' || peek(lexer) == 'l') {
    lexer_advance(lexer);
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

  char c = lexer_advance(lexer);

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
        lexer_advance(lexer);
    }
    return make_token(lexer, SLEEPY_TOKEN_AT);
  case '\\':
    if (peek(lexer) == '&') {
      lexer_advance(lexer); // consume &
      if (is_alpha(peek(lexer)) || peek(lexer) == '_') {
        while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
               peek(lexer) == '_')
          lexer_advance(lexer);
        return make_token(lexer, SLEEPY_TOKEN_ADDRESS);
      }
    }
    if (peek(lexer) == '$' || peek(lexer) == '@' || peek(lexer) == '%') {
      lexer_advance(lexer); // consume symbol
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_')
        lexer_advance(lexer);
      return make_token(lexer, SLEEPY_TOKEN_ARG_PASSED_BY_NAME);
    }
    return make_token(lexer, SLEEPY_TOKEN_BACKSLASH);
  case '%':
    if (is_alpha(peek(lexer)) || is_digit(peek(lexer)) || peek(lexer) == '_') {
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_')
        lexer_advance(lexer);
    }
    return make_token(lexer, SLEEPY_TOKEN_PERCENT);

  case '.':
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_CATEQUAL);
    return make_token(lexer, SLEEPY_TOKEN_DOT);
  case '-':
    if (lexer_match(lexer, '-'))
      return make_token(lexer, SLEEPY_TOKEN_DEC);
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_MINUSEQUAL);
    if (is_alpha(peek(lexer))) {
      // It's a unary predicate like -isarray or -f
      // Consume the identifier after the dash
      lexer_advance(lexer);
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_')
        lexer_advance(lexer);
      return make_token(lexer, SLEEPY_TOKEN_UNARY_PREDICATE_BRIDGE);
    }
    return make_token(lexer, SLEEPY_TOKEN_MINUS);
  case '+':
    if (lexer_match(lexer, '+'))
      return make_token(lexer, SLEEPY_TOKEN_INC);
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_PLUSEQUAL);
    return make_token(lexer, SLEEPY_TOKEN_PLUS);
  case '/':
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_DIVEQUAL);
    return make_token(lexer, SLEEPY_TOKEN_SLASH);
  case '*':
    if (lexer_match(lexer, '*')) {
      if (lexer_match(lexer, '='))
        return make_token(lexer, SLEEPY_TOKEN_EXPEQUAL);
      return make_token(lexer, SLEEPY_TOKEN_EXP);
    }
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_TIMESEQUAL);
    return make_token(lexer, SLEEPY_TOKEN_STAR);
  case '^':
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_XOREQUAL);
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
      return make_token(lexer, SLEEPY_TOKEN_CLASS_LITERAL);
    }
    return make_token(lexer, SLEEPY_TOKEN_CARET);
  case '|':
    if (lexer_match(lexer, '|'))
      return make_token(lexer, SLEEPY_TOKEN_LOR);
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_OREQUAL);
    return make_token(lexer, SLEEPY_TOKEN_PIPE);
  case '&':
    if (lexer_match(lexer, '&'))
      return make_token(lexer, SLEEPY_TOKEN_LAND);
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_ANDEQUAL);
    if (is_alpha(peek(lexer)) || peek(lexer) == '_') {
      while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) ||
             peek(lexer) == '_')
        lexer_advance(lexer);
      return make_token(lexer, SLEEPY_TOKEN_ADDRESS);
    }
    return make_token(lexer, SLEEPY_TOKEN_AMPERSAND);
  case '!':
    if (lexer_match(lexer, '=')) {
      if (lexer_match(lexer, '~'))
        return make_token(lexer, SLEEPY_TOKEN_NEQI);
      return make_token(lexer, SLEEPY_TOKEN_NE);
    }
    return make_token(lexer, SLEEPY_TOKEN_BANG);
  case '=':
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_EQ);
    if (lexer_match(lexer, '~'))
      return make_token(lexer, SLEEPY_TOKEN_EQI);
    if (lexer_match(lexer, '>'))
      return make_token(lexer, SLEEPY_TOKEN_ARROW);
    return make_token(lexer, SLEEPY_TOKEN_EQUAL);
  case '<':
    if (lexer_match(lexer, '=')) {
      if (lexer_match(lexer, '>'))
        return make_token(lexer, SLEEPY_TOKEN_SPACESHIP);
      return make_token(lexer, SLEEPY_TOKEN_LE);
    }
    if (lexer_match(lexer, '<')) {
      if (lexer_match(lexer, '='))
        return make_token(lexer, SLEEPY_TOKEN_LSHIFTEQUAL);
      return make_token(lexer, SLEEPY_TOKEN_LSHIFT);
    }
    return make_token(lexer, SLEEPY_TOKEN_LESS);
  case '>':
    if (lexer_match(lexer, '='))
      return make_token(lexer, SLEEPY_TOKEN_GE);
    if (lexer_match(lexer, '>')) {
      if (lexer_match(lexer, '='))
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
    if (lexer_match(lexer, 'n') && lexer_match(lexer, 'u') &&
        lexer_match(lexer, 'l') && lexer_match(lexer, 'l')) {
      return make_token(lexer, SLEEPY_TOKEN_NULL);
    }
    // Check for special scalar $+
    if (peek(lexer) == '+') {
      lexer_advance(lexer);
      return make_token(lexer, SLEEPY_TOKEN_SCALAR);
    }
    // Otherwise parse normal SCALAR
    while (is_alpha(peek(lexer)) || is_digit(peek(lexer)) || peek(lexer) == '_')
      lexer_advance(lexer);
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

/* --- File: ../src/sleepy_parser.c --- */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static void advance(SleepyParser *parser);
static void error_at(SleepyParser *parser, SleepyToken *token,
                     const char *message);
static void error(SleepyParser *parser, const char *message);
static void error_at_current(SleepyParser *parser, const char *message);
static void consume(SleepyParser *parser, SleepyTokenType type,
                    const char *message);
static bool match(SleepyParser *parser, SleepyTokenType type);
static bool check(SleepyParser *parser, SleepyTokenType type);
static SleepyASTNode *allocate_node(SleepyParser *parser,
                                    SleepyASTNodeType type);

static SleepyASTNode *expression(SleepyParser *parser);
static SleepyASTNode *statement(SleepyParser *parser);
static SleepyASTNode *declaration(SleepyParser *parser);
static SleepyASTNode *block(SleepyParser *parser);
static SleepyASTNode **parse_args(SleepyParser *parser, size_t *count,
                                  SleepyTokenType endToken);

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT, // =, +=, -=, etc.
  PREC_LOR,        // ||
  PREC_LAND,       // &&
  PREC_BIT_OR,     // |
  PREC_BIT_XOR,    // ^
  PREC_BIT_AND,    // &
  PREC_EQUALITY,   // ==, !=, <=>, eq, ne, etc.
  PREC_COMPARISON, // <, >, <=, >=, gt, lt, etc.
  PREC_SHIFT,      // <<, >>
  PREC_CONCAT,     // .
  PREC_TERM,       // +, -
  PREC_FACTOR,     // *, /, %, x
  PREC_UNARY,      // ! - +
  PREC_INCDEC,     // ++ --
  PREC_EXP,        // **
  PREC_CALL,       // . () []
  PREC_PRIMARY
} ParsePrecedence;

typedef SleepyASTNode *(*ParseFn)(SleepyParser *parser, SleepyASTNode *left,
                                  bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  ParsePrecedence precedence;
} ParseRule;

static ParseRule *get_rule(SleepyTokenType type);
static SleepyASTNode *parse_precedence(SleepyParser *parser,
                                       ParsePrecedence precedence);

// -----------------------------------------------------------------------------
// Parser Core
// -----------------------------------------------------------------------------

void sleepy_parser_init(SleepyParser *parser, const char *source,
                        SleepyAllocator *allocator) {
  sleepy_lexer_init(&parser->lexer, source, allocator);
  parser->allocator = allocator;
  parser->had_error = false;
  parser->panic_mode = false;
  parser->previous.type = SLEEPY_TOKEN_ERROR;
  parser->previous.start = NULL;
  parser->previous.length = 0;
  parser->previous.line = 0;
  parser->current = parser->previous;
  parser->error_line = 0;
  parser->error_message = NULL;
}

static void error_at(SleepyParser *parser, SleepyToken *token,
                     const char *message) {
  if (parser->panic_mode)
    return;
  parser->panic_mode = true;
  parser->had_error = true;
  parser->error_line = token->line;
  parser->error_message = message;
}

static void error(SleepyParser *parser, const char *message) {
  error_at(parser, &parser->previous, message);
}

static void error_at_current(SleepyParser *parser, const char *message) {
  error_at(parser, &parser->current, message);
}

static void advance(SleepyParser *parser) {
  parser->previous = parser->current;
  for (;;) {
    parser->current = sleepy_lexer_scan_token(&parser->lexer);
    if (parser->current.type != SLEEPY_TOKEN_ERROR)
      break;
    error_at_current(parser, parser->current.start);
  }
}

static void consume(SleepyParser *parser, SleepyTokenType type,
                    const char *message) {
  if (parser->current.type == type) {
    advance(parser);
    return;
  }
  error_at_current(parser, message);
}

static bool match(SleepyParser *parser, SleepyTokenType type) {
  if (parser->current.type != type)
    return false;
  advance(parser);
  return true;
}

static bool check(SleepyParser *parser, SleepyTokenType type) {
  return parser->current.type == type;
}

static SleepyASTNode *allocate_node(SleepyParser *parser,
                                    SleepyASTNodeType type) {
  SleepyASTNode *node = SLEEPY_ALLOCATE(parser->allocator, SleepyASTNode);
  if (node) {
    memset(node, 0, sizeof(SleepyASTNode));
    node->type = type;
    node->line = parser->previous.line;
  }
  return node;
}

// -----------------------------------------------------------------------------
// Expression implementation
// -----------------------------------------------------------------------------

static SleepyASTNode *binary(SleepyParser *parser, SleepyASTNode *left,
                             bool canAssign) {
  (void)canAssign;
  SleepyToken operatorToken = parser->previous;
  ParseRule *rule = get_rule(operatorToken.type);
  SleepyASTNode *right =
      parse_precedence(parser, (ParsePrecedence)(rule->precedence + 1));
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_BINOP);
  node->as.binop.left = left;
  node->as.binop.op = operatorToken;
  node->as.binop.right = right;
  node->as.binop.negate = false;
  return node;
}

static SleepyASTNode *negated_binary(SleepyParser *parser, SleepyASTNode *left,
                                     bool canAssign) {
  (void)canAssign;
  advance(parser); // consume the predicate
  SleepyToken predicateToken = parser->previous;
  SleepyASTNode *right = parse_precedence(parser, PREC_EQUALITY);
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_BINOP);
  node->as.binop.left = left;
  node->as.binop.op = predicateToken;
  node->as.binop.right = right;
  node->as.binop.negate = true;
  return node;
}

static SleepyASTNode *unary(SleepyParser *parser, SleepyASTNode *left,
                            bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyToken operatorToken = parser->previous;
  SleepyASTNode *operand = parse_precedence(parser, PREC_UNARY);
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_UNARYOP);
  node->as.unaryop.op = operatorToken;
  node->as.unaryop.operand = operand;
  return node;
}

static SleepyASTNode *assignment(SleepyParser *parser, SleepyASTNode *left,
                                 bool canAssign) {
  (void)canAssign;
  SleepyToken operatorToken = parser->previous;
  SleepyASTNode *right = parse_precedence(parser, PREC_ASSIGNMENT);
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_ASSIGNMENT);
  node->as.assign.left = left;
  node->as.assign.op = operatorToken;
  node->as.assign.right = right;
  return node;
}

static SleepyASTNode *identifier(SleepyParser *parser, SleepyASTNode *left,
                                 bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_IDENTIFIER);
  node->as.string_val =
      sleepy_lexer_copy_lexeme(&parser->lexer, &parser->previous);
  return node;
}

static const char *copy_unquoted_lexeme(SleepyParser *parser, SleepyToken *token) {
    if (token->length < 2) return sleepy_lexer_copy_lexeme(&parser->lexer, token);
    size_t inner_len = token->length - 2;
    char *str = (char *)parser->allocator->reallocate(NULL, inner_len + 1, NULL);
    if (!str) return NULL;
    const char *src = token->start + 1;
    size_t wi = 0;
    for (size_t ri = 0; ri < inner_len; ri++) {
        if (src[ri] == '\\' && ri + 1 < inner_len) {
            char next = src[ri + 1];
            switch (next) {
                case 'n': str[wi++] = '\n'; ri++; break;
                case 't': str[wi++] = '\t'; ri++; break;
                case 'r': str[wi++] = '\r'; ri++; break;
                case '\\': str[wi++] = '\\'; ri++; break;
                case '"': str[wi++] = '"'; ri++; break;
                case '\'': str[wi++] = '\''; ri++; break;
                default: str[wi++] = src[ri]; break;
            }
        } else {
            str[wi++] = src[ri];
        }
    }
    str[wi] = '\0';
    return str;
}

static SleepyASTNode *string_val(SleepyParser *parser, SleepyASTNode *left,
                                 bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_STRING);
  node->as.string_val = copy_unquoted_lexeme(parser, &parser->previous);
  return node;
}

static SleepyASTNode *number(SleepyParser *parser, SleepyASTNode *left,
                             bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyToken token = parser->previous;
  const char *lexeme = sleepy_lexer_copy_lexeme(&parser->lexer, &token);

  if (token.type == SLEEPY_TOKEN_LONG) {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_LONG);
    node->as.long_val = strtoll(lexeme, NULL, 10);
    return node;
  } else {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_NUMBER);
    node->as.double_val = strtod(lexeme, NULL);
    return node;
  }
}

static SleepyASTNode *literal(SleepyParser *parser, SleepyASTNode *left,
                              bool canAssign) {
  (void)canAssign;
  (void)left;
  switch (parser->previous.type) {
  case SLEEPY_TOKEN_FALSE: {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_BOOLEAN);
    node->as.boolean = false;
    return node;
  }
  case SLEEPY_TOKEN_TRUE: {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_BOOLEAN);
    node->as.boolean = true;
    return node;
  }
  case SLEEPY_TOKEN_NULL:
    return allocate_node(parser, SLEEPY_AST_NULL);
  default:
    return NULL;
  }
}

static SleepyASTNode *scalar(SleepyParser *parser, SleepyASTNode *left,
                             bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_SCALAR);
  SleepyToken idToken = parser->previous;
  if (idToken.length > 0) {
    idToken.start++;
    idToken.length--;
  }
  node->as.string_val = sleepy_lexer_copy_lexeme(&parser->lexer, &idToken);
  return node;
}

static SleepyASTNode *array(SleepyParser *parser, SleepyASTNode *left,
                            bool canAssign) {
  (void)canAssign;
  (void)left;
  if (parser->previous.length == 1) { // @(...)
    consume(parser, SLEEPY_TOKEN_LEFT_PAREN, "Expect '(' after '@'.");
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_CALL);
    SleepyASTNode *target = allocate_node(parser, SLEEPY_AST_ARRAY);
    char *at = (char *)parser->allocator->reallocate(NULL, 2, parser->allocator->user_data);
    if (at) sleepy_utils_strcpy(at, "@");
    target->as.string_val = at;
    node->as.call.target = target;
    extern SleepyASTNode **parse_args(SleepyParser * parser, size_t *count,
                                      SleepyTokenType endToken);
    node->as.call.args =
        parse_args(parser, &node->as.call.arg_count, SLEEPY_TOKEN_RIGHT_PAREN);
    consume(parser, SLEEPY_TOKEN_RIGHT_PAREN,
            "Expect ')' after array arguments.");
    return node;
  }
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_ARRAY);
  SleepyToken idToken = parser->previous;
  idToken.start++;
  idToken.length--;
  node->as.string_val = sleepy_lexer_copy_lexeme(&parser->lexer, &idToken);
  return node;
}

static SleepyASTNode *hash(SleepyParser *parser, SleepyASTNode *left,
                           bool canAssign) {
  (void)canAssign;
  (void)left;
  if (parser->previous.length == 1) { // %(...)
    consume(parser, SLEEPY_TOKEN_LEFT_PAREN, "Expect '(' after '%'.");
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_CALL);
    SleepyASTNode *target = allocate_node(parser, SLEEPY_AST_HASHTABLE);
    char *pct = (char *)parser->allocator->reallocate(NULL, 2, parser->allocator->user_data);
    if (pct) sleepy_utils_strcpy(pct, "%");
    target->as.string_val = pct;
    node->as.call.target = target;
    extern SleepyASTNode **parse_args(SleepyParser * parser, size_t *count,
                                      SleepyTokenType endToken);
    node->as.call.args =
        parse_args(parser, &node->as.call.arg_count, SLEEPY_TOKEN_RIGHT_PAREN);
    consume(parser, SLEEPY_TOKEN_RIGHT_PAREN,
            "Expect ')' after hash arguments.");
    return node;
  }
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_HASHTABLE);
  SleepyToken idToken = parser->previous;
  idToken.start++;
  idToken.length--;
  node->as.string_val = sleepy_lexer_copy_lexeme(&parser->lexer, &idToken);
  return node;
}

static SleepyASTNode *grouping(SleepyParser *parser, SleepyASTNode *left,
                               bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyASTNode *expr = expression(parser);
  if (check(parser, SLEEPY_TOKEN_COMMA)) {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_LVALUE_TUPLE);
    node->as.block.capacity = 4;
    node->as.block.count = 0;
    node->as.block.statements = (SleepyASTNode **)parser->allocator->reallocate(
        NULL, sizeof(SleepyASTNode *) * node->as.block.capacity,
        parser->allocator->user_data);
    node->as.block.statements[node->as.block.count++] = expr;
    while (match(parser, SLEEPY_TOKEN_COMMA)) {
      if (node->as.block.count >= node->as.block.capacity) {
        node->as.block.capacity *= 2;
        node->as.block.statements =
            (SleepyASTNode **)parser->allocator->reallocate(
                node->as.block.statements,
                sizeof(SleepyASTNode *) * node->as.block.capacity,
                parser->allocator->user_data);
      }
      node->as.block.statements[node->as.block.count++] = expression(parser);
    }
    consume(parser, SLEEPY_TOKEN_RIGHT_PAREN, "Expect ')' after tuple.");
    return node;
  }
  consume(parser, SLEEPY_TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
  return expr;
}

static SleepyASTNode *postfix(SleepyParser *parser, SleepyASTNode *left,
                              bool canAssign) {
  (void)canAssign;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_UNARYOP);
  node->as.unaryop.op = parser->previous;
  node->as.unaryop.operand = left;
  return node;
}

static SleepyASTNode *logical(SleepyParser *parser, SleepyASTNode *left,
                              bool canAssign) {
  (void)canAssign;
  SleepyToken operatorToken = parser->previous;
  ParseRule *rule = get_rule(operatorToken.type);
  SleepyASTNode *right =
      parse_precedence(parser, (ParsePrecedence)(rule->precedence));
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_BINOP);
  node->as.binop.left = left;
  node->as.binop.op = operatorToken;
  node->as.binop.right = right;
  node->as.binop.negate = false;
  return node;
}

static SleepyASTNode *index_acc(SleepyParser *parser, SleepyASTNode *left,
                                bool canAssign) {
  (void)canAssign;
  SleepyASTNode *element = expression(parser);
  consume(parser, SLEEPY_TOKEN_RIGHT_BRACKET, "Expect ']' after index.");
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_INDEX);
  node->as.index.container = left;
  node->as.index.element = element;
  return node;
}

static SleepyASTNode *obj_expr(SleepyParser *parser, SleepyASTNode *left,
                               bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_OBJ_EXPR);
  node->as.obj_expr.target = expression(parser);
  extern SleepyASTNode **parse_args(SleepyParser * parser, size_t *count,
                                    SleepyTokenType endToken);
  if (match(parser, SLEEPY_TOKEN_COLON)) {
    node->as.obj_expr.message = NULL;
    node->as.obj_expr.args = parse_args(parser, &node->as.obj_expr.arg_count,
                                        SLEEPY_TOKEN_RIGHT_BRACKET);
  } else if (!check(parser, SLEEPY_TOKEN_RIGHT_BRACKET)) {
    // Parse the message - keywords can be used as message names in object
    // expressions
    SleepyASTNode *message = NULL;

    // Check if current token is a command keyword (return, yield, etc.)
    // These should be treated as identifiers in object expressions
    bool is_command_keyword = false;
    switch (parser->current.type) {
    case SLEEPY_TOKEN_RETURN:
    case SLEEPY_TOKEN_THROW:
    case SLEEPY_TOKEN_YIELD:
    case SLEEPY_TOKEN_ASSERT:
    case SLEEPY_TOKEN_CALLCC:
    case SLEEPY_TOKEN_HALT:
    case SLEEPY_TOKEN_DONE:
    case SLEEPY_TOKEN_LOCAL:
    case SLEEPY_TOKEN_THIS:
    case SLEEPY_TOKEN_BREAK:
    case SLEEPY_TOKEN_CONTINUE:
      is_command_keyword = true;
      break;
    default:
      break;
    }

    if (is_command_keyword) {
      // Treat this keyword as an identifier/message name
      advance(parser);
      message = allocate_node(parser, SLEEPY_AST_IDENTIFIER);
      message->as.string_val =
          sleepy_lexer_copy_lexeme(&parser->lexer, &parser->previous);
    } else {
      message = expression(parser);
    }
    node->as.obj_expr.message = message;

    if (match(parser, SLEEPY_TOKEN_COLON)) {
      node->as.obj_expr.args = parse_args(parser, &node->as.obj_expr.arg_count,
                                          SLEEPY_TOKEN_RIGHT_BRACKET);
    } else {
      node->as.obj_expr.args = NULL;
      node->as.obj_expr.arg_count = 0;
    }
  } else {
    node->as.obj_expr.message = NULL;
    node->as.obj_expr.args = NULL;
    node->as.obj_expr.arg_count = 0;
  }
  consume(parser, SLEEPY_TOKEN_RIGHT_BRACKET,
          "Expect ']' after object expression.");
  return node;
}

static SleepyASTNode *call(SleepyParser *parser, SleepyASTNode *left,
                           bool canAssign) {
  (void)canAssign;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_CALL);
  node->as.call.target = left;
  extern SleepyASTNode **parse_args(SleepyParser * parser, size_t *count,
                                    SleepyTokenType endToken);
  node->as.call.args =
      parse_args(parser, &node->as.call.arg_count, SLEEPY_TOKEN_RIGHT_PAREN);
  consume(parser, SLEEPY_TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return node;
}

static SleepyASTNode *command(SleepyParser *parser, SleepyASTNode *left,
                              bool canAssign) {
  (void)left;
  (void)canAssign;
  SleepyToken op = parser->previous;
  SleepyASTNodeType type = SLEEPY_AST_NOP;
  if (op.type == SLEEPY_TOKEN_RETURN)
    type = SLEEPY_AST_RETURN;
  else if (op.type == SLEEPY_TOKEN_THROW)
    type = SLEEPY_AST_THROW;
  else if (op.type == SLEEPY_TOKEN_YIELD)
    type = SLEEPY_AST_YIELD;
  else if (op.type == SLEEPY_TOKEN_ASSERT)
    type = SLEEPY_AST_ASSERT;
  else if (op.type == SLEEPY_TOKEN_CALLCC)
    type = SLEEPY_AST_CALLCC;
  else if (op.type == SLEEPY_TOKEN_HALT)
    type = SLEEPY_AST_HALT;
  else if (op.type == SLEEPY_TOKEN_DONE)
    type = SLEEPY_AST_DONE;
  else if (op.type == SLEEPY_TOKEN_LOCAL)
    type = SLEEPY_AST_LOCAL;
  else if (op.type == SLEEPY_TOKEN_THIS)
    type = SLEEPY_AST_THIS;
  else if (op.type == SLEEPY_TOKEN_BREAK)
    type = SLEEPY_AST_BREAK;
  else if (op.type == SLEEPY_TOKEN_CONTINUE)
    type = SLEEPY_AST_CONTINUE;

  SleepyASTNode *node = allocate_node(parser, type);
  node->as.control.value = NULL;

  // For assert, we may have: assert expr : "message"
  if (op.type == SLEEPY_TOKEN_ASSERT) {
    if (!check(parser, SLEEPY_TOKEN_SEMICOLON) &&
        !check(parser, SLEEPY_TOKEN_EOF) &&
        !check(parser, SLEEPY_TOKEN_RIGHT_PAREN) &&
        !check(parser, SLEEPY_TOKEN_RIGHT_BRACE)) {
      node->as.control.value = expression(parser);
      // Check for optional : "message"
      if (match(parser, SLEEPY_TOKEN_COLON)) {
        // The message is parsed as part of the expression - for now just
        // consume it In a full implementation, we'd store this separately
        if (!check(parser, SLEEPY_TOKEN_SEMICOLON) &&
            !check(parser, SLEEPY_TOKEN_EOF)) {
          expression(parser); // Parse but discard the message for now
        }
      }
    }
  } else {
    // For other commands
    if (!check(parser, SLEEPY_TOKEN_SEMICOLON) &&
        !check(parser, SLEEPY_TOKEN_EOF) &&
        !check(parser, SLEEPY_TOKEN_RIGHT_PAREN) &&
        !check(parser, SLEEPY_TOKEN_RIGHT_BRACE)) {
      node->as.control.value = expression(parser);
    }
  }
  return node;
}

static SleepyASTNode *bridge(SleepyParser *parser, SleepyASTNode *left,
                             bool canAssign) {
  (void)left;
  (void)canAssign;
  SleepyToken keyword = parser->previous;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_ENV_BRIDGE);
  node->as.env_bridge.keyword =
      sleepy_lexer_copy_lexeme(&parser->lexer, &keyword);
  node->as.env_bridge.identifier = NULL;
  node->as.env_bridge.string = NULL;
  node->as.env_bridge.body = NULL;

  if (match(parser, SLEEPY_TOKEN_ID)) {
    node->as.env_bridge.identifier =
        sleepy_lexer_copy_lexeme(&parser->lexer, &parser->previous);
  }
  if (match(parser, SLEEPY_TOKEN_STRING) ||
      match(parser, SLEEPY_TOKEN_LITERAL)) {
    node->as.env_bridge.string =
        sleepy_lexer_copy_lexeme(&parser->lexer, &parser->previous);
  }
  if (check(parser, SLEEPY_TOKEN_LEFT_BRACE)) {
    node->as.env_bridge.body = block(parser);
  }
  return node;
}

static SleepyASTNode *backtick_expr(SleepyParser *parser, SleepyASTNode *left,
                                    bool canAssign) {
  (void)canAssign;
  (void)left;
  return allocate_node(parser, SLEEPY_AST_BACKTICK);
}

static SleepyASTNode *address_expr(SleepyParser *parser, SleepyASTNode *left,
                                   bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_ADDRESS);
  SleepyToken t = parser->previous;
  if (t.length > 0) {
    t.start++;
    t.length--;
  }
  node->as.string_val = sleepy_lexer_copy_lexeme(&parser->lexer, &t);
  return node;
}

static SleepyASTNode *class_expr(SleepyParser *parser, SleepyASTNode *left,
                                 bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_CLASS_LITERAL);
  SleepyToken t = parser->previous;
  if (t.length > 0) {
    t.start++;
    t.length--;
  }
  node->as.string_val = sleepy_lexer_copy_lexeme(&parser->lexer, &t);
  return node;
}

static SleepyASTNode *block_expr(SleepyParser *parser, SleepyASTNode *left,
                                 bool canAssign) {
  (void)left;
  (void)canAssign;
  // The '{' has already been consumed by parse_precedence
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_BLOCK);
  node->as.block.capacity = 8;
  node->as.block.count = 0;
  node->as.block.statements = (SleepyASTNode **)parser->allocator->reallocate(
      NULL, sizeof(SleepyASTNode *) * node->as.block.capacity,
      parser->allocator->user_data);
  while (!check(parser, SLEEPY_TOKEN_RIGHT_BRACE) &&
         !check(parser, SLEEPY_TOKEN_EOF) && !parser->had_error) {
    SleepyASTNode *stmt = declaration(parser);
    if (stmt) {
      if (node->as.block.count >= node->as.block.capacity) {
        node->as.block.capacity *= 2;
        node->as.block.statements =
            (SleepyASTNode **)parser->allocator->reallocate(
                node->as.block.statements,
                sizeof(SleepyASTNode *) * node->as.block.capacity,
                parser->allocator->user_data);
      }
      node->as.block.statements[node->as.block.count++] = stmt;
    }
  }
  consume(parser, SLEEPY_TOKEN_RIGHT_BRACE, "Expect '}' after block.");
  return node;
}

// -----------------------------------------------------------------------------
// Rules Table
// -----------------------------------------------------------------------------

ParseRule rules[] = {
    [SLEEPY_TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [SLEEPY_TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [SLEEPY_TOKEN_LEFT_BRACE] = {block_expr, NULL, PREC_NONE},
    [SLEEPY_TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [SLEEPY_TOKEN_LEFT_BRACKET] = {obj_expr, index_acc, PREC_CALL},
    [SLEEPY_TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [SLEEPY_TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [SLEEPY_TOKEN_DOT] = {NULL, binary, PREC_CONCAT},
    [SLEEPY_TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [SLEEPY_TOKEN_PLUS] = {unary, binary, PREC_TERM},
    [SLEEPY_TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [SLEEPY_TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [SLEEPY_TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [SLEEPY_TOKEN_BANG] = {unary, negated_binary, PREC_UNARY},
    [SLEEPY_TOKEN_NE] = {NULL, binary, PREC_EQUALITY},
    [SLEEPY_TOKEN_EQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLEEPY_TOKEN_EQ] = {NULL, binary, PREC_EQUALITY},
    [SLEEPY_TOKEN_EQI] = {NULL, binary, PREC_EQUALITY},
    [SLEEPY_TOKEN_NEQI] = {NULL, binary, PREC_EQUALITY},
    [SLEEPY_TOKEN_SPACESHIP] = {NULL, binary, PREC_EQUALITY},
    [SLEEPY_TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [SLEEPY_TOKEN_GE] = {NULL, binary, PREC_COMPARISON},
    [SLEEPY_TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [SLEEPY_TOKEN_LE] = {NULL, binary, PREC_COMPARISON},
    [SLEEPY_TOKEN_ID] = {identifier, NULL, PREC_NONE},
    [SLEEPY_TOKEN_STRING] = {string_val, NULL, PREC_NONE},
    [SLEEPY_TOKEN_LITERAL] = {string_val, NULL, PREC_NONE},
    [SLEEPY_TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [SLEEPY_TOKEN_DOUBLE] = {number, NULL, PREC_NONE},
    [SLEEPY_TOKEN_LONG] = {number, NULL, PREC_NONE},
    [SLEEPY_TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [SLEEPY_TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [SLEEPY_TOKEN_NULL] = {literal, NULL, PREC_NONE},
    [SLEEPY_TOKEN_LAND] = {NULL, logical, PREC_LAND},
    [SLEEPY_TOKEN_LOR] = {NULL, logical, PREC_LOR},
    [SLEEPY_TOKEN_SCALAR] = {scalar, NULL, PREC_NONE},
    [SLEEPY_TOKEN_AT] = {array, NULL, PREC_NONE},
    [SLEEPY_TOKEN_PERCENT] = {hash, binary, PREC_FACTOR},
    [SLEEPY_TOKEN_LOWER_X] = {NULL, binary, PREC_FACTOR},
    [SLEEPY_TOKEN_BACKTICK_EXPR] = {backtick_expr, NULL, PREC_NONE},
    [SLEEPY_TOKEN_BUILTIN_BINARY_PREDICATE_BRIDGE] = {NULL, binary,
                                                      PREC_EQUALITY},
    [SLEEPY_TOKEN_UNARY_PREDICATE_BRIDGE] = {unary, NULL, PREC_UNARY},
    [SLEEPY_TOKEN_AMPERSAND] = {NULL, binary, PREC_BIT_AND},
    [SLEEPY_TOKEN_PIPE] = {NULL, binary, PREC_BIT_OR},
    [SLEEPY_TOKEN_CARET] = {NULL, binary, PREC_BIT_XOR},
    [SLEEPY_TOKEN_LSHIFT] = {NULL, binary, PREC_SHIFT},
    [SLEEPY_TOKEN_RSHIFT] = {NULL, binary, PREC_SHIFT},
    [SLEEPY_TOKEN_EXP] = {NULL, binary, PREC_EXP},
    [SLEEPY_TOKEN_EOF] = {NULL, NULL, PREC_NONE},
    [SLEEPY_TOKEN_ADDRESS] = {address_expr, NULL, PREC_NONE},
    [SLEEPY_TOKEN_CLASS_LITERAL] = {class_expr, NULL, PREC_NONE},
    [SLEEPY_TOKEN_ARG_PASSED_BY_NAME] = {address_expr, NULL, PREC_NONE},
    [SLEEPY_TOKEN_COPY] = {identifier, NULL, PREC_NONE},
    [SLEEPY_TOKEN_ADDALL] = {identifier, NULL, PREC_NONE},
    [SLEEPY_TOKEN_PRINTLN] = {identifier, NULL, PREC_NONE},
    [SLEEPY_TOKEN_REMOVEALL] = {identifier, NULL, PREC_NONE},
    [SLEEPY_TOKEN_RETAINALL] = {identifier, NULL, PREC_NONE},
    [SLEEPY_TOKEN_ANDEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLEEPY_TOKEN_CATEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLEEPY_TOKEN_DIVEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLEEPY_TOKEN_LSHIFTEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLEEPY_TOKEN_MINUSEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLEEPY_TOKEN_OREQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLEEPY_TOKEN_PLUSEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLEEPY_TOKEN_RSHIFTEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLEEPY_TOKEN_TIMESEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLEEPY_TOKEN_XOREQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLEEPY_TOKEN_EXPEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLEEPY_TOKEN_ASSERT] = {command, NULL, PREC_NONE},
    [SLEEPY_TOKEN_BREAK] = {command, NULL, PREC_NONE},
    [SLEEPY_TOKEN_CONTINUE] = {command, NULL, PREC_NONE},
    [SLEEPY_TOKEN_DONE] = {command, NULL, PREC_NONE},
    [SLEEPY_TOKEN_HALT] = {command, NULL, PREC_NONE},
    [SLEEPY_TOKEN_RETURN] = {command, NULL, PREC_NONE},
    [SLEEPY_TOKEN_THROW] = {command, NULL, PREC_NONE},
    [SLEEPY_TOKEN_YIELD] = {command, NULL, PREC_NONE},
    [SLEEPY_TOKEN_CALLCC] = {command, NULL, PREC_NONE},
    [SLEEPY_TOKEN_SUB] = {bridge, NULL, PREC_NONE},
    [SLEEPY_TOKEN_INLINE] = {bridge, NULL, PREC_NONE},
    [SLEEPY_TOKEN_LOCAL] = {command, NULL, PREC_NONE},
    [SLEEPY_TOKEN_THIS] = {command, NULL, PREC_NONE},
    [SLEEPY_TOKEN_INC] = {unary, postfix, PREC_INCDEC},
    [SLEEPY_TOKEN_DEC] = {unary, postfix, PREC_INCDEC},
};

static ParseRule *get_rule(SleepyTokenType type) { return &rules[type]; }

static SleepyASTNode *parse_precedence(SleepyParser *parser,
                                       ParsePrecedence precedence) {
  advance(parser);
  ParseFn prefix_rule = get_rule(parser->previous.type)->prefix;
  if (prefix_rule == NULL) {
    error(parser, "Expect expression.");
    return NULL;
  }
  bool canAssign = precedence <= PREC_ASSIGNMENT;
  SleepyASTNode *expr = prefix_rule(parser, NULL, canAssign);

  // Special case: string juxtaposition concatenation
  // If the expression we just parsed is a string literal, and the NEXT token
  // is ALSO a string literal, we should concatenate them into a single node.
  if (expr &&
      (expr->type == SLEEPY_AST_STRING || expr->type == SLEEPY_AST_LITERAL) &&
      (check(parser, SLEEPY_TOKEN_STRING) ||
       check(parser, SLEEPY_TOKEN_LITERAL))) {

    SleepyStringBuffer buffer;
    sleepy_string_buffer_init(&buffer, parser->allocator);

    // Append the first string
    sleepy_string_buffer_append_string(
        &buffer, expr->as.string_val, sleepy_utils_strlen(expr->as.string_val));

    // While the next token is a string literal, consume and append it
    while (check(parser, SLEEPY_TOKEN_STRING) ||
           check(parser, SLEEPY_TOKEN_LITERAL)) {
      advance(parser);
      char *next =
          (char *)sleepy_lexer_copy_lexeme(&parser->lexer, &parser->previous);
      sleepy_string_buffer_append_string(&buffer, next,
                                         sleepy_utils_strlen(next));
      parser->allocator->reallocate(next, 0, parser->allocator->user_data);
    }

    // Update the expression to be the concatenated string
    expr->type = SLEEPY_AST_STRING;
    // Free the old string value
    parser->allocator->reallocate((void *)expr->as.string_val, 0,
                                  parser->allocator->user_data);

    char *new_str = (char *)parser->allocator->reallocate(
        NULL, buffer.length + 1, parser->allocator->user_data);
    sleepy_utils_memcpy(new_str, buffer.data, buffer.length);
    new_str[buffer.length] = '\0';
    expr->as.string_val = new_str;

    sleepy_string_buffer_free(&buffer);
  }

  while (precedence <= get_rule(parser->current.type)->precedence) {
    advance(parser);
    ParseFn infix_rule = get_rule(parser->previous.type)->infix;
    if (infix_rule != NULL)
      expr = infix_rule(parser, expr, canAssign);
  }
  return expr;
}

static SleepyASTNode *expression(SleepyParser *parser) {
  return parse_precedence(parser, PREC_ASSIGNMENT);
}

// -----------------------------------------------------------------------------
// Statements and Declarations
// -----------------------------------------------------------------------------

static SleepyASTNode *expression_statement(SleepyParser *parser) {
  SleepyASTNode *expr = expression(parser);
  match(parser, SLEEPY_TOKEN_SEMICOLON);
  return expr;
}

static SleepyASTNode *if_statement(SleepyParser *parser) {
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_IF);
  consume(parser, SLEEPY_TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  node->as.if_stmt.condition = expression(parser);
  consume(parser, SLEEPY_TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
  node->as.if_stmt.then_branch = statement(parser);
  if (match(parser, SLEEPY_TOKEN_ELSE))
    node->as.if_stmt.else_branch = statement(parser);
  else
    node->as.if_stmt.else_branch = NULL;
  return node;
}

static SleepyASTNode *while_statement(SleepyParser *parser) {
  SleepyASTNode *assign_var = NULL;
  if (match(parser, SLEEPY_TOKEN_SCALAR))
    assign_var = scalar(parser, NULL, false);
  consume(parser, SLEEPY_TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
  SleepyASTNode *condition = expression(parser);
  consume(parser, SLEEPY_TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
  SleepyASTNode *body = block(parser);
  if (assign_var) {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_ASSIGN_LOOP);
    node->as.assign_loop.value = assign_var->as.string_val;
    node->as.assign_loop.generator = condition;
    node->as.assign_loop.body = body;
    return node;
  }
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_WHILE);
  node->as.while_stmt.condition = condition;
  node->as.while_stmt.body = body;
  return node;
}

static SleepyASTNode *for_statement(SleepyParser *parser) {
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_FOR);
  consume(parser, SLEEPY_TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

  // Parse initializer(s)
  node->as.for_stmt.initializer = NULL;
  node->as.for_stmt.init_count = 0;
  if (!check(parser, SLEEPY_TOKEN_SEMICOLON)) {
    size_t init_capacity = 4;
    node->as.for_stmt.initializer =
        (SleepyASTNode **)parser->allocator->reallocate(
            NULL, sizeof(SleepyASTNode *) * init_capacity,
            parser->allocator->user_data);

    while (!check(parser, SLEEPY_TOKEN_SEMICOLON) &&
           !check(parser, SLEEPY_TOKEN_EOF)) {
      if (node->as.for_stmt.init_count >= init_capacity) {
        init_capacity *= 2;
        node->as.for_stmt.initializer =
            (SleepyASTNode **)parser->allocator->reallocate(
                node->as.for_stmt.initializer,
                sizeof(SleepyASTNode *) * init_capacity,
                parser->allocator->user_data);
      }
      node->as.for_stmt.initializer[node->as.for_stmt.init_count++] =
          expression(parser);
      if (!match(parser, SLEEPY_TOKEN_COMMA))
        break;
    }
  }
  consume(parser, SLEEPY_TOKEN_SEMICOLON,
          "Expect ';' after for loop initializer.");

  // Parse condition
  node->as.for_stmt.condition = NULL;
  if (!check(parser, SLEEPY_TOKEN_SEMICOLON)) {
    node->as.for_stmt.condition = expression(parser);
  }
  consume(parser, SLEEPY_TOKEN_SEMICOLON,
          "Expect ';' after for loop condition.");

  // Parse increment(s)
  node->as.for_stmt.increment = NULL;
  node->as.for_stmt.inc_count = 0;
  if (!check(parser, SLEEPY_TOKEN_RIGHT_PAREN) &&
      !check(parser, SLEEPY_TOKEN_EOF)) {
    size_t inc_capacity = 4;
    node->as.for_stmt.increment =
        (SleepyASTNode **)parser->allocator->reallocate(
            NULL, sizeof(SleepyASTNode *) * inc_capacity,
            parser->allocator->user_data);

    while (!check(parser, SLEEPY_TOKEN_RIGHT_PAREN) &&
           !check(parser, SLEEPY_TOKEN_EOF)) {
      if (node->as.for_stmt.inc_count >= inc_capacity) {
        inc_capacity *= 2;
        node->as.for_stmt.increment =
            (SleepyASTNode **)parser->allocator->reallocate(
                node->as.for_stmt.increment,
                sizeof(SleepyASTNode *) * inc_capacity,
                parser->allocator->user_data);
      }
      node->as.for_stmt.increment[node->as.for_stmt.inc_count++] =
          expression(parser);
      if (!match(parser, SLEEPY_TOKEN_COMMA))
        break;
    }
  }
  consume(parser, SLEEPY_TOKEN_RIGHT_PAREN,
          "Expect ')' after for loop increment.");

  node->as.for_stmt.body = statement(parser);
  return node;
}

static SleepyASTNode *foreach_statement(SleepyParser *parser) {
  SleepyASTNode *key_node = NULL, *val_node = NULL;
  if (match(parser, SLEEPY_TOKEN_SCALAR)) {
    val_node = scalar(parser, NULL, false);
    if (match(parser, SLEEPY_TOKEN_ARROW)) {
      key_node = val_node;
      consume(parser, SLEEPY_TOKEN_SCALAR, "Expect value variable.");
      val_node = scalar(parser, NULL, false);
    }
  }
  consume(parser, SLEEPY_TOKEN_LEFT_PAREN,
          "Expect '(' after foreach variable.");
  SleepyASTNode *source = expression(parser);
  consume(parser, SLEEPY_TOKEN_RIGHT_PAREN, "Expect ')' after collection.");
  SleepyASTNode *body = block(parser);
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_FOREACH);
  node->as.foreach.index = (key_node) ? key_node->as.string_val : NULL;
  
  if (val_node) {
      node->as.foreach.value = val_node->as.string_val;
  } else {
      char *stub = (char *)parser->allocator->reallocate(NULL, 9, parser->allocator->user_data);
      if (stub) sleepy_utils_strcpy(stub, "stub_var");
      node->as.foreach.value = stub;
  }
  
  if (key_node) SLEEPY_FREE(parser->allocator, key_node);
  if (val_node) SLEEPY_FREE(parser->allocator, val_node);

  node->as.foreach.generator = source;
  node->as.foreach.body = body;
  return node;
}

static SleepyASTNode *try_statement(SleepyParser *parser) {
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_TRY_CATCH);
  node->as.try_catch.body = block(parser);
  if (match(parser, SLEEPY_TOKEN_CATCH)) {
    // Advance to the scalar token
    advance(parser);
    SleepyASTNode *var_node = scalar(parser, NULL, false);
    node->as.try_catch.value = var_node->as.string_val;
    SLEEPY_FREE(parser->allocator, var_node);
    node->as.try_catch.handler = block(parser);
  } else
    node->as.try_catch.handler = NULL;
  return node;
}

static SleepyASTNode *statement(SleepyParser *parser) {
  if (match(parser, SLEEPY_TOKEN_SEMICOLON))
    return allocate_node(parser, SLEEPY_AST_NOP);
  if (match(parser, SLEEPY_TOKEN_IF))
    return if_statement(parser);
  if (match(parser, SLEEPY_TOKEN_WHILE))
    return while_statement(parser);
  if (match(parser, SLEEPY_TOKEN_FOR))
    return for_statement(parser);
  if (match(parser, SLEEPY_TOKEN_FOREACH))
    return foreach_statement(parser);
  if (match(parser, SLEEPY_TOKEN_TRY))
    return try_statement(parser);
  if (check(parser, SLEEPY_TOKEN_LEFT_BRACE))
    return block(parser);
  return expression_statement(parser);
}

static SleepyASTNode *import_statement(SleepyParser *parser) {
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_IMPORT);
  node->as.import_stmt.target = NULL;
  node->as.import_stmt.path = NULL;

  // Parse import target (e.g., java.util.*)
  // Build up the target string
  char target_buffer[512];
  size_t target_len = 0;
  target_buffer[0] = '\0';

  if (check(parser, SLEEPY_TOKEN_ID) || check(parser, SLEEPY_TOKEN_STRING)) {
    if (match(parser, SLEEPY_TOKEN_ID) || match(parser, SLEEPY_TOKEN_STRING)) {
      const char *part =
          sleepy_lexer_copy_lexeme(&parser->lexer, &parser->previous);
      size_t part_len = sleepy_utils_strlen(part);
      if (target_len + part_len < sizeof(target_buffer) - 1) {
        sleepy_utils_strcpy(target_buffer + target_len, part);
        target_len += part_len;
      }
      SLEEPY_FREE(parser->allocator, (void *)part);

      // Check for dotted names (e.g., java.util.*)
      while (match(parser, SLEEPY_TOKEN_DOT)) {
        if (target_len + 1 < sizeof(target_buffer) - 1) {
          target_buffer[target_len++] = '.';
          target_buffer[target_len] = '\0';
        }
        if (match(parser, SLEEPY_TOKEN_ID) ||
            match(parser, SLEEPY_TOKEN_STRING) ||
            match(parser, SLEEPY_TOKEN_STAR)) {
          const char *next_part =
              sleepy_lexer_copy_lexeme(&parser->lexer, &parser->previous);
          size_t next_len = sleepy_utils_strlen(next_part);
          if (target_len + next_len < sizeof(target_buffer) - 1) {
            sleepy_utils_strcpy(target_buffer + target_len, next_part);
            target_len += next_len;
          }
          SLEEPY_FREE(parser->allocator, (void *)next_part);
        }
      }
    }
  }

  // Store the target string
  if (target_len > 0) {
    char *stored_target = (char *)parser->allocator->reallocate(
        NULL, target_len + 1, parser->allocator->user_data);
    sleepy_utils_strcpy(stored_target, target_buffer);
    node->as.import_stmt.target = stored_target;
  }

  // Check for optional FROM ':' path
  if (match(parser, SLEEPY_TOKEN_FROM)) {
    consume(parser, SLEEPY_TOKEN_COLON, "Expect ':' after FROM.");
    if (match(parser, SLEEPY_TOKEN_STRING) ||
        match(parser, SLEEPY_TOKEN_IMPORT_PATH) ||
        match(parser, SLEEPY_TOKEN_LITERAL)) {
      node->as.import_stmt.path =
          sleepy_lexer_copy_lexeme(&parser->lexer, &parser->previous);
    }
  }

  // Consume the semicolon at the end
  match(parser, SLEEPY_TOKEN_SEMICOLON);

  return node;
}

static SleepyASTNode *declaration(SleepyParser *parser) {
  if (match(parser, SLEEPY_TOKEN_SUB) || match(parser, SLEEPY_TOKEN_INLINE)) {
    return bridge(parser, NULL, false);
  }
  if (match(parser, SLEEPY_TOKEN_IMPORT)) {
    return import_statement(parser);
  }

  // Generic Bridge Detection: ID [ID|STRING|LITERAL]? {
  if (check(parser, SLEEPY_TOKEN_ID)) {
    const char *saved_current = parser->lexer.current;
    int saved_line = parser->lexer.line;
    SleepyToken next = sleepy_lexer_scan_token(&parser->lexer);
    parser->lexer.current = saved_current;
    parser->lexer.line = saved_line;

    if (next.type == SLEEPY_TOKEN_ID || next.type == SLEEPY_TOKEN_STRING ||
        next.type == SLEEPY_TOKEN_LITERAL ||
        next.type == SLEEPY_TOKEN_LEFT_BRACE) {
      advance(parser); // Consume the keyword identifier
      return bridge(parser, NULL, false);
    }
  }

  return statement(parser);
}

static SleepyASTNode *block(SleepyParser *parser) {
  consume(parser, SLEEPY_TOKEN_LEFT_BRACE, "Expect '{' to start block.");
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_BLOCK);
  node->as.block.capacity = 8;
  node->as.block.count = 0;
  node->as.block.statements = (SleepyASTNode **)parser->allocator->reallocate(
      NULL, sizeof(SleepyASTNode *) * node->as.block.capacity,
      parser->allocator->user_data);
  while (!check(parser, SLEEPY_TOKEN_RIGHT_BRACE) &&
         !check(parser, SLEEPY_TOKEN_EOF) && !parser->had_error) {
    SleepyASTNode *stmt = declaration(parser);
    if (stmt) {
      if (node->as.block.count >= node->as.block.capacity) {
        node->as.block.capacity *= 2;
        node->as.block.statements =
            (SleepyASTNode **)parser->allocator->reallocate(
                node->as.block.statements,
                sizeof(SleepyASTNode *) * node->as.block.capacity,
                parser->allocator->user_data);
      }
      node->as.block.statements[node->as.block.count++] = stmt;
    }
  }
  consume(parser, SLEEPY_TOKEN_RIGHT_BRACE, "Expect '}' after block.");
  return node;
}

SleepyASTNode **parse_args(SleepyParser *parser, size_t *count,
                           SleepyTokenType endToken) {
  size_t capacity = 4;
  *count = 0;
  SleepyASTNode **args = (SleepyASTNode **)parser->allocator->reallocate(
      NULL, sizeof(SleepyASTNode *) * capacity, parser->allocator->user_data);
  if (!check(parser, endToken)) {
    do {
      if (check(parser, endToken))
        break;
      if (*count >= capacity) {
        capacity *= 2;
        args = (SleepyASTNode **)parser->allocator->reallocate(
            args, sizeof(SleepyASTNode *) * capacity,
            parser->allocator->user_data);
      }
      SleepyASTNode *expr = NULL;

      // Special case: 'x' (as LOWER_X or ID) followed by '=>' should be treated
      // as an identifier This is used in hash literals like: hash(x => "value")
      if ((check(parser, SLEEPY_TOKEN_LOWER_X) ||
           check(parser, SLEEPY_TOKEN_ID)) &&
          parser->current.length == 1 && parser->current.start[0] == 'x') {
        // Check if the next token is =>
        const char *saved_current = parser->lexer.current;
        SleepyToken next_token = sleepy_lexer_scan_token(&parser->lexer);
        parser->lexer.current = saved_current; // Restore position

        if (next_token.type == SLEEPY_TOKEN_ARROW) {
          advance(parser);
          expr = allocate_node(parser, SLEEPY_AST_IDENTIFIER);
          expr->as.string_val =
              sleepy_lexer_copy_lexeme(&parser->lexer, &parser->previous);
        } else {
          expr = expression(parser);
        }
      } else {
        expr = expression(parser);
      }

      SleepyASTNode *arg_node = allocate_node(parser, SLEEPY_AST_ARG);
      if (match(parser, SLEEPY_TOKEN_ARROW)) {
        arg_node->as.arg.name = expr;
        arg_node->as.arg.value = expression(parser);
      } else {
        arg_node->as.arg.name = NULL;
        arg_node->as.arg.value = expr;
      }
      args[(*count)++] = arg_node;
      if (check(parser, endToken))
        break;
    } while ((match(parser, SLEEPY_TOKEN_COMMA) ||
              (!check(parser, endToken) && !check(parser, SLEEPY_TOKEN_EOF))) &&
             !parser->had_error);
  }
  return args;
}

SleepyASTNode *sleepy_parser_parse(SleepyParser *parser) {
  advance(parser);
  if (match(parser, SLEEPY_TOKEN_EOF))
    return NULL;
  SleepyASTNode *script = allocate_node(parser, SLEEPY_AST_SCRIPT);
  script->as.block.capacity = 8;
  script->as.block.count = 0;
  script->as.block.statements = (SleepyASTNode **)parser->allocator->reallocate(
      NULL, sizeof(SleepyASTNode *) * script->as.block.capacity,
      parser->allocator->user_data);
  while (!match(parser, SLEEPY_TOKEN_EOF) && !parser->had_error) {
    SleepyASTNode *stmt = declaration(parser);
    if (stmt) {
      if (script->as.block.count >= script->as.block.capacity) {
        script->as.block.capacity *= 2;
        script->as.block.statements =
            (SleepyASTNode **)parser->allocator->reallocate(
                script->as.block.statements,
                sizeof(SleepyASTNode *) * script->as.block.capacity,
                parser->allocator->user_data);
      }
      script->as.block.statements[script->as.block.count++] = stmt;
    }
  }
  return script;
}

/* --- File: ../src/sleepy_ast.c --- */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Forward declaration of internal recursive formatter
static void format_node(SleepyASTNode *node, SleepyStringBuffer *buffer,
                        int depth);

// Helper for operator token types
static SleepyTokenType get_token_type_from_op(const char *op) {
  if (!op)
    return SLEEPY_TOKEN_ERROR;
  if (strcmp(op, "+") == 0)
    return SLEEPY_TOKEN_PLUS;
  if (strcmp(op, "-") == 0)
    return SLEEPY_TOKEN_MINUS;
  if (strcmp(op, "*") == 0)
    return SLEEPY_TOKEN_STAR;
  if (strcmp(op, "/") == 0)
    return SLEEPY_TOKEN_SLASH;
  if (strcmp(op, "%") == 0)
    return SLEEPY_TOKEN_PERCENT;
  if (strcmp(op, "==") == 0)
    return SLEEPY_TOKEN_EQ;
  if (strcmp(op, "!=") == 0)
    return SLEEPY_TOKEN_NE;
  if (strcmp(op, "<") == 0)
    return SLEEPY_TOKEN_LESS;
  if (strcmp(op, "<=") == 0)
    return SLEEPY_TOKEN_LE;
  if (strcmp(op, ">") == 0)
    return SLEEPY_TOKEN_GREATER;
  if (strcmp(op, ">=") == 0)
    return SLEEPY_TOKEN_GE;
  if (strcmp(op, "&&") == 0)
    return SLEEPY_TOKEN_LAND;
  if (strcmp(op, "||") == 0)
    return SLEEPY_TOKEN_LOR;
  if (strcmp(op, "=") == 0)
    return SLEEPY_TOKEN_EQUAL;
  return SLEEPY_TOKEN_ERROR;
}

char *sleepy_ast_format(SleepyASTNode *node, SleepyAllocator *allocator) {
  if (!node || !allocator)
    return NULL;

  SleepyStringBuffer buffer;
  sleepy_string_buffer_init(&buffer, allocator);

  format_node(node, &buffer, 0);

  return buffer.data;
}

// --- Bindings Helpers ---

void sleepy_ast_get_children(SleepyASTNode *node, SleepyASTNode ***out_children,
                             size_t *out_count, SleepyAllocator *allocator) {
  *out_children = NULL;
  *out_count = 0;
  if (!node || !allocator)
    return;

  switch (node->type) {
  case SLEEPY_AST_SCRIPT:
  case SLEEPY_AST_BLOCK: {
    if (node->as.block.count == 0)
      break;
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, node->as.block.count * sizeof(SleepyASTNode *),
        allocator->user_data);
    for (size_t i = 0; i < node->as.block.count; i++) {
      children[i] = node->as.block.statements[i];
    }
    *out_children = children;
    *out_count = node->as.block.count;
    break;
  }
  case SLEEPY_AST_CALL: {
    if (node->as.call.arg_count == 0)
      break;
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, node->as.call.arg_count * sizeof(SleepyASTNode *),
        allocator->user_data);
    for (size_t i = 0; i < node->as.call.arg_count; i++) {
      children[i] = node->as.call.args[i];
    }
    *out_children = children;
    *out_count = node->as.call.arg_count;
    break;
  }
  case SLEEPY_AST_BINOP: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    children[0] = node->as.binop.left;
    children[1] = node->as.binop.right;
    *out_children = children;
    *out_count = 2;
    break;
  }
  case SLEEPY_AST_ASSIGNMENT: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    children[0] = node->as.assign.left;
    children[1] = node->as.assign.right;
    *out_children = children;
    *out_count = 2;
    break;
  }
  case SLEEPY_AST_ARG:
  case SLEEPY_AST_KV_PAIR: {
    size_t cnt =
        (node->type == SLEEPY_AST_KV_PAIR && node->as.arg.name) ? 2 : 1;
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, cnt * sizeof(SleepyASTNode *), allocator->user_data);
    if (cnt == 2) {
      children[0] = node->as.arg.name;
      children[1] = node->as.arg.value;
    } else {
      children[0] = node->as.arg.value;
    }
    *out_children = children;
    *out_count = cnt;
    break;
  }
  case SLEEPY_AST_ENV_BRIDGE: {
    if (node->as.env_bridge.body) {
      SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
          NULL, 1 * sizeof(SleepyASTNode *), allocator->user_data);
      children[0] = node->as.env_bridge.body;
      *out_children = children;
      *out_count = 1;
    }
    break;
  }
  case SLEEPY_AST_IF: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 3 * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.if_stmt.condition)
      children[count++] = node->as.if_stmt.condition;
    if (node->as.if_stmt.then_branch)
      children[count++] = node->as.if_stmt.then_branch;
    if (node->as.if_stmt.else_branch)
      children[count++] = node->as.if_stmt.else_branch;
    if (count > 0) {
      *out_children = children;
      *out_count = count;
    } else {
      allocator->reallocate(children, 0, allocator->user_data);
    }
    break;
  }
  case SLEEPY_AST_WHILE: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.while_stmt.condition)
      children[count++] = node->as.while_stmt.condition;
    if (node->as.while_stmt.body)
      children[count++] = node->as.while_stmt.body;
    if (count > 0) {
      *out_children = children;
      *out_count = count;
    } else {
      allocator->reallocate(children, 0, allocator->user_data);
    }
    break;
  }
  case SLEEPY_AST_FOR: {
    size_t total_count =
        node->as.for_stmt.init_count + 1 + node->as.for_stmt.inc_count + 1;
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, total_count * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;
    for (size_t i = 0; i < node->as.for_stmt.init_count; i++) {
      children[count++] = node->as.for_stmt.initializer[i];
    }
    if (node->as.for_stmt.condition)
      children[count++] = node->as.for_stmt.condition;
    for (size_t i = 0; i < node->as.for_stmt.inc_count; i++) {
      children[count++] = node->as.for_stmt.increment[i];
    }
    if (node->as.for_stmt.body)
      children[count++] = node->as.for_stmt.body;
    if (count > 0) {
      *out_children = children;
      *out_count = count;
    } else {
      allocator->reallocate(children, 0, allocator->user_data);
    }
    break;
  }
  case SLEEPY_AST_FOREACH:
  case SLEEPY_AST_ASSIGN_LOOP: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->type == SLEEPY_AST_FOREACH) {
      if (node->as.foreach.generator)
        children[count++] = node->as.foreach.generator;
      if (node->as.foreach.body)
        children[count++] = node->as.foreach.body;
    } else {
      if (node->as.assign_loop.generator)
        children[count++] = node->as.assign_loop.generator;
      if (node->as.assign_loop.body)
        children[count++] = node->as.assign_loop.body;
    }
    if (count > 0) {
      *out_children = children;
      *out_count = count;
    } else {
      allocator->reallocate(children, 0, allocator->user_data);
    }
    break;
  }
  case SLEEPY_AST_RETURN:
  case SLEEPY_AST_THROW:
  case SLEEPY_AST_ASSERT:
  case SLEEPY_AST_YIELD:
  case SLEEPY_AST_LOCAL:
  case SLEEPY_AST_THIS: {
    if (node->as.control.value) {
      SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
          NULL, 1 * sizeof(SleepyASTNode *), allocator->user_data);
      children[0] = node->as.control.value;
      *out_children = children;
      *out_count = 1;
    }
    break;
  }
  case SLEEPY_AST_UNARYOP: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 1 * sizeof(SleepyASTNode *), allocator->user_data);
    children[0] = node->as.unaryop.operand;
    *out_children = children;
    *out_count = 1;
    break;
  }
  case SLEEPY_AST_INDEX: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.index.container)
      children[count++] = node->as.index.container;
    if (node->as.index.element)
      children[count++] = node->as.index.element;
    *out_children = children;
    *out_count = count;
    break;
  }
  case SLEEPY_AST_OBJ_EXPR: {
    size_t total = 2 + node->as.obj_expr.arg_count;
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, total * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.obj_expr.target)
      children[count++] = node->as.obj_expr.target;
    if (node->as.obj_expr.message)
      children[count++] = node->as.obj_expr.message;
    for (size_t i = 0; i < node->as.obj_expr.arg_count; i++) {
      children[count++] = node->as.obj_expr.args[i];
    }
    *out_children = children;
    *out_count = count;
    break;
  }
  case SLEEPY_AST_TRY_CATCH: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.try_catch.body)
      children[count++] = node->as.try_catch.body;
    if (node->as.try_catch.handler)
      children[count++] = node->as.try_catch.handler;
    *out_children = children;
    *out_count = count;
    break;
  }
  default:
    break;
  }
}

void sleepy_ast_free_children(SleepyASTNode **children,
                              SleepyAllocator *allocator) {
  if (children && allocator) {
    allocator->reallocate(children, 0, allocator->user_data);
  }
}

const char *sleepy_ast_get_string(SleepyASTNode *node) {
  if (!node)
    return NULL;
  switch (node->type) {
  case SLEEPY_AST_STRING:
  case SLEEPY_AST_LITERAL:
  case SLEEPY_AST_SCALAR:
  case SLEEPY_AST_ARRAY:
  case SLEEPY_AST_HASHTABLE:
  case SLEEPY_AST_IDENTIFIER:
  case SLEEPY_AST_CLASS_LITERAL:
  case SLEEPY_AST_BACKTICK:
  case SLEEPY_AST_ADDRESS:
    return node->as.string_val;
  case SLEEPY_AST_ENV_BRIDGE:
    return node->as.env_bridge.keyword;
  case SLEEPY_AST_CALL:
    if (node->as.call.target &&
        (node->as.call.target->type == SLEEPY_AST_IDENTIFIER ||
         node->as.call.target->type == SLEEPY_AST_HASHTABLE ||
         node->as.call.target->type == SLEEPY_AST_ARRAY)) {
      return node->as.call.target->as.string_val;
    }
    return NULL;
  default:
    return NULL;
  }
}

const char *sleepy_ast_get_op(SleepyASTNode *node) {
  if (!node)
    return NULL;
  switch (node->type) {
  case SLEEPY_AST_BINOP:
    return node->as.binop.op.start;
  case SLEEPY_AST_UNARYOP:
    return node->as.unaryop.op.start;
  case SLEEPY_AST_ASSIGNMENT:
    return node->as.assign.op.start;
  default:
    return NULL;
  }
}

size_t sleepy_ast_get_op_length(SleepyASTNode *node) {
  if (!node)
    return 0;
  switch (node->type) {
  case SLEEPY_AST_BINOP:
    return node->as.binop.op.length;
  case SLEEPY_AST_UNARYOP:
    return node->as.unaryop.op.length;
  case SLEEPY_AST_ASSIGNMENT:
    return node->as.assign.op.length;
  default:
    return 0;
  }
}

long sleepy_ast_get_long(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_LONG) ? node->as.long_val : 0;
}

double sleepy_ast_get_double(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_NUMBER) ? node->as.double_val : 0.0;
}

bool sleepy_ast_get_bool(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_BOOLEAN) ? node->as.boolean : false;
}

const char *sleepy_ast_get_env_bridge_id(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_ENV_BRIDGE)
             ? node->as.env_bridge.identifier
             : NULL;
}

const char *sleepy_ast_get_env_bridge_string(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_ENV_BRIDGE)
             ? node->as.env_bridge.string
             : NULL;
}

const char *sleepy_ast_get_foreach_index(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_FOREACH) ? node->as.foreach.index
                                                    : NULL;
}

const char *sleepy_ast_get_foreach_value(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_FOREACH) ? node->as.foreach.value
                                                    : NULL;
}

const char *sleepy_ast_get_import_path(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_IMPORT) ? node->as.import_stmt.path
                                                   : NULL;
}

const char *sleepy_ast_get_try_catch_var(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_TRY_CATCH) ? node->as.try_catch.value
                                                      : NULL;
}

size_t sleepy_ast_get_for_init_count(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_FOR) ? node->as.for_stmt.init_count
                                                : 0;
}

size_t sleepy_ast_get_for_inc_count(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_FOR) ? node->as.for_stmt.inc_count
                                                : 0;
}

// --- AST Builder APIs for FFI ---

SleepyASTNode *sleepy_ast_build_node(SleepyASTNodeType type, int line,
                                     SleepyAllocator *allocator) {
  SleepyASTNode *node =
      (SleepyASTNode *)allocator->reallocate(NULL, sizeof(SleepyASTNode), NULL);
  if (!node)
    return NULL;
  memset(node, 0, sizeof(SleepyASTNode));
  node->type = type;
  node->line = line;
  return node;
}

void sleepy_ast_set_string_val(SleepyASTNode *node, const char *str,
                               SleepyAllocator *allocator) {
  if (!node || !str || !allocator)
    return;
  size_t len = strlen(str);
  char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
  if (dup) {
    strcpy(dup, str);
    switch (node->type) {
    case SLEEPY_AST_STRING:
    case SLEEPY_AST_LITERAL:
    case SLEEPY_AST_SCALAR:
    case SLEEPY_AST_ARRAY:
    case SLEEPY_AST_HASHTABLE:
    case SLEEPY_AST_IDENTIFIER:
    case SLEEPY_AST_CLASS_LITERAL:
    case SLEEPY_AST_BACKTICK:
    case SLEEPY_AST_ADDRESS:
      node->as.string_val = dup;
      break;
    default:
      allocator->reallocate(dup, 0, NULL);
      break;
    }
  }
}

void sleepy_ast_set_long_val(SleepyASTNode *node, long val) {
  if (node && node->type == SLEEPY_AST_LONG)
    node->as.long_val = val;
}

void sleepy_ast_set_double_val(SleepyASTNode *node, double val) {
  if (node && node->type == SLEEPY_AST_NUMBER)
    node->as.double_val = val;
}

void sleepy_ast_set_bool_val(SleepyASTNode *node, bool val) {
  if (node && node->type == SLEEPY_AST_BOOLEAN)
    node->as.boolean = val;
}

SleepyASTNode **sleepy_ast_allocate_children(size_t count,
                                             SleepyAllocator *allocator) {
  if (count == 0)
    return NULL;
  return (SleepyASTNode **)allocator->reallocate(
      NULL, count * sizeof(SleepyASTNode *), NULL);
}

void sleepy_ast_set_children(SleepyASTNode *node, SleepyASTNode **children,
                             size_t count, SleepyAllocator *allocator) {
  if (!node)
    return;
  SleepyASTNode **arr = NULL;
  if (count > 0 && children) {
    arr = (SleepyASTNode **)allocator->reallocate(
        NULL, count * sizeof(SleepyASTNode *), NULL);
    for (size_t i = 0; i < count; i++)
      arr[i] = children[i];
  }
  switch (node->type) {
  case SLEEPY_AST_SCRIPT:
  case SLEEPY_AST_BLOCK:
    node->as.block.statements = arr;
    node->as.block.count = count;
    node->as.block.capacity = count;
    break;
  case SLEEPY_AST_CALL:
    node->as.call.args = arr;
    node->as.call.arg_count = count;
    break;
  default:
    if (arr)
      allocator->reallocate(arr, 0, NULL);
    break;
  }
}

void sleepy_ast_set_binop(SleepyASTNode *node, SleepyASTNode *left,
                          SleepyASTNode *right) {
  if (node && node->type == SLEEPY_AST_BINOP) {
    node->as.binop.left = left;
    node->as.binop.right = right;
  }
}

void sleepy_ast_set_binop_with_op(SleepyASTNode *node, SleepyASTNode *left,
                                  SleepyASTNode *right, const char *op,
                                  SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_BINOP && allocator) {
    node->as.binop.left = left;
    node->as.binop.right = right;
    node->as.binop.op.type = get_token_type_from_op(op);
    size_t len = strlen(op);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, op);
      node->as.binop.op.start = dup;
      node->as.binop.op.length = len;
    }
  }
}

void sleepy_ast_set_unaryop_with_op(SleepyASTNode *node, SleepyASTNode *operand,
                                    const char *op,
                                    SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_UNARYOP && allocator) {
    node->as.unaryop.operand = operand;
    node->as.unaryop.op.type = get_token_type_from_op(op);
    size_t len = strlen(op);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, op);
      node->as.unaryop.op.start = dup;
      node->as.unaryop.op.length = len;
    }
  }
}

void sleepy_ast_set_if(SleepyASTNode *node, SleepyASTNode *condition,
                       SleepyASTNode *then_branch, SleepyASTNode *else_branch) {
  if (node && node->type == SLEEPY_AST_IF) {
    node->as.if_stmt.condition = condition;
    node->as.if_stmt.then_branch = then_branch;
    node->as.if_stmt.else_branch = else_branch;
  }
}

void sleepy_ast_set_while(SleepyASTNode *node, SleepyASTNode *condition,
                          SleepyASTNode *body) {
  if (node && node->type == SLEEPY_AST_WHILE) {
    node->as.while_stmt.condition = condition;
    node->as.while_stmt.body = body;
  }
}

void sleepy_ast_set_arg(SleepyASTNode *node, SleepyASTNode *value) {
  if (node && node->type == SLEEPY_AST_ARG)
    node->as.arg.value = value;
}

void sleepy_ast_set_arg_with_name(SleepyASTNode *node, SleepyASTNode *name, SleepyASTNode *value) {
  if (node && node->type == SLEEPY_AST_ARG) {
    node->as.arg.name = name;
    node->as.arg.value = value;
  }
}

void sleepy_ast_set_obj_expr(SleepyASTNode *node, SleepyASTNode *target,
                             SleepyASTNode *message, SleepyASTNode **args,
                             size_t arg_count, SleepyAllocator *allocator) {
  (void)allocator;
  if (node && node->type == SLEEPY_AST_OBJ_EXPR) {
    node->as.obj_expr.target = target;
    node->as.obj_expr.message = message;
    node->as.obj_expr.args = args;
    node->as.obj_expr.arg_count = arg_count;
  }
}

void sleepy_ast_set_assignment(SleepyASTNode *node, SleepyASTNode *target,
                               SleepyASTNode *value) {
  if (node && node->type == SLEEPY_AST_ASSIGNMENT) {
    node->as.assign.left = target;
    node->as.assign.right = value;
    node->as.assign.op.type = SLEEPY_TOKEN_EQUAL;
    node->as.assign.op.start = "=";
    node->as.assign.op.length = 1;
  }
}

void sleepy_ast_set_env_bridge_keyword(SleepyASTNode *node, const char *keyword,
                                       SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_ENV_BRIDGE && keyword) {
    size_t len = strlen(keyword);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, keyword);
      node->as.env_bridge.keyword = dup;
    }
  }
}

void sleepy_ast_set_env_bridge_id(SleepyASTNode *node, const char *id,
                                  SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_ENV_BRIDGE && id) {
    size_t len = strlen(id);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, id);
      node->as.env_bridge.identifier = dup;
    }
  }
}

void sleepy_ast_set_env_bridge_string(SleepyASTNode *node, const char *str,
                                      SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_ENV_BRIDGE && str) {
    size_t len = strlen(str);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, str);
      node->as.env_bridge.string = dup;
    }
  }
}

void sleepy_ast_set_env_bridge_body(SleepyASTNode *node, SleepyASTNode *body) {
  if (node && node->type == SLEEPY_AST_ENV_BRIDGE)
    node->as.env_bridge.body = body;
}

void sleepy_ast_set_foreach(SleepyASTNode *node, const char *index,
                            const char *value, SleepyASTNode *generator,
                            SleepyASTNode *body, SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_FOREACH) {
    if (index) {
      size_t len = strlen(index);
      char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
      if (dup) {
        strcpy(dup, index);
        node->as.foreach.index = dup;
      }
    }
    if (value) {
      size_t len = strlen(value);
      char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
      if (dup) {
        strcpy(dup, value);
        node->as.foreach.value = dup;
      }
    }
    node->as.foreach.generator = generator;
    node->as.foreach.body = body;
  }
}

void sleepy_ast_set_for(SleepyASTNode *node, SleepyASTNode **init,
                        size_t init_cnt, SleepyASTNode *cond,
                        SleepyASTNode **inc, size_t inc_cnt,
                        SleepyASTNode *body, SleepyAllocator *allocator) {
  (void)allocator;
  if (node && node->type == SLEEPY_AST_FOR) {
    node->as.for_stmt.initializer = init;
    node->as.for_stmt.init_count = init_cnt;
    node->as.for_stmt.condition = cond;
    node->as.for_stmt.increment = inc;
    node->as.for_stmt.inc_count = inc_cnt;
    node->as.for_stmt.body = body;
  }
}

void sleepy_ast_set_return(SleepyASTNode *node, SleepyASTNode *value) {
  if (node && node->type == SLEEPY_AST_RETURN)
    node->as.control.value = value;
}

void sleepy_ast_set_break(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_BREAK) {
  }
}

void sleepy_ast_set_continue(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_CONTINUE) {
  }
}

void sleepy_ast_set_yield(SleepyASTNode *node, SleepyASTNode *value) {
  if (node && node->type == SLEEPY_AST_YIELD)
    node->as.control.value = value;
}

void sleepy_ast_set_throw(SleepyASTNode *node, SleepyASTNode *value) {
  if (node && node->type == SLEEPY_AST_THROW)
    node->as.control.value = value;
}

void sleepy_ast_set_assert(SleepyASTNode *node, SleepyASTNode *value) {
  if (node && node->type == SLEEPY_AST_ASSERT)
    node->as.control.value = value;
}

void sleepy_ast_set_try_catch(SleepyASTNode *node, SleepyASTNode *body,
                              const char *var, SleepyASTNode *handler,
                              SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_TRY_CATCH) {
    node->as.try_catch.body = body;
    if (var) {
      size_t len = strlen(var);
      char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
      if (dup) {
        strcpy(dup, var);
        node->as.try_catch.value = dup;
      }
    }
    node->as.try_catch.handler = handler;
  }
}

void sleepy_ast_set_import(SleepyASTNode *node, const char *path,
                           SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_IMPORT && path) {
    size_t len = strlen(path);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, path);
      node->as.import_stmt.path = dup;
    }
  }
}

void sleepy_ast_set_backtick(SleepyASTNode *node, const char *cmd,
                             SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_BACKTICK && cmd) {
    size_t len = strlen(cmd);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, cmd);
      node->as.string_val = dup;
    }
  }
}

void sleepy_ast_set_call_target(SleepyASTNode *node, const char *target,
                                SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_CALL && target) {
    SleepyASTNode *id =
        sleepy_ast_build_node(SLEEPY_AST_IDENTIFIER, node->line, allocator);
    sleepy_ast_set_string_val(id, target, allocator);
    node->as.call.target = id;
  }
}

// Memory Management for FFI-built nodes
void sleepy_parser_free_node(SleepyASTNode *node, SleepyAllocator *allocator) {
  if (!node || !allocator)
    return;

  // 1. Free Value Strings
  switch (node->type) {
  case SLEEPY_AST_STRING:
  case SLEEPY_AST_LITERAL:
  case SLEEPY_AST_SCALAR:
  case SLEEPY_AST_ARRAY:
  case SLEEPY_AST_HASHTABLE:
  case SLEEPY_AST_IDENTIFIER:
  case SLEEPY_AST_CLASS_LITERAL:
  case SLEEPY_AST_BACKTICK:
  case SLEEPY_AST_ADDRESS:
    if (node->as.string_val)
      allocator->reallocate((void *)node->as.string_val, 0, NULL);
    break;
  default:
    break;
  }

  // 2. Free recursive children and specific non-list children
  switch (node->type) {
  case SLEEPY_AST_SCRIPT:
  case SLEEPY_AST_BLOCK:
    if (node->as.block.statements) {
      for (size_t i = 0; i < node->as.block.count; i++) {
        if (node->as.block.statements[i])
          sleepy_parser_free_node(node->as.block.statements[i], allocator);
      }
      allocator->reallocate(node->as.block.statements, 0, NULL);
    }
    break;
  case SLEEPY_AST_CALL:
    if (node->as.call.args) {
      for (size_t i = 0; i < node->as.call.arg_count; i++) {
        if (node->as.call.args[i])
          sleepy_parser_free_node(node->as.call.args[i], allocator);
      }
      allocator->reallocate(node->as.call.args, 0, NULL);
    }
    if (node->as.call.target)
      sleepy_parser_free_node(node->as.call.target, allocator);
    break;
  case SLEEPY_AST_ENV_BRIDGE:
    if (node->as.env_bridge.body) {
      sleepy_parser_free_node(node->as.env_bridge.body, allocator);
    }
    if (node->as.env_bridge.keyword) {
      allocator->reallocate((void *)node->as.env_bridge.keyword, 0, NULL);
    }
    if (node->as.env_bridge.identifier) {
      allocator->reallocate((void *)node->as.env_bridge.identifier, 0, NULL);
    }
    if (node->as.env_bridge.string) {
      allocator->reallocate((void *)node->as.env_bridge.string, 0, NULL);
    }
    break;
  case SLEEPY_AST_BINOP:
    if (node->as.binop.left)
      sleepy_parser_free_node(node->as.binop.left, allocator);
    if (node->as.binop.right)
      sleepy_parser_free_node(node->as.binop.right, allocator);
    break;
  case SLEEPY_AST_UNARYOP:
    if (node->as.unaryop.operand)
      sleepy_parser_free_node(node->as.unaryop.operand, allocator);
    break;
  case SLEEPY_AST_IF:
    if (node->as.if_stmt.condition)
      sleepy_parser_free_node(node->as.if_stmt.condition, allocator);
    if (node->as.if_stmt.then_branch)
      sleepy_parser_free_node(node->as.if_stmt.then_branch, allocator);
    if (node->as.if_stmt.else_branch)
      sleepy_parser_free_node(node->as.if_stmt.else_branch, allocator);
    break;
  case SLEEPY_AST_WHILE:
    if (node->as.while_stmt.condition)
      sleepy_parser_free_node(node->as.while_stmt.condition, allocator);
    if (node->as.while_stmt.body)
      sleepy_parser_free_node(node->as.while_stmt.body, allocator);
    break;
  case SLEEPY_AST_FOR:
    if (node->as.for_stmt.initializer) {
      for (size_t i = 0; i < node->as.for_stmt.init_count; i++)
        if (node->as.for_stmt.initializer[i])
          sleepy_parser_free_node(node->as.for_stmt.initializer[i], allocator);
      allocator->reallocate(node->as.for_stmt.initializer, 0, NULL);
    }
    if (node->as.for_stmt.condition)
      sleepy_parser_free_node(node->as.for_stmt.condition, allocator);
    if (node->as.for_stmt.increment) {
      for (size_t i = 0; i < node->as.for_stmt.inc_count; i++)
        if (node->as.for_stmt.increment[i])
          sleepy_parser_free_node(node->as.for_stmt.increment[i], allocator);
      allocator->reallocate(node->as.for_stmt.increment, 0, NULL);
    }
    if (node->as.for_stmt.body)
      sleepy_parser_free_node(node->as.for_stmt.body, allocator);
    break;
  case SLEEPY_AST_FOREACH:
    if (node->as.foreach.generator)
      sleepy_parser_free_node(node->as.foreach.generator, allocator);
    if (node->as.foreach.body)
      sleepy_parser_free_node(node->as.foreach.body, allocator);
    if (node->as.foreach.index)
      allocator->reallocate((void *)node->as.foreach.index, 0, NULL);
    if (node->as.foreach.value)
      allocator->reallocate((void *)node->as.foreach.value, 0, NULL);
    break;
  case SLEEPY_AST_ASSIGNMENT:
    if (node->as.assign.left)
      sleepy_parser_free_node(node->as.assign.left, allocator);
    if (node->as.assign.right)
      sleepy_parser_free_node(node->as.assign.right, allocator);
    break;
  case SLEEPY_AST_ARG:
  case SLEEPY_AST_KV_PAIR:
    if (node->as.arg.value)
      sleepy_parser_free_node(node->as.arg.value, allocator);
    if (node->type == SLEEPY_AST_KV_PAIR && node->as.arg.name)
      sleepy_parser_free_node(node->as.arg.name, allocator);
    break;
  case SLEEPY_AST_TRY_CATCH:
    if (node->as.try_catch.body)
      sleepy_parser_free_node(node->as.try_catch.body, allocator);
    if (node->as.try_catch.handler)
      sleepy_parser_free_node(node->as.try_catch.handler, allocator);
    if (node->as.try_catch.value)
      allocator->reallocate((void *)node->as.try_catch.value, 0, NULL);
    break;
  case SLEEPY_AST_RETURN:
  case SLEEPY_AST_THROW:
  case SLEEPY_AST_ASSERT:
  case SLEEPY_AST_YIELD:
  case SLEEPY_AST_LOCAL:
  case SLEEPY_AST_THIS:
    if (node->as.control.value)
      sleepy_parser_free_node(node->as.control.value, allocator);
    break;
  case SLEEPY_AST_IMPORT:
    if (node->as.import_stmt.path)
      allocator->reallocate((void *)node->as.import_stmt.path, 0, NULL);
    break;
  case SLEEPY_AST_INDEX:
    if (node->as.index.container)
      sleepy_parser_free_node(node->as.index.container, allocator);
    if (node->as.index.element)
      sleepy_parser_free_node(node->as.index.element, allocator);
    break;
  case SLEEPY_AST_OBJ_EXPR:
    if (node->as.obj_expr.target)
      sleepy_parser_free_node(node->as.obj_expr.target, allocator);
    if (node->as.obj_expr.message)
      sleepy_parser_free_node(node->as.obj_expr.message, allocator);
    if (node->as.obj_expr.args) {
      for (size_t i = 0; i < node->as.obj_expr.arg_count; i++)
        if (node->as.obj_expr.args[i])
          sleepy_parser_free_node(node->as.obj_expr.args[i], allocator);
      allocator->reallocate(node->as.obj_expr.args, 0, NULL);
    }
    break;
  default:
    break;
  }

  // 3. Free the node itself
  allocator->reallocate(node, 0, NULL);
}

static void append_indent(SleepyStringBuffer *buffer, int depth) {
  for (int i = 0; i < depth; i++) {
    sleepy_string_buffer_append_string(buffer, "    ", 4);
  }
}

static void format_node(SleepyASTNode *node, SleepyStringBuffer *buffer,
                        int depth) {
  if (!node)
    return;

  switch (node->type) {
  case SLEEPY_AST_SCRIPT: {
    for (size_t i = 0; i < node->as.block.count; i++) {
      format_node(node->as.block.statements[i], buffer, depth);
      if (i < node->as.block.count - 1) {
        if (node->as.block.statements[i]->type != SLEEPY_AST_ENV_BRIDGE) {
          sleepy_string_buffer_append_char(buffer, ';');
        }
        sleepy_string_buffer_append_char(buffer, '\n');
      }
    }
    break;
  }

  case SLEEPY_AST_BLOCK: {
    sleepy_string_buffer_append_string(buffer, "{\n", 2);
    for (size_t i = 0; i < node->as.block.count; i++) {
      append_indent(buffer, depth + 1);
      format_node(node->as.block.statements[i], buffer, depth + 1);
      if (node->as.block.statements[i]->type != SLEEPY_AST_ENV_BRIDGE &&
          node->as.block.statements[i]->type != SLEEPY_AST_IF &&
          node->as.block.statements[i]->type != SLEEPY_AST_WHILE &&
          node->as.block.statements[i]->type != SLEEPY_AST_FOR &&
          node->as.block.statements[i]->type != SLEEPY_AST_FOREACH) {
        sleepy_string_buffer_append_char(buffer, ';');
      }
      sleepy_string_buffer_append_char(buffer, '\n');
    }
    append_indent(buffer, depth);
    sleepy_string_buffer_append_char(buffer, '}');
    break;
  }

  case SLEEPY_AST_BOOLEAN: {
    if (node->as.boolean) {
      sleepy_string_buffer_append_string(buffer, "true", 4);
    } else {
      sleepy_string_buffer_append_string(buffer, "false", 5);
    }
    break;
  }

  case SLEEPY_AST_NULL: {
    sleepy_string_buffer_append_string(buffer, "$null", 5);
    break;
  }

  case SLEEPY_AST_LONG: {
    char buf[64];
    snprintf(buf, sizeof(buf), "%ldL", node->as.long_val);
    sleepy_string_buffer_append_string(buffer, buf, sleepy_utils_strlen(buf));
    break;
  }

  case SLEEPY_AST_NUMBER: {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", node->as.double_val);
    sleepy_string_buffer_append_string(buffer, buf, sleepy_utils_strlen(buf));
    break;
  }

  case SLEEPY_AST_LITERAL:
  case SLEEPY_AST_STRING: {
    sleepy_string_buffer_append_char(buffer, '"');
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    sleepy_string_buffer_append_char(buffer, '"');
    break;
  }

  case SLEEPY_AST_SCALAR: {
    sleepy_string_buffer_append_char(buffer, '$');
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    break;
  }

  case SLEEPY_AST_IDENTIFIER:
  case SLEEPY_AST_CLASS_LITERAL: {
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    break;
  }

  case SLEEPY_AST_ARRAY: {
    if (node->as.string_val && node->as.string_val[0] != '@') {
      sleepy_string_buffer_append_char(buffer, '@');
    }
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    break;
  }

  case SLEEPY_AST_HASHTABLE: {
    if (node->as.string_val && node->as.string_val[0] != '%') {
      sleepy_string_buffer_append_char(buffer, '%');
    }
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    break;
  }

  case SLEEPY_AST_BACKTICK: {
    sleepy_string_buffer_append_char(buffer, '`');
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    sleepy_string_buffer_append_char(buffer, '`');
    break;
  }

  case SLEEPY_AST_CALL: {
    format_node(node->as.call.target, buffer, depth);
    sleepy_string_buffer_append_char(buffer, '(');
    for (size_t i = 0; i < node->as.call.arg_count; i++) {
      format_node(node->as.call.args[i], buffer, depth);
      if (i < node->as.call.arg_count - 1) {
        sleepy_string_buffer_append_string(buffer, ", ", 2);
      }
    }
    sleepy_string_buffer_append_char(buffer, ')');
    break;
  }

  case SLEEPY_AST_BINOP: {
    format_node(node->as.binop.left, buffer, depth);
    sleepy_string_buffer_append_char(buffer, ' ');
    if (node->as.binop.negate) {
      sleepy_string_buffer_append_char(buffer, '!');
    }
    if (node->as.binop.op.start) {
      sleepy_string_buffer_append_string(buffer, node->as.binop.op.start,
                                         node->as.binop.op.length);
    } else {
      sleepy_string_buffer_append_string(buffer, "/* missing op */", 16);
    }
    sleepy_string_buffer_append_char(buffer, ' ');
    format_node(node->as.binop.right, buffer, depth);
    break;
  }

  case SLEEPY_AST_ASSIGNMENT: {
    format_node(node->as.assign.left, buffer, depth);
    sleepy_string_buffer_append_char(buffer, ' ');
    if (node->as.assign.op.start) {
      sleepy_string_buffer_append_string(buffer, node->as.assign.op.start,
                                         node->as.assign.op.length);
    } else {
      sleepy_string_buffer_append_char(buffer, '=');
    }
    sleepy_string_buffer_append_char(buffer, ' ');
    format_node(node->as.assign.right, buffer, depth);
    break;
  }

  case SLEEPY_AST_UNARYOP: {
    if (!node->as.unaryop.op.start) {
      format_node(node->as.unaryop.operand, buffer, depth);
      break;
    }
    bool is_post = (node->as.unaryop.op.start[0] == '+' ||
                    node->as.unaryop.op.start[0] == '-') &&
                   (node->as.unaryop.op.length == 2);
    if (!is_post) {
      sleepy_string_buffer_append_string(buffer, node->as.unaryop.op.start,
                                         node->as.unaryop.op.length);
    }
    format_node(node->as.unaryop.operand, buffer, depth);
    if (is_post) {
      sleepy_string_buffer_append_string(buffer, node->as.unaryop.op.start,
                                         node->as.unaryop.op.length);
    }
    break;
  }

  case SLEEPY_AST_IF: {
    sleepy_string_buffer_append_string(buffer, "if (", 4);
    format_node(node->as.if_stmt.condition, buffer, depth);
    sleepy_string_buffer_append_string(buffer, ") ", 2);
    format_node(node->as.if_stmt.then_branch, buffer, depth);
    if (node->as.if_stmt.else_branch) {
      sleepy_string_buffer_append_string(buffer, " else ", 6);
      format_node(node->as.if_stmt.else_branch, buffer, depth);
    }
    break;
  }

  case SLEEPY_AST_WHILE: {
    sleepy_string_buffer_append_string(buffer, "while (", 7);
    format_node(node->as.while_stmt.condition, buffer, depth);
    sleepy_string_buffer_append_string(buffer, ") ", 2);
    format_node(node->as.while_stmt.body, buffer, depth);
    break;
  }

  case SLEEPY_AST_FOR: {
    sleepy_string_buffer_append_string(buffer, "for (", 5);
    for (size_t i = 0; i < node->as.for_stmt.init_count; i++) {
      format_node(node->as.for_stmt.initializer[i], buffer, depth);
      if (i < node->as.for_stmt.init_count - 1) {
        sleepy_string_buffer_append_string(buffer, ", ", 2);
      }
    }
    sleepy_string_buffer_append_string(buffer, "; ", 2);
    format_node(node->as.for_stmt.condition, buffer, depth);
    sleepy_string_buffer_append_string(buffer, "; ", 2);
    for (size_t i = 0; i < node->as.for_stmt.inc_count; i++) {
      format_node(node->as.for_stmt.increment[i], buffer, depth);
      if (i < node->as.for_stmt.inc_count - 1) {
        sleepy_string_buffer_append_string(buffer, ", ", 2);
      }
    }
    sleepy_string_buffer_append_string(buffer, ") ", 2);
    format_node(node->as.for_stmt.body, buffer, depth);
    break;
  }

  case SLEEPY_AST_FOREACH: {
    sleepy_string_buffer_append_string(buffer, "foreach ", 8);
    if (node->as.foreach.index) {
      sleepy_string_buffer_append_string(
          buffer, node->as.foreach.index,
          sleepy_utils_strlen(node->as.foreach.index));
      sleepy_string_buffer_append_string(buffer, " => ", 4);
    }
    if (node->as.foreach.value) {
      sleepy_string_buffer_append_string(
          buffer, node->as.foreach.value,
          sleepy_utils_strlen(node->as.foreach.value));
    } else {
      sleepy_string_buffer_append_string(buffer, "$_", 2);
    }
    sleepy_string_buffer_append_string(buffer, " (", 2);
    format_node(node->as.foreach.generator, buffer, depth);
    sleepy_string_buffer_append_string(buffer, ") ", 2);
    format_node(node->as.foreach.body, buffer, depth);
    break;
  }

  case SLEEPY_AST_RETURN: {
    sleepy_string_buffer_append_string(buffer, "return", 6);
    if (node->as.control.value) {
      sleepy_string_buffer_append_char(buffer, ' ');
      format_node(node->as.control.value, buffer, depth);
    }
    break;
  }

  case SLEEPY_AST_BREAK: {
    sleepy_string_buffer_append_string(buffer, "break", 5);
    break;
  }

  case SLEEPY_AST_CONTINUE: {
    sleepy_string_buffer_append_string(buffer, "continue", 8);
    break;
  }

  case SLEEPY_AST_YIELD: {
    sleepy_string_buffer_append_string(buffer, "yield", 5);
    if (node->as.control.value) {
      sleepy_string_buffer_append_char(buffer, ' ');
      format_node(node->as.control.value, buffer, depth);
    }
    break;
  }

  case SLEEPY_AST_INDEX: {
    format_node(node->as.index.container, buffer, depth);
    sleepy_string_buffer_append_char(buffer, '[');
    format_node(node->as.index.element, buffer, depth);
    sleepy_string_buffer_append_char(buffer, ']');
    break;
  }

  case SLEEPY_AST_OBJ_EXPR: {
    sleepy_string_buffer_append_char(buffer, '[');
    format_node(node->as.obj_expr.target, buffer, depth);
    if (node->as.obj_expr.message) {
      sleepy_string_buffer_append_char(buffer, ' ');
      format_node(node->as.obj_expr.message, buffer, depth);
    }
    if (node->as.obj_expr.arg_count > 0) {
      sleepy_string_buffer_append_string(buffer, ": ", 2);
      for (size_t i = 0; i < node->as.obj_expr.arg_count; i++) {
        format_node(node->as.obj_expr.args[i], buffer, depth);
        if (i < node->as.obj_expr.arg_count - 1) {
          sleepy_string_buffer_append_string(buffer, ", ", 2);
        }
      }
    }
    sleepy_string_buffer_append_char(buffer, ']');
    break;
  }

  case SLEEPY_AST_TRY_CATCH: {
    sleepy_string_buffer_append_string(buffer, "try ", 4);
    format_node(node->as.try_catch.body, buffer, depth);
    sleepy_string_buffer_append_string(buffer, " catch ", 7);
    if (node->as.try_catch.value) {
      sleepy_string_buffer_append_string(
          buffer, node->as.try_catch.value,
          sleepy_utils_strlen(node->as.try_catch.value));
    }
    sleepy_string_buffer_append_char(buffer, ' ');
    format_node(node->as.try_catch.handler, buffer, depth);
    break;
  }

  case SLEEPY_AST_THROW: {
    sleepy_string_buffer_append_string(buffer, "throw ", 6);
    format_node(node->as.control.value, buffer, depth);
    break;
  }

  case SLEEPY_AST_ASSERT: {
    sleepy_string_buffer_append_string(buffer, "assert ", 7);
    format_node(node->as.control.value, buffer, depth);
    break;
  }

  case SLEEPY_AST_ENV_BRIDGE: {
    if (node->as.env_bridge.keyword) {
      sleepy_string_buffer_append_string(
          buffer, node->as.env_bridge.keyword,
          sleepy_utils_strlen(node->as.env_bridge.keyword));
    } else {
      sleepy_string_buffer_append_string(buffer, "/* missing keyword */", 21);
    }

    if (node->as.env_bridge.identifier) {
      sleepy_string_buffer_append_char(buffer, ' ');
      sleepy_string_buffer_append_string(
          buffer, node->as.env_bridge.identifier,
          sleepy_utils_strlen(node->as.env_bridge.identifier));
    }
    if (node->as.env_bridge.string) {
      sleepy_string_buffer_append_char(buffer, ' ');
      sleepy_string_buffer_append_string(
          buffer, node->as.env_bridge.string,
          sleepy_utils_strlen(node->as.env_bridge.string));
    }
    if (node->as.env_bridge.body) {
      sleepy_string_buffer_append_char(buffer, ' ');
      format_node(node->as.env_bridge.body, buffer, depth);
    }
    break;
  }

  case SLEEPY_AST_IMPORT: {
    sleepy_string_buffer_append_string(buffer, "import ", 7);
    if (node->as.import_stmt.path) {
      sleepy_string_buffer_append_string(
          buffer, node->as.import_stmt.path,
          sleepy_utils_strlen(node->as.import_stmt.path));
    }
    break;
  }

  case SLEEPY_AST_KV_PAIR:
  case SLEEPY_AST_ARG: {
    if (node->type == SLEEPY_AST_KV_PAIR && node->as.arg.name) {
      format_node(node->as.arg.name, buffer, depth);
      sleepy_string_buffer_append_string(buffer, " => ", 4);
    }
    format_node(node->as.arg.value, buffer, depth);
    break;
  }

  case SLEEPY_AST_DONE: {
    sleepy_string_buffer_append_string(buffer, "done", 4);
    break;
  }

  case SLEEPY_AST_HALT: {
    sleepy_string_buffer_append_string(buffer, "halt", 4);
    break;
  }

  case SLEEPY_AST_LOCAL: {
    sleepy_string_buffer_append_string(buffer, "local ", 6);
    format_node(node->as.control.value, buffer, depth);
    break;
  }

  case SLEEPY_AST_THIS: {
    sleepy_string_buffer_append_string(buffer, "this ", 5);
    format_node(node->as.control.value, buffer, depth);
    break;
  }

  case SLEEPY_AST_NOP: {
    break;
  }
  case SLEEPY_AST_ADDRESS: {
    sleepy_string_buffer_append_char(buffer, '&');
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    break;
  }

  default:
    sleepy_string_buffer_append_string(buffer, "/* unknown node type */", 23);
    break;
  }
}

void sleepy_ast_set_index(SleepyASTNode *node, SleepyASTNode *container, SleepyASTNode *element) {
    if (!node) return;
    node->as.index.container = container;
    node->as.index.element = element;
}

void sleepy_ast_set_kv_pair(SleepyASTNode *node, SleepyASTNode *name, SleepyASTNode *value) {
    if (!node) return;
    node->as.arg.name = name;
    node->as.arg.value = value;
    node->as.arg.trailing_sep = false;
}

/* --- File: ../src/sleepy_value.c --- */

bool sleepy_value_is_falsy(SleepyValue v) {
    if (SLEEPY_IS_NULL(v)) return true;
    if (SLEEPY_IS_BOOL(v) && !SLEEPY_AS_BOOL(v)) return true;
    if (SLEEPY_IS_NUM(v) && SLEEPY_AS_NUM(v) == 0.0) return true;
    return false;
}

bool sleepy_value_equals(SleepyValue a, SleepyValue b) {
#ifdef SLEEPY_NAN_TAGGING
    if (a == b) return true;
    if (SLEEPY_IS_NUM(a) && SLEEPY_IS_NUM(b))
        return SLEEPY_AS_NUM(a) == SLEEPY_AS_NUM(b);
    return false;
#else
    if (a.type != b.type) return false;
    switch (a.type) {
    case SLEEPY_VAL_NULL: return true;
    case SLEEPY_VAL_BOOL: return a.as.boolean == b.as.boolean;
    case SLEEPY_VAL_NUM: return a.as.num == b.as.num;
    case SLEEPY_VAL_OBJ: return a.as.obj == b.as.obj;
    }
    return false;
#endif
}

uint32_t sleepy_hash_string(const char *key, uint32_t length) {
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

uint32_t sleepy_hash_value(SleepyValue v) {
    if (SLEEPY_IS_NUM(v)) {
        double d = SLEEPY_AS_NUM(v);
        uint32_t h;
        sleepy_utils_memcpy(&h, &d, 4);
        return h;
    }
    if (SLEEPY_IS_OBJ(v)) {
        SleepyObj *obj = SLEEPY_AS_OBJ(v);
        if (obj->type == SLEEPY_OBJ_STRING) {
            return ((SleepyObjString*)obj)->hash;
        }
        uint32_t ptr;
        sleepy_utils_memcpy(&ptr, &obj, 4);
        return ptr;
    }
#ifdef SLEEPY_NAN_TAGGING
    return (uint32_t)(v & 0xFFFFFFFF);
#else
    return (uint32_t)v.type;
#endif
}

SleepyObjString *sleepy_find_interned_string(SleepyObj *head, const char *chars, uint32_t length, uint32_t hash) {
    SleepyObj *obj = head;
    while (obj != NULL) {
        if (obj->type == SLEEPY_OBJ_STRING) {
            SleepyObjString *s = (SleepyObjString*)obj;
            if (s->hash == hash && s->length == length) {
                bool match = true;
                for (uint32_t i = 0; i < length; i++) {
                    if (s->chars[i] != chars[i]) {
                        match = false;
                        break;
                    }
                }
                if (match) return s;
            }
        }
        obj = obj->next;
    }
    return NULL;
}

SleepyObjString *sleepy_obj_string_new(SleepyAllocator *alloc, const char *chars, uint32_t length) {
    size_t size = sizeof(SleepyObjString) + length + 1;
    SleepyObjString *str = (SleepyObjString*)SLEEPY_MALLOC(alloc, size);
    if (!str) return NULL;
    str->obj.type = SLEEPY_OBJ_STRING;
    str->obj.is_marked = false;
    str->obj.next = NULL;
    str->length = length;
    str->hash = sleepy_hash_string(chars, length);
    sleepy_utils_memcpy(str->chars, chars, length);
    str->chars[length] = '\0';
    return str;
}

SleepyObjString *sleepy_obj_string_copy(SleepyAllocator *alloc, const char *chars, uint32_t length) {
    return sleepy_obj_string_new(alloc, chars, length);
}

SleepyObjLong *sleepy_obj_long_new(SleepyAllocator *alloc, int64_t value) {
    SleepyObjLong *obj = SLEEPY_ALLOCATE(alloc, SleepyObjLong);
    if (!obj) return NULL;
    obj->obj.type = SLEEPY_OBJ_LONG;
    obj->obj.is_marked = false;
    obj->obj.next = NULL;
    obj->value = value;
    return obj;
}

SleepyObjArray *sleepy_obj_array_new(SleepyAllocator *alloc) {
    SleepyObjArray *arr = SLEEPY_ALLOCATE(alloc, SleepyObjArray);
    if (!arr) return NULL;
    arr->obj.type = SLEEPY_OBJ_ARRAY;
    arr->obj.is_marked = false;
    arr->obj.next = NULL;
    arr->elements = NULL;
    arr->count = 0;
    arr->capacity = 0;
    return arr;
}

void sleepy_obj_array_push(SleepyAllocator *alloc, SleepyObjArray *arr, SleepyValue val) {
    if (arr->count + 1 > arr->capacity) {
        int new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        arr->elements = (SleepyValue*)SLEEPY_REALLOC(alloc, arr->elements, sizeof(SleepyValue) * new_cap);
        arr->capacity = new_cap;
    }
    arr->elements[arr->count++] = val;
}

SleepyValue sleepy_obj_array_pop(SleepyObjArray *arr) {
    if (arr->count == 0) return SLEEPY_NULL_VAL;
    return arr->elements[--arr->count];
}

SleepyValue sleepy_obj_array_get(SleepyObjArray *arr, int index) {
    if (index < 0 || index >= arr->count) return SLEEPY_NULL_VAL;
    return arr->elements[index];
}

void sleepy_obj_array_set(SleepyAllocator *alloc, SleepyObjArray *arr, int index, SleepyValue val) {
    if (index < 0) return;
    while (index >= arr->capacity) {
        int new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
        arr->elements = (SleepyValue*)SLEEPY_REALLOC(alloc, arr->elements, sizeof(SleepyValue) * new_cap);
        for (int i = arr->capacity; i < new_cap; i++)
            arr->elements[i] = SLEEPY_NULL_VAL;
        arr->capacity = new_cap;
    }
    arr->elements[index] = val;
    if (index >= arr->count) arr->count = index + 1;
}

static void hash_grow(SleepyAllocator *alloc, SleepyObjHash *hash) {
    int new_cap = hash->capacity == 0 ? 8 : hash->capacity * 2;
    SleepyHashEntry *new_entries = (SleepyHashEntry*)SLEEPY_MALLOC(alloc, sizeof(SleepyHashEntry) * new_cap);
    for (int i = 0; i < new_cap; i++) {
        new_entries[i].key = SLEEPY_NULL_VAL;
        new_entries[i].value = SLEEPY_NULL_VAL;
    }
    for (int i = 0; i < hash->capacity; i++) {
        if (SLEEPY_IS_NULL(hash->entries[i].key)) continue;
        uint32_t idx = sleepy_hash_value(hash->entries[i].key) & (new_cap - 1);
        while (!SLEEPY_IS_NULL(new_entries[idx].key)) {
            idx = (idx + 1) & (new_cap - 1);
        }
        new_entries[idx] = hash->entries[i];
    }
    if (hash->entries) SLEEPY_FREE(alloc, hash->entries);
    hash->entries = new_entries;
    hash->capacity = new_cap;
}

SleepyObjHash *sleepy_obj_hash_new(SleepyAllocator *alloc) {
    SleepyObjHash *h = SLEEPY_ALLOCATE(alloc, SleepyObjHash);
    if (!h) return NULL;
    h->obj.type = SLEEPY_OBJ_HASH;
    h->obj.is_marked = false;
    h->obj.next = NULL;
    h->entries = NULL;
    h->capacity = 0;
    h->count = 0;
    return h;
}

bool sleepy_obj_hash_set(SleepyAllocator *alloc, SleepyObjHash *hash, SleepyValue key, SleepyValue value) {
    if (hash->count + 1 > hash->capacity * 3 / 4)
        hash_grow(alloc, hash);
    if (hash->capacity == 0)
        hash_grow(alloc, hash);
    uint32_t idx = sleepy_hash_value(key) & (hash->capacity - 1);
    int tombstone = -1;
    for (;;) {
        if (SLEEPY_IS_NULL(hash->entries[idx].key)) {
            if (SLEEPY_IS_TRUE(hash->entries[idx].value)) {
                if (tombstone == -1) tombstone = idx;
            } else {
                if (tombstone != -1) idx = tombstone;
                hash->entries[idx].key = key;
                hash->entries[idx].value = value;
                hash->count++;
                return true;
            }
        } else if (sleepy_value_equals(hash->entries[idx].key, key)) {
            hash->entries[idx].value = value;
            return true;
        }
        idx = (idx + 1) & (hash->capacity - 1);
    }
}

SleepyValue sleepy_obj_hash_get(SleepyObjHash *hash, SleepyValue key) {
    if (hash->capacity == 0) return SLEEPY_NULL_VAL;
    uint32_t idx = sleepy_hash_value(key) & (hash->capacity - 1);
    for (int i = 0; i < hash->capacity; i++) {
        if (SLEEPY_IS_NULL(hash->entries[idx].key)) {
            if (SLEEPY_IS_TRUE(hash->entries[idx].value)) {
                // Tombstone
                idx = (idx + 1) & (hash->capacity - 1);
                continue;
            } else {
                return SLEEPY_NULL_VAL;
            }
        }
        if (sleepy_value_equals(hash->entries[idx].key, key))
            return hash->entries[idx].value;
        idx = (idx + 1) & (hash->capacity - 1);
    }
    return SLEEPY_NULL_VAL;
}

bool sleepy_obj_hash_delete(SleepyAllocator *alloc, SleepyObjHash *hash, SleepyValue key) {
    if (hash->capacity == 0) return false;
    uint32_t idx = sleepy_hash_value(key) & (hash->capacity - 1);
    for (int i = 0; i < hash->capacity; i++) {
        if (SLEEPY_IS_NULL(hash->entries[idx].key))
            return false;
        if (sleepy_value_equals(hash->entries[idx].key, key)) {
            hash->entries[idx].key = SLEEPY_NULL_VAL;
            hash->entries[idx].value = SLEEPY_TRUE_VAL;
            hash->count--;
            return true;
        }
        idx = (idx + 1) & (hash->capacity - 1);
    }
    return false;
    (void)alloc;
}

SleepyObjFunction *sleepy_obj_function_new(SleepyAllocator *alloc) {
    SleepyObjFunction *fn = SLEEPY_ALLOCATE(alloc, SleepyObjFunction);
    if (!fn) return NULL;
    fn->obj.type = SLEEPY_OBJ_FUNCTION;
    fn->obj.is_marked = false;
    fn->obj.next = NULL;
    fn->arity = 0;
    fn->upvalue_count = 0;
    fn->chunk = SLEEPY_ALLOCATE(alloc, SleepyChunk);
    if (fn->chunk) sleepy_chunk_init(fn->chunk, alloc);
    fn->name = NULL;
    return fn;
}

SleepyObjClosure *sleepy_obj_closure_new(SleepyAllocator *alloc, SleepyObjFunction *fn) {
    SleepyObjClosure *closure = SLEEPY_ALLOCATE(alloc, SleepyObjClosure);
    if (!closure) return NULL;
    closure->obj.type = SLEEPY_OBJ_CLOSURE;
    closure->obj.is_marked = false;
    closure->obj.next = NULL;
    closure->function = fn;
    closure->upvalues = NULL;
    if (fn->upvalue_count > 0) {
        closure->upvalues = (SleepyObjUpvalue**)SLEEPY_MALLOC(alloc, sizeof(SleepyObjUpvalue*) * fn->upvalue_count);
        for (int i = 0; i < fn->upvalue_count; i++)
            closure->upvalues[i] = NULL;
    }
    return closure;
}

SleepyObjUpvalue *sleepy_obj_upvalue_new(SleepyAllocator *alloc, SleepyValue *slot) {
    SleepyObjUpvalue *uv = SLEEPY_ALLOCATE(alloc, SleepyObjUpvalue);
    if (!uv) return NULL;
    uv->obj.type = SLEEPY_OBJ_UPVALUE;
    uv->obj.is_marked = false;
    uv->obj.next = NULL;
    uv->location = slot;
    uv->closed = SLEEPY_NULL_VAL;
    return uv;
}

SleepyObjNative *sleepy_obj_native_new(SleepyAllocator *alloc, SleepyNativeFn fn, SleepyObjString *name) {
    SleepyObjNative *native = SLEEPY_ALLOCATE(alloc, SleepyObjNative);
    if (!native) return NULL;
    native->obj.type = SLEEPY_OBJ_NATIVE;
    native->obj.is_marked = false;
    native->obj.next = NULL;
    native->fn = fn;
    native->name = name;
    return native;
}

SleepyObjBridge *sleepy_obj_bridge_new(SleepyAllocator *alloc, SleepyObjString *keyword, SleepyObjString *name, SleepyObjClosure *closure) {
    SleepyObjBridge *bridge = SLEEPY_ALLOCATE(alloc, SleepyObjBridge);
    if (!bridge) return NULL;
    bridge->obj.type = SLEEPY_OBJ_BRIDGE;
    bridge->obj.is_marked = false;
    bridge->obj.next = NULL;
    bridge->keyword = keyword;
    bridge->name = name;
    bridge->closure = closure;
    return bridge;
}

SleepyObjContinuation *sleepy_obj_continuation_new(SleepyAllocator *alloc) {
    SleepyObjContinuation *cont = SLEEPY_ALLOCATE(alloc, SleepyObjContinuation);
    if (!cont) return NULL;
    cont->obj.type = SLEEPY_OBJ_CONTINUATION;
    cont->obj.is_marked = false;
    cont->obj.next = NULL;
    cont->frames = NULL;
    cont->frame_count = 0;
    cont->stack = NULL;
    cont->stack_count = 0;
    cont->saved_ip = NULL;
    return cont;
}

/* --- File: ../src/sleepy_chunk.c --- */

void sleepy_chunk_init(SleepyChunk *chunk, SleepyAllocator *allocator) {
    chunk->allocator = allocator;
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->constants = NULL;
    chunk->constant_count = 0;
    chunk->constant_capacity = 0;
}

void sleepy_chunk_free(SleepyChunk *chunk) {
    if (chunk->code) SLEEPY_FREE(chunk->allocator, chunk->code);
    if (chunk->lines) SLEEPY_FREE(chunk->allocator, chunk->lines);
    if (chunk->constants) SLEEPY_FREE(chunk->allocator, chunk->constants);
    sleepy_chunk_init(chunk, chunk->allocator);
}

int sleepy_chunk_write(SleepyChunk *chunk, uint8_t byte, int line) {
    if (chunk->count + 1 > chunk->capacity) {
        int new_cap = chunk->capacity == 0 ? 64 : chunk->capacity * 2;
        chunk->code = (uint8_t*)SLEEPY_REALLOC(chunk->allocator, chunk->code, new_cap);
        chunk->lines = (int*)SLEEPY_REALLOC(chunk->allocator, chunk->lines, sizeof(int) * new_cap);
        chunk->capacity = new_cap;
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    return chunk->count++;
}

int sleepy_chunk_add_constant(SleepyChunk *chunk, SleepyValue value) {
    for (int i = 0; i < chunk->constant_count; i++) {
        if (sleepy_value_equals(chunk->constants[i], value))
            return i;
    }
    if (chunk->constant_count + 1 > chunk->constant_capacity) {
        int new_cap = chunk->constant_capacity == 0 ? 16 : chunk->constant_capacity * 2;
        chunk->constants = (SleepyValue*)SLEEPY_REALLOC(chunk->allocator, chunk->constants, sizeof(SleepyValue) * new_cap);
        chunk->constant_capacity = new_cap;
    }
    chunk->constants[chunk->constant_count] = value;
    return chunk->constant_count++;
}

uint16_t sleepy_chunk_read_short(SleepyChunk *chunk, int offset) {
    return (uint16_t)((chunk->code[offset] << 8) | chunk->code[offset + 1]);
}

void sleepy_chunk_write_short(SleepyChunk *chunk, uint16_t val, int line) {
    sleepy_chunk_write(chunk, (uint8_t)((val >> 8) & 0xFF), line);
    sleepy_chunk_write(chunk, (uint8_t)(val & 0xFF), line);
}

/* --- File: ../src/sleepy_gc.c --- */

void sleepy_gc_init(SleepyVM *vm) {
    vm->bytes_allocated = 0;
    vm->next_gc_threshold = 1024 * 1024;
    vm->gc_gray_stack = NULL;
    vm->gc_gray_count = 0;
    vm->gc_gray_capacity = 0;
}

static void gc_gray_stack_push(SleepyVM *vm, SleepyObj *obj) {
    if (vm->gc_gray_count + 1 > vm->gc_gray_capacity) {
        int new_cap = vm->gc_gray_capacity == 0 ? 64 : vm->gc_gray_capacity * 2;
        vm->gc_gray_stack = (SleepyObj**)SLEEPY_REALLOC(vm->allocator, vm->gc_gray_stack, sizeof(SleepyObj*) * new_cap);
        vm->gc_gray_capacity = new_cap;
    }
    vm->gc_gray_stack[vm->gc_gray_count++] = obj;
}

static void blacken_object(SleepyVM *vm, SleepyObj *obj) {
    switch (obj->type) {
    case SLEEPY_OBJ_STRING:
        break;
    case SLEEPY_OBJ_LONG:
        break;
    case SLEEPY_OBJ_ARRAY: {
        SleepyObjArray *arr = (SleepyObjArray*)obj;
        for (int i = 0; i < arr->count; i++)
            sleepy_gc_mark_value(vm, arr->elements[i]);
        break;
    }
    case SLEEPY_OBJ_HASH: {
        SleepyObjHash *hash = (SleepyObjHash*)obj;
        for (int i = 0; i < hash->capacity; i++) {
            if (!SLEEPY_IS_NULL(hash->entries[i].key)) {
                sleepy_gc_mark_value(vm, hash->entries[i].key);
                sleepy_gc_mark_value(vm, hash->entries[i].value);
            }
        }
        break;
    }
    case SLEEPY_OBJ_FUNCTION: {
        SleepyObjFunction *fn = (SleepyObjFunction*)obj;
        if (fn->name) sleepy_gc_mark_obj(vm, (SleepyObj*)fn->name);
        if (fn->chunk) {
            for (int i = 0; i < fn->chunk->constant_count; i++)
                sleepy_gc_mark_value(vm, fn->chunk->constants[i]);
        }
        break;
    }
    case SLEEPY_OBJ_CLOSURE: {
        SleepyObjClosure *closure = (SleepyObjClosure*)obj;
        sleepy_gc_mark_obj(vm, (SleepyObj*)closure->function);
        for (int i = 0; i < closure->function->upvalue_count; i++)
            if (closure->upvalues[i])
                sleepy_gc_mark_obj(vm, (SleepyObj*)closure->upvalues[i]);
        break;
    }
    case SLEEPY_OBJ_UPVALUE:
        sleepy_gc_mark_value(vm, ((SleepyObjUpvalue*)obj)->closed);
        break;
    case SLEEPY_OBJ_CONTINUATION: {
        SleepyObjContinuation *cont = (SleepyObjContinuation*)obj;
        if (cont->stack) {
            for (int i = 0; i < cont->stack_count; i++)
                sleepy_gc_mark_value(vm, cont->stack[i]);
        }
        break;
    }
    case SLEEPY_OBJ_NATIVE: {
        SleepyObjNative *native = (SleepyObjNative*)obj;
        if (native->name) sleepy_gc_mark_obj(vm, (SleepyObj*)native->name);
        break;
    }
    case SLEEPY_OBJ_BRIDGE: {
        SleepyObjBridge *bridge = (SleepyObjBridge*)obj;
        if (bridge->keyword) sleepy_gc_mark_obj(vm, (SleepyObj*)bridge->keyword);
        if (bridge->name) sleepy_gc_mark_obj(vm, (SleepyObj*)bridge->name);
        if (bridge->closure) sleepy_gc_mark_obj(vm, (SleepyObj*)bridge->closure);
        break;
    }
    }
}

static void mark_roots(SleepyVM *vm) {
    for (SleepyValue *slot = vm->stack; slot < vm->stack_top; slot++)
        sleepy_gc_mark_value(vm, *slot);

    for (int i = 0; i < vm->frame_count; i++) {
        sleepy_gc_mark_obj(vm, (SleepyObj*)vm->frames[i].closure);
    }

    SleepyObjUpvalue *uv = vm->open_upvalues;
    while (uv) {
        sleepy_gc_mark_obj(vm, (SleepyObj*)uv);
        uv = uv->next;
    }

    if (vm->globals)
        sleepy_gc_mark_obj(vm, (SleepyObj*)vm->globals);
    if (vm->natives)
        sleepy_gc_mark_obj(vm, (SleepyObj*)vm->natives);

    SleepyBridgeType *bt = vm->bridge_types;
    while (bt) {
        bt = bt->next;
    }
}

static void trace_references(SleepyVM *vm) {
    while (vm->gc_gray_count > 0) {
        SleepyObj *obj = vm->gc_gray_stack[--vm->gc_gray_count];
        blacken_object(vm, obj);
    }
}

void sleepy_gc_sweep(SleepyVM *vm) {
    SleepyObj **obj_ptr = &vm->objects;
    while (*obj_ptr) {
        if ((*obj_ptr)->is_marked) {
            (*obj_ptr)->is_marked = false;
            obj_ptr = &(*obj_ptr)->next;
        } else {
            SleepyObj *unreached = *obj_ptr;
            *obj_ptr = unreached->next;
            size_t size = sleepy_gc_object_size(unreached);
            vm->bytes_allocated -= size;
            SLEEPY_FREE(vm->allocator, unreached);
        }
    }
}

size_t sleepy_gc_object_size(SleepyObj *obj) {
    switch (obj->type) {
    case SLEEPY_OBJ_STRING: {
        SleepyObjString *s = (SleepyObjString*)obj;
        return sizeof(SleepyObjString) + s->length + 1;
    }
    case SLEEPY_OBJ_LONG: return sizeof(SleepyObjLong);
    case SLEEPY_OBJ_ARRAY: return sizeof(SleepyObjArray);
    case SLEEPY_OBJ_HASH: return sizeof(SleepyObjHash);
    case SLEEPY_OBJ_FUNCTION: return sizeof(SleepyObjFunction);
    case SLEEPY_OBJ_CLOSURE: return sizeof(SleepyObjClosure);
    case SLEEPY_OBJ_UPVALUE: return sizeof(SleepyObjUpvalue);
    case SLEEPY_OBJ_CONTINUATION: return sizeof(SleepyObjContinuation);
    case SLEEPY_OBJ_NATIVE: return sizeof(SleepyObjNative);
    case SLEEPY_OBJ_BRIDGE: return sizeof(SleepyObjBridge);
    }
    return sizeof(SleepyObj);
}

void sleepy_gc_collect(SleepyVM *vm) {
    mark_roots(vm);
    trace_references(vm);
    sleepy_gc_sweep(vm);
    vm->next_gc_threshold = vm->bytes_allocated * 3 / 2;
    if (vm->next_gc_threshold < 256 * 1024)
        vm->next_gc_threshold = 256 * 1024;
}

void sleepy_gc_mark_obj(SleepyVM *vm, SleepyObj *obj) {
    if (!obj || obj->is_marked) return;
    obj->is_marked = true;
    gc_gray_stack_push(vm, obj);
}

void sleepy_gc_mark_value(SleepyVM *vm, SleepyValue value) {
    if (SLEEPY_IS_OBJ(value))
        sleepy_gc_mark_obj(vm, SLEEPY_AS_OBJ(value));
}

SleepyObj *sleepy_gc_allocate_object(SleepyVM *vm, size_t size, SleepyObjType type) {
    (void)type;
    vm->bytes_allocated += size;
    if (vm->bytes_allocated > vm->next_gc_threshold)
        sleepy_gc_collect(vm);
    return NULL;
}

void sleepy_gc_free(SleepyVM *vm) {
    SleepyObj *obj = vm->objects;
    while (obj) {
        SleepyObj *next = obj->next;
        SLEEPY_FREE(vm->allocator, obj);
        obj = next;
    }
    if (vm->gc_gray_stack)
        SLEEPY_FREE(vm->allocator, vm->gc_gray_stack);
    vm->objects = NULL;
    vm->gc_gray_stack = NULL;
    vm->gc_gray_count = 0;
    vm->gc_gray_capacity = 0;
}

/* --- File: ../src/sleepy_vm.c --- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *vm_default_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}

SleepyVM *sleepy_vm_new(SleepyAllocator *allocator) {
    SleepyVM *vm;
    if (allocator) {
        vm = (SleepyVM*)allocator->reallocate(NULL, sizeof(SleepyVM), allocator->user_data);
    } else {
        SleepyAllocator *a = (SleepyAllocator*)malloc(sizeof(SleepyAllocator));
        a->reallocate = vm_default_alloc;
        a->user_data = NULL;
        vm = (SleepyVM*)a->reallocate(NULL, sizeof(SleepyVM), a->user_data);
        allocator = a;
    }
    if (!vm) return NULL;
    memset(vm, 0, sizeof(SleepyVM));
    vm->allocator = allocator;
    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->objects = NULL;

    vm->open_upvalues = NULL;
    vm->bridge_types = NULL;
    vm->halted = false;
    vm->thrown_exception = SLEEPY_NULL_VAL;
    vm->error_fn = NULL;
    vm->error_userdata = NULL;
    vm->write_fn = NULL;
    vm->write_userdata = NULL;

    sleepy_gc_init(vm);

    vm->globals = sleepy_obj_hash_new(allocator);
    if (vm->globals) {
        vm->globals->obj.next = vm->objects;
        vm->objects = &vm->globals->obj;
    }
    vm->natives = sleepy_obj_hash_new(allocator);
    if (vm->natives) {
        vm->natives->obj.next = vm->objects;
        vm->objects = &vm->natives->obj;
    }

    return vm;
}

void sleepy_vm_free(SleepyVM *vm) {
    SleepyBridgeType *bt = vm->bridge_types;
    while (bt) {
        SleepyBridgeType *next = bt->next;
        SLEEPY_FREE(vm->allocator, bt->keyword);
        SLEEPY_FREE(vm->allocator, bt);
        bt = next;
    }
    if (vm->interned)
        vm->allocator->reallocate(vm->interned, 0, vm->allocator->user_data);
    sleepy_gc_free(vm);
    SLEEPY_FREE(vm->allocator, vm);
}

void sleepy_vm_push(SleepyVM *vm, SleepyValue value) {
    *vm->stack_top = value;
    vm->stack_top++;
}

SleepyValue sleepy_vm_pop(SleepyVM *vm) {
    vm->stack_top--;
    return *vm->stack_top;
}

SleepyValue sleepy_vm_peek(SleepyVM *vm, int distance) {
    return vm->stack_top[-1 - distance];
}

static void intern_grow(SleepyVM *vm) {
    int new_cap = vm->interned_capacity * 2;
    SleepyObjString **new_table = (SleepyObjString**)vm->allocator->reallocate(
        NULL, sizeof(SleepyObjString*) * new_cap, vm->allocator->user_data);
    if (!new_table) return;
    memset(new_table, 0, sizeof(SleepyObjString*) * new_cap);
    for (int i = 0; i < vm->interned_capacity; i++) {
        SleepyObjString *s = vm->interned[i];
        if (s) {
            uint32_t idx = s->hash & (new_cap - 1);
            while (new_table[idx]) {
                idx = (idx + 1) & (new_cap - 1);
            }
            new_table[idx] = s;
        }
    }
    vm->allocator->reallocate(vm->interned, 0, vm->allocator->user_data);
    vm->interned = new_table;
    vm->interned_capacity = new_cap;
}

SleepyObjString *sleepy_vm_intern_string(SleepyVM *vm, const char *chars, uint32_t length) {
    uint32_t hash = sleepy_hash_string(chars, length);
    if (vm->interned) {
        uint32_t idx = hash & (vm->interned_capacity - 1);
        for (int probes = 0; probes < vm->interned_capacity; probes++) {
            SleepyObjString *s = vm->interned[idx];
            if (!s) break;
            if (s->hash == hash && s->length == length) {
                bool match = true;
                for (uint32_t i = 0; i < length; i++) {
                    if (s->chars[i] != chars[i]) { match = false; break; }
                }
                if (match) return s;
            }
            idx = (idx + 1) & (vm->interned_capacity - 1);
        }
    }
    if (vm->interned_count * 2 >= vm->interned_capacity) {
        if (!vm->interned) {
            vm->interned_capacity = SLEEPY_INTERN_INIT_CAP;
            vm->interned = (SleepyObjString**)vm->allocator->reallocate(
                NULL, sizeof(SleepyObjString*) * vm->interned_capacity,
                vm->allocator->user_data);
            if (vm->interned) memset(vm->interned, 0, sizeof(SleepyObjString*) * vm->interned_capacity);
        } else {
            intern_grow(vm);
        }
    }
    SleepyObjString *str = sleepy_obj_string_new(vm->allocator, chars, length);
    if (str) {
        str->hash = hash;
        str->obj.next = vm->objects;
        vm->objects = &str->obj;
        vm->bytes_allocated += sizeof(SleepyObjString) + length + 1;
        if (vm->interned) {
            uint32_t idx = hash & (vm->interned_capacity - 1);
            while (vm->interned[idx]) {
                idx = (idx + 1) & (vm->interned_capacity - 1);
            }
            vm->interned[idx] = str;
            vm->interned_count++;
        }
    }
    return str;
}

SleepyObjString *sleepy_vm_copy_string(SleepyVM *vm, const char *chars, uint32_t length) {
    return sleepy_vm_intern_string(vm, chars, length);
}

static SleepyCallFrame *current_frame(SleepyVM *vm) {
    return &vm->frames[vm->frame_count - 1];
}

static uint8_t read_byte(SleepyCallFrame *frame) {
    return *frame->ip++;
}

static uint16_t read_short(SleepyCallFrame *frame) {
    frame->ip += 2;
    return (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]);
}

static SleepyValue read_constant(SleepyCallFrame *frame) {
    uint16_t idx = read_short(frame);
    return frame->closure->function->chunk->constants[idx];
}

static void close_upvalues(SleepyVM *vm, SleepyValue *last) {
    while (vm->open_upvalues && vm->open_upvalues->location >= last) {
        SleepyObjUpvalue *uv = vm->open_upvalues;
        uv->closed = *uv->location;
        uv->location = &uv->closed;
        vm->open_upvalues = uv->next;
    }
}

static void define_native(SleepyVM *vm, const char *name, SleepyNativeFn fn) {
    SleepyObjString *name_str = sleepy_vm_copy_string(vm, name, (uint32_t)strlen(name));
    SleepyObjNative *native = sleepy_obj_native_new(vm->allocator, fn, name_str);
    if (native) {
        native->obj.next = vm->objects;
        vm->objects = &native->obj;
    }
    sleepy_obj_hash_set(vm->allocator, vm->globals, SLEEPY_OBJ_VAL(name_str), SLEEPY_OBJ_VAL(native));
}

static SleepyValue native_println(SleepyVM *vm, SleepyValue *args, int argc) {
    (void)vm;
    if (argc > 0) {
        if (SLEEPY_IS_NUM(args[0]))
            printf("%g\n", SLEEPY_AS_NUM(args[0]));
        else if (SLEEPY_IS_BOOL(args[0]))
            printf("%s\n", SLEEPY_AS_BOOL(args[0]) ? "true" : "false");
        else if (SLEEPY_IS_NULL(args[0]))
            printf("$null\n");
        else if (SLEEPY_IS_OBJ(args[0])) {
            SleepyObj *obj = SLEEPY_AS_OBJ(args[0]);
            if (obj->type == SLEEPY_OBJ_STRING)
                printf("%s\n", ((SleepyObjString*)obj)->chars);
            else
                printf("[object]\n");
        }
    } else {
        printf("\n");
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_size(SleepyVM *vm, SleepyValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLEEPY_NUM_VAL(0);
    if (SLEEPY_IS_OBJ(args[0])) {
        SleepyObj *obj = SLEEPY_AS_OBJ(args[0]);
        if (obj->type == SLEEPY_OBJ_STRING)
            return SLEEPY_NUM_VAL((double)((SleepyObjString*)obj)->length);
        if (obj->type == SLEEPY_OBJ_ARRAY)
            return SLEEPY_NUM_VAL((double)((SleepyObjArray*)obj)->count);
        if (obj->type == SLEEPY_OBJ_HASH)
            return SLEEPY_NUM_VAL((double)((SleepyObjHash*)obj)->count);
    }
    return SLEEPY_NUM_VAL(0);
}

static SleepyValue native_array(SleepyVM *vm, SleepyValue *args, int argc) {
    SleepyObjArray *arr = sleepy_obj_array_new(vm->allocator);
    if (arr) {
        arr->obj.next = vm->objects;
        vm->objects = &arr->obj;
        for (int i = 0; i < argc; i++)
            sleepy_obj_array_push(vm->allocator, arr, args[i]);
    }
    return arr ? SLEEPY_OBJ_VAL(arr) : SLEEPY_NULL_VAL;
}

static SleepyValue native_push(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc >= 2 && SLEEPY_IS_OBJ(args[0]) &&
        SLEEPY_OBJ_TYPE(args[0]) == SLEEPY_OBJ_ARRAY) {
        SleepyObjArray *arr = SLEEPY_AS_ARRAY(args[0]);
        sleepy_obj_array_push(vm->allocator, arr, args[1]);
        return SLEEPY_OBJ_VAL(arr);
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_pop(SleepyVM *vm, SleepyValue *args, int argc) {
    (void)vm;
    if (argc >= 1 && SLEEPY_IS_OBJ(args[0]) &&
        SLEEPY_OBJ_TYPE(args[0]) == SLEEPY_OBJ_ARRAY) {
        return sleepy_obj_array_pop(SLEEPY_AS_ARRAY(args[0]));
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_hash(SleepyVM *vm, SleepyValue *args, int argc) {
    SleepyObjHash *hash = sleepy_obj_hash_new(vm->allocator);
    if (hash) {
        hash->obj.next = vm->objects;
        vm->objects = &hash->obj;
        for (int i = 0; i < argc - 1; i += 2)
            sleepy_obj_hash_set(vm->allocator, hash, args[i], args[i+1]);
    }
    return hash ? SLEEPY_OBJ_VAL(hash) : SLEEPY_NULL_VAL;
}

static SleepyValue native_typeof(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc < 1) return SLEEPY_NULL_VAL;
    SleepyValue v = args[0];
    if (SLEEPY_IS_NULL(v)) return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "null", 4));
    if (SLEEPY_IS_BOOL(v)) return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "boolean", 7));
    if (SLEEPY_IS_NUM(v)) return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "number", 6));
    if (SLEEPY_IS_OBJ(v)) {
        switch (SLEEPY_OBJ_TYPE(v)) {
            case SLEEPY_OBJ_STRING: return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "string", 6));
            case SLEEPY_OBJ_LONG: return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "long", 4));
            case SLEEPY_OBJ_ARRAY: return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "array", 5));
            case SLEEPY_OBJ_HASH: return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "hash", 4));
            case SLEEPY_OBJ_FUNCTION:
            case SLEEPY_OBJ_CLOSURE:
            case SLEEPY_OBJ_NATIVE:
            case SLEEPY_OBJ_BRIDGE:
                return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "function", 8));
            default: return SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, "object", 6));
        }
    }
    return SLEEPY_NULL_VAL;
}

static SleepyValue native_keys(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc < 1 || !SLEEPY_IS_OBJ(args[0]) || SLEEPY_OBJ_TYPE(args[0]) != SLEEPY_OBJ_HASH)
        return SLEEPY_NULL_VAL;
    SleepyObjHash *hash = SLEEPY_AS_HASH(args[0]);
    SleepyObjArray *arr = sleepy_obj_array_new(vm->allocator);
    if (!arr) return SLEEPY_NULL_VAL;
    arr->obj.next = vm->objects;
    vm->objects = &arr->obj;
    for (int i = 0; i < hash->capacity; i++) {
        if (!SLEEPY_IS_NULL(hash->entries[i].key)) {
            sleepy_obj_array_push(vm->allocator, arr, hash->entries[i].key);
        }
    }
    return SLEEPY_OBJ_VAL(arr);
}

static SleepyValue native_remove(SleepyVM *vm, SleepyValue *args, int argc) {
    if (argc < 2 || !SLEEPY_IS_OBJ(args[0])) return SLEEPY_BOOL_VAL(false);
    if (SLEEPY_OBJ_TYPE(args[0]) == SLEEPY_OBJ_HASH) {
        return SLEEPY_BOOL_VAL(sleepy_obj_hash_delete(vm->allocator, SLEEPY_AS_HASH(args[0]), args[1]));
    }
    return SLEEPY_BOOL_VAL(false);
}

static void define_builtins(SleepyVM *vm) {
    define_native(vm, "println", native_println);
    define_native(vm, "size", native_size);
    define_native(vm, "array", native_array);
    define_native(vm, "@", native_array);
    define_native(vm, "%", native_hash);
    define_native(vm, "push", native_push);
    define_native(vm, "pop", native_pop);
    define_native(vm, "typeof", native_typeof);
    define_native(vm, "keys", native_keys);
    define_native(vm, "remove", native_remove);
}

static bool call_closure(SleepyVM *vm, SleepyObjClosure *closure, int arg_count) {
    if (arg_count != closure->function->arity) {
            sleepy_vm_runtime_error(vm, "Wrong number of arguments.");
        return false;
    }
    if (vm->frame_count >= SLEEPY_MAX_FRAMES) {
        sleepy_vm_runtime_error(vm, "Stack overflow.");
        return false;
    }
    SleepyCallFrame *frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip = closure->function->chunk->code;
    frame->slots = vm->stack_top - arg_count - 1;
    return true;
}

static bool call_value(SleepyVM *vm, SleepyValue callee, int arg_count) {
    if (!SLEEPY_IS_OBJ(callee)) {
        sleepy_vm_runtime_error(vm, "Can only call functions and closures.");
        return false;
    }
    switch (SLEEPY_OBJ_TYPE(callee)) {
    case SLEEPY_OBJ_CLOSURE:
        return call_closure(vm, SLEEPY_AS_CLOSURE(callee), arg_count);
    case SLEEPY_OBJ_NATIVE: {
        SleepyObjNative *native = SLEEPY_AS_NATIVE(callee);
        SleepyValue result = native->fn(vm, vm->stack_top - arg_count, arg_count);
        vm->stack_top -= arg_count + 1;
        sleepy_vm_push(vm, result);
        return true;
    }
    default:
        sleepy_vm_runtime_error(vm, "Can only call functions and closures.");
        return false;
    }
}

void sleepy_vm_runtime_error(SleepyVM *vm, const char *msg) {
    if (vm->error_fn) {
        int line = 0;
        if (vm->frame_count > 0) {
            SleepyCallFrame *frame = current_frame(vm);
            int offset = (int)(frame->ip - frame->closure->function->chunk->code);
            if (offset >= 0 && offset < frame->closure->function->chunk->count)
                line = frame->closure->function->chunk->lines[offset];
        }
        vm->error_fn(vm->error_userdata, line, msg);
    }
}

SleepyResult sleepy_vm_call(SleepyVM *vm, int arg_count);

SleepyResult sleepy_vm_run(SleepyVM *vm, SleepyObjFunction *fn) {
    SleepyObjClosure *closure = sleepy_obj_closure_new(vm->allocator, fn);
    if (!closure) return SLEEPY_RUNTIME_ERROR;
    closure->obj.next = vm->objects;
    vm->objects = &closure->obj;

    sleepy_vm_push(vm, SLEEPY_OBJ_VAL(closure));
    return sleepy_vm_call(vm, 0);
}

SleepyResult sleepy_vm_call(SleepyVM *vm, int arg_count) {
    SleepyValue callee = sleepy_vm_peek(vm, arg_count);
    if (!call_value(vm, callee, arg_count))
        return SLEEPY_RUNTIME_ERROR;
    
    // We need to run the VM to actually execute the closure, 
    // unless it's a native function which call_value already executed.
    if (SLEEPY_IS_OBJ(callee) && SLEEPY_OBJ_TYPE(callee) == SLEEPY_OBJ_CLOSURE) {
        // The call_closure function set up a new frame.
        // We now start the execution loop. We need a way to run the loop until this frame returns.
        int target_frame_count = vm->frame_count - 1;
        for (;;) {
            uint8_t instruction = read_byte(current_frame(vm));
            switch (instruction) {
        case OP_PUSH_NULL:
            sleepy_vm_push(vm, SLEEPY_NULL_VAL);
            break;
        case OP_PUSH_TRUE:
            sleepy_vm_push(vm, SLEEPY_TRUE_VAL);
            break;
        case OP_PUSH_FALSE:
            sleepy_vm_push(vm, SLEEPY_FALSE_VAL);
            break;
        case OP_PUSH_CONST: {
            SleepyValue val = read_constant(current_frame(vm));
            sleepy_vm_push(vm, val);
            break;
        }
        case OP_LOAD_LOCAL_0: sleepy_vm_push(vm, current_frame(vm)->slots[0]); break;
        case OP_LOAD_LOCAL_1: sleepy_vm_push(vm, current_frame(vm)->slots[1]); break;
        case OP_LOAD_LOCAL_2: sleepy_vm_push(vm, current_frame(vm)->slots[2]); break;
        case OP_LOAD_LOCAL_3: sleepy_vm_push(vm, current_frame(vm)->slots[3]); break;
        case OP_LOAD_LOCAL_4: sleepy_vm_push(vm, current_frame(vm)->slots[4]); break;
        case OP_LOAD_LOCAL_5: sleepy_vm_push(vm, current_frame(vm)->slots[5]); break;
        case OP_LOAD_LOCAL_6: sleepy_vm_push(vm, current_frame(vm)->slots[6]); break;
        case OP_LOAD_LOCAL_7: sleepy_vm_push(vm, current_frame(vm)->slots[7]); break;
        case OP_STORE_LOCAL_0: current_frame(vm)->slots[0] = sleepy_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_1: current_frame(vm)->slots[1] = sleepy_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_2: current_frame(vm)->slots[2] = sleepy_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_3: current_frame(vm)->slots[3] = sleepy_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_4: current_frame(vm)->slots[4] = sleepy_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_5: current_frame(vm)->slots[5] = sleepy_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_6: current_frame(vm)->slots[6] = sleepy_vm_peek(vm, 0); break;
        case OP_STORE_LOCAL_7: current_frame(vm)->slots[7] = sleepy_vm_peek(vm, 0); break;
        case OP_LOAD_LOCAL: {
            uint8_t slot = read_byte(current_frame(vm));
            sleepy_vm_push(vm, current_frame(vm)->slots[slot]);
            break;
        }
        case OP_STORE_LOCAL: {
            uint8_t slot = read_byte(current_frame(vm));
            current_frame(vm)->slots[slot] = sleepy_vm_peek(vm, 0);
            break;
        }
        case OP_LOAD_GLOBAL: {
            uint16_t idx = read_short(current_frame(vm));
            SleepyValue name_val = current_frame(vm)->closure->function->chunk->constants[idx];
            SleepyObjString *name_str = SLEEPY_AS_STRING(name_val);
            
            // Inline fast-path for string lookup in globals
            SleepyObjHash *hash = vm->globals;
            SleepyValue val = SLEEPY_NULL_VAL;
            if (hash->capacity > 0) {
                uint32_t bucket = name_str->hash & (hash->capacity - 1);
                for (int i = 0; i < hash->capacity; i++) {
                    SleepyValue k = hash->entries[bucket].key;
                    if (SLEEPY_IS_NULL(k)) {
                        if (!SLEEPY_IS_TRUE(hash->entries[bucket].value)) break;
                    } else if (SLEEPY_IS_OBJ(k) && SLEEPY_AS_OBJ(k) == SLEEPY_AS_OBJ(name_val)) {
                        val = hash->entries[bucket].value;
                        break;
                    }
                    bucket = (bucket + 1) & (hash->capacity - 1);
                }
            }
            sleepy_vm_push(vm, val);
            break;
        }
        case OP_STORE_GLOBAL: {
            uint16_t idx = read_short(current_frame(vm));
            SleepyValue name_val = current_frame(vm)->closure->function->chunk->constants[idx];
            SleepyValue val = sleepy_vm_peek(vm, 0);
            SleepyObjString *name_str = SLEEPY_AS_STRING(name_val);
            SleepyObjHash *hash = vm->globals;
            bool set = false;

            if (hash->capacity > 0) {
                uint32_t bucket = name_str->hash & (hash->capacity - 1);
                for (int i = 0; i < hash->capacity; i++) {
                    SleepyValue k = hash->entries[bucket].key;
                    if (SLEEPY_IS_NULL(k)) {
                        if (!SLEEPY_IS_TRUE(hash->entries[bucket].value)) break;
                    } else if (SLEEPY_IS_OBJ(k) && SLEEPY_AS_OBJ(k) == SLEEPY_AS_OBJ(name_val)) {
                        hash->entries[bucket].value = val;
                        set = true;
                        break;
                    }
                    bucket = (bucket + 1) & (hash->capacity - 1);
                }
            }

            if (!set) {
                sleepy_obj_hash_set(vm->allocator, hash, name_val, val);
            }
            break;
        }
        case OP_LOAD_UPVALUE: {
            uint8_t idx = read_byte(current_frame(vm));
            sleepy_vm_push(vm, *current_frame(vm)->closure->upvalues[idx]->location);
            break;
        }
        case OP_STORE_UPVALUE: {
            uint8_t idx = read_byte(current_frame(vm));
            *current_frame(vm)->closure->upvalues[idx]->location = sleepy_vm_peek(vm, 0);
            break;
        }
        case OP_ADD: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            if (SLEEPY_IS_NUM(a) && SLEEPY_IS_NUM(b))
                sleepy_vm_push(vm, SLEEPY_NUM_VAL(SLEEPY_AS_NUM(a) + SLEEPY_AS_NUM(b)));
            else
                sleepy_vm_push(vm, SLEEPY_NUM_VAL(SLEEPY_AS_NUM(a) + SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_SUBTRACT: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(SLEEPY_AS_NUM(a) - SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_MULTIPLY: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(SLEEPY_AS_NUM(a) * SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_DIVIDE: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(SLEEPY_AS_NUM(a) / SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_MODULO: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL((double)((long)SLEEPY_AS_NUM(a) % (long)SLEEPY_AS_NUM(b))));
            break;
        }
        case OP_POWER: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            double result = 1;
            double base = SLEEPY_AS_NUM(a);
            long exp = (long)SLEEPY_AS_NUM(b);
            if (exp < 0) { base = 1.0/base; exp = -exp; }
            for (long i = 0; i < exp; i++) result *= base;
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(result));
            break;
        }
        case OP_NEGATE:
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(-SLEEPY_AS_NUM(sleepy_vm_pop(vm))));
            break;
        case OP_CONCAT: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            const char *sa = "", *sb = "";
            uint32_t la = 0, lb = 0;
            char a_buf[64] = {0}, b_buf[64] = {0};
            if (SLEEPY_IS_OBJ(a) && SLEEPY_OBJ_TYPE(a) == SLEEPY_OBJ_STRING) {
                SleepyObjString *osa = SLEEPY_AS_STRING(a);
                sa = osa->chars; la = osa->length;
            } else if (SLEEPY_IS_NUM(a)) {
                snprintf(a_buf, sizeof(a_buf), "%g", SLEEPY_AS_NUM(a));
                sa = a_buf; la = (uint32_t)strlen(a_buf);
            } else if (SLEEPY_IS_BOOL(a)) {
                sa = SLEEPY_AS_BOOL(a) ? "true" : "false";
                la = (uint32_t)strlen(sa);
            } else if (SLEEPY_IS_NULL(a)) {
                sa = "$null"; la = 5;
            }
            if (SLEEPY_IS_OBJ(b) && SLEEPY_OBJ_TYPE(b) == SLEEPY_OBJ_STRING) {
                SleepyObjString *osb = SLEEPY_AS_STRING(b);
                sb = osb->chars; lb = osb->length;
            } else if (SLEEPY_IS_NUM(b)) {
                snprintf(b_buf, sizeof(b_buf), "%g", SLEEPY_AS_NUM(b));
                sb = b_buf; lb = (uint32_t)strlen(b_buf);
            } else if (SLEEPY_IS_BOOL(b)) {
                sb = SLEEPY_AS_BOOL(b) ? "true" : "false";
                lb = (uint32_t)strlen(sb);
            } else if (SLEEPY_IS_NULL(b)) {
                sb = "$null"; lb = 5;
            }
            uint32_t total = la + lb;
            char *concat_buf = (char*)SLEEPY_MALLOC(vm->allocator, total + 1);
            memcpy(concat_buf, sa, la);
            memcpy(concat_buf + la, sb, lb);
            concat_buf[total] = '\0';
            SleepyObjString *result = sleepy_vm_intern_string(vm, concat_buf, total);
            SLEEPY_FREE(vm->allocator, concat_buf);
            sleepy_vm_push(vm, SLEEPY_OBJ_VAL(result));
            break;
        }
        case OP_REPEAT: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            (void)a; (void)b;
            sleepy_vm_push(vm, SLEEPY_NULL_VAL);
            break;
        }
        case OP_EQUAL: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(sleepy_value_equals(a, b)));
            break;
        }
        case OP_NOT_EQUAL: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(!sleepy_value_equals(a, b)));
            break;
        }
        case OP_LESS: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(SLEEPY_AS_NUM(a) < SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_GREATER: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(SLEEPY_AS_NUM(a) > SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_LESS_EQUAL: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(SLEEPY_AS_NUM(a) <= SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_GREATER_EQUAL: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(SLEEPY_AS_NUM(a) >= SLEEPY_AS_NUM(b)));
            break;
        }
        case OP_SPACESHIP: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            double da = SLEEPY_AS_NUM(a), db = SLEEPY_AS_NUM(b);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(da < db ? -1.0 : (da > db ? 1.0 : 0.0)));
            break;
        }
        case OP_NOT:
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(sleepy_value_is_falsy(sleepy_vm_pop(vm))));
            break;
        case OP_AND: {
            uint16_t offset = read_short(current_frame(vm));
            if (sleepy_value_is_falsy(sleepy_vm_peek(vm, 0))) {
                current_frame(vm)->ip += offset;
            } else {
                sleepy_vm_pop(vm);
            }
            break;
        }
        case OP_OR: {
            uint16_t offset = read_short(current_frame(vm));
            if (!sleepy_value_is_falsy(sleepy_vm_peek(vm, 0))) {
                current_frame(vm)->ip += offset;
            } else {
                sleepy_vm_pop(vm);
            }
            break;
        }
        case OP_BIT_AND: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL((double)((long)SLEEPY_AS_NUM(a) & (long)SLEEPY_AS_NUM(b))));
            break;
        }
        case OP_BIT_OR: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL((double)((long)SLEEPY_AS_NUM(a) | (long)SLEEPY_AS_NUM(b))));
            break;
        }
        case OP_BIT_XOR: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL((double)((long)SLEEPY_AS_NUM(a) ^ (long)SLEEPY_AS_NUM(b))));
            break;
        }
        case OP_BIT_NOT:
            sleepy_vm_push(vm, SLEEPY_NUM_VAL((double)(~(long)SLEEPY_AS_NUM(sleepy_vm_pop(vm)))));
            break;
        case OP_LSHIFT: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL((double)((long)SLEEPY_AS_NUM(a) << (long)SLEEPY_AS_NUM(b))));
            break;
        }
        case OP_RSHIFT: {
            SleepyValue b = sleepy_vm_pop(vm);
            SleepyValue a = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL((double)((long)SLEEPY_AS_NUM(a) >> (long)SLEEPY_AS_NUM(b))));
            break;
        }
        case OP_INCREMENT: {
            SleepyValue v = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(SLEEPY_AS_NUM(v) + 1));
            break;
        }
        case OP_DECREMENT: {
            SleepyValue v = sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_NUM_VAL(SLEEPY_AS_NUM(v) - 1));
            break;
        }
        case OP_POP:
            sleepy_vm_pop(vm);
            break;
        case OP_DUP:
            sleepy_vm_push(vm, sleepy_vm_peek(vm, 0));
            break;
        case OP_JUMP: {
            uint16_t offset = read_short(current_frame(vm));
            current_frame(vm)->ip += offset;
            break;
        }
        case OP_JUMP_IF_FALSE: {
            uint16_t offset = read_short(current_frame(vm));
            if (sleepy_value_is_falsy(sleepy_vm_peek(vm, 0)))
                current_frame(vm)->ip += offset;
            break;
        }
        case OP_JUMP_IF_TRUE: {
            uint16_t offset = read_short(current_frame(vm));
            if (!sleepy_value_is_falsy(sleepy_vm_peek(vm, 0)))
                current_frame(vm)->ip += offset;
            break;
        }
        case OP_LOOP: {
            int16_t offset = (int16_t)read_short(current_frame(vm));
            current_frame(vm)->ip -= offset;
            break;
        }
        case OP_CALL: {
            uint8_t arg_count = read_byte(current_frame(vm));
            if (!call_value(vm, sleepy_vm_peek(vm, arg_count), arg_count))
                return SLEEPY_RUNTIME_ERROR;
            break;
        }
        case OP_RETURN: {
            SleepyValue result = sleepy_vm_pop(vm);
            close_upvalues(vm, current_frame(vm)->slots);
            vm->frame_count--;
            if (vm->frame_count == target_frame_count) {
                vm->stack_top = current_frame(vm)->slots + 1; // Wait, current_frame is now the caller. We want to pop the function and args, which `slots` points to.
                // Actually the current frame is now the caller! So we shouldn't use current_frame(vm)->slots + 1. 
                // The frame we just returned from had `slots = vm->stack_top - arg_count - 1`. 
                // We should just pop the result, pop the frame, pop the args and function, and push the result.
                // In call_closure, `frame->slots` is set to `vm->stack_top - arg_count - 1`. So the function is at `slots[0]`, args at `slots[1..arg_count]`.
                // So `vm->stack_top = frame->slots` resets the stack to before the function call, then we push `result`.
                // Since we already did `vm->frame_count--`, we need to use the old frame's slots.
                SleepyCallFrame *old_frame = &vm->frames[vm->frame_count];
                vm->stack_top = old_frame->slots;
                sleepy_vm_push(vm, result);
                return SLEEPY_OK;
            }
            if (vm->frame_count == 0) return SLEEPY_OK; // Should not happen here if target_frame_count was set right, but safe.
            SleepyCallFrame *old_frame = &vm->frames[vm->frame_count];
            vm->stack_top = old_frame->slots;
            sleepy_vm_push(vm, result);
            break;
        }
        case OP_CLOSURE: {
            uint16_t fn_idx = read_short(current_frame(vm));
            SleepyObjFunction *fn_obj = (SleepyObjFunction*)SLEEPY_AS_OBJ(
                current_frame(vm)->closure->function->chunk->constants[fn_idx]);
            SleepyObjClosure *cl = sleepy_obj_closure_new(vm->allocator, fn_obj);
            cl->obj.next = vm->objects;
            vm->objects = &cl->obj;
            for (int i = 0; i < fn_obj->upvalue_count; i++) {
                uint8_t is_local = read_byte(current_frame(vm));
                uint8_t idx = read_byte(current_frame(vm));
                if (is_local) {
                    cl->upvalues[i] = sleepy_obj_upvalue_new(vm->allocator,
                        &current_frame(vm)->slots[idx]);
                    cl->upvalues[i]->obj.next = vm->objects;
                    vm->objects = &cl->upvalues[i]->obj;
                } else {
                    cl->upvalues[i] = current_frame(vm)->closure->upvalues[idx];
                }
            }
            sleepy_vm_push(vm, SLEEPY_OBJ_VAL(cl));
            break;
        }
        case OP_CLOSE_UPVALUE:
            close_upvalues(vm, vm->stack_top - 1);
            sleepy_vm_pop(vm);
            break;
        case OP_BUILD_ARRAY: {
            uint16_t count = read_short(current_frame(vm));
            SleepyObjArray *arr = sleepy_obj_array_new(vm->allocator);
            arr->obj.next = vm->objects;
            vm->objects = &arr->obj;
            for (uint16_t i = 0; i < count; i++) {
                SleepyValue val = sleepy_vm_pop(vm);
                sleepy_obj_array_push(vm->allocator, arr, val);
            }
            for (int i = 0; i < arr->count / 2; i++) {
                SleepyValue tmp = arr->elements[i];
                arr->elements[i] = arr->elements[arr->count - 1 - i];
                arr->elements[arr->count - 1 - i] = tmp;
            }
            sleepy_vm_push(vm, SLEEPY_OBJ_VAL(arr));
            break;
        }
        case OP_BUILD_HASH: {
            uint16_t count = read_short(current_frame(vm));
            SleepyObjHash *hash = sleepy_obj_hash_new(vm->allocator);
            hash->obj.next = vm->objects;
            vm->objects = &hash->obj;
            for (uint16_t i = 0; i < count; i++) {
                SleepyValue val = sleepy_vm_pop(vm);
                SleepyValue key = sleepy_vm_pop(vm);
                sleepy_obj_hash_set(vm->allocator, hash, key, val);
            }
            sleepy_vm_push(vm, SLEEPY_OBJ_VAL(hash));
            break;
        }
        case OP_INDEX_GET: {
            SleepyValue idx_val = sleepy_vm_pop(vm);
            SleepyValue container = sleepy_vm_pop(vm);
            if (SLEEPY_IS_OBJ(container)) {
                SleepyObj *obj = SLEEPY_AS_OBJ(container);
                if (obj->type == SLEEPY_OBJ_ARRAY) {
                    int idx = (int)SLEEPY_AS_NUM(idx_val);
                    sleepy_vm_push(vm, sleepy_obj_array_get((SleepyObjArray*)obj, idx));
                } else if (obj->type == SLEEPY_OBJ_HASH) {
                    sleepy_vm_push(vm, sleepy_obj_hash_get((SleepyObjHash*)obj, idx_val));
                } else {
                    sleepy_vm_push(vm, SLEEPY_NULL_VAL);
                }
            } else {
                sleepy_vm_push(vm, SLEEPY_NULL_VAL);
            }
            break;
        }
        case OP_INDEX_SET: {
            SleepyValue val = sleepy_vm_pop(vm);
            SleepyValue idx_val = sleepy_vm_pop(vm);
            SleepyValue container = sleepy_vm_pop(vm);
            if (SLEEPY_IS_OBJ(container)) {
                SleepyObj *obj = SLEEPY_AS_OBJ(container);
                if (obj->type == SLEEPY_OBJ_ARRAY)
                    sleepy_obj_array_set(vm->allocator, (SleepyObjArray*)obj, (int)SLEEPY_AS_NUM(idx_val), val);
                else if (obj->type == SLEEPY_OBJ_HASH)
                    sleepy_obj_hash_set(vm->allocator, (SleepyObjHash*)obj, idx_val, val);
            }
            sleepy_vm_push(vm, val);
            break;
        }
        case OP_PUSH_HANDLER: {
            uint16_t offset = read_short(current_frame(vm));
            SleepyCallFrame *frame = current_frame(vm);
            if (vm->try_handler_count < SLEEPY_MAX_HANDLERS) {
                vm->try_handlers[vm->try_handler_count].catch_ip = frame->ip + offset;
                vm->try_handlers[vm->try_handler_count].frame_count = vm->frame_count;
                vm->try_handler_count++;
            }
            break;
        }
        case OP_POP_HANDLER:
            if (vm->try_handler_count > 0)
                vm->try_handler_count--;
            break;
        case OP_THROW: {
            SleepyValue exc = sleepy_vm_pop(vm);
            if (vm->try_handler_count > 0) {
                vm->try_handler_count--;
                SleepyTryHandler h = vm->try_handlers[vm->try_handler_count];
                while (vm->frame_count > h.frame_count) {
                    close_upvalues(vm, current_frame(vm)->slots);
                    vm->frame_count--;
                }
                current_frame(vm)->ip = h.catch_ip;
                sleepy_vm_push(vm, exc);
            } else {
                vm->thrown_exception = exc;
                return SLEEPY_RUNTIME_ERROR;
            }
            break;
        }
        case OP_ASSERT: {
            SleepyValue test = sleepy_vm_pop(vm);
            if (sleepy_value_is_falsy(test)) {
                sleepy_vm_runtime_error(vm, "Assertion failed.");
                return SLEEPY_RUNTIME_ERROR;
            }
            break;
        }
        case OP_HALT:
            return SLEEPY_HALT;
        case OP_DONE:
            return SLEEPY_OK;
        case OP_YIELD:
            return SLEEPY_OK;
        case OP_BREAK:
        case OP_CONTINUE:
            break;
        case OP_BRIDGE_REGISTER: {
            uint16_t kw_idx = read_short(current_frame(vm));
            uint16_t name_idx = read_short(current_frame(vm));
            SleepyValue closure_val = sleepy_vm_pop(vm);
            SleepyObjClosure *cl = SLEEPY_AS_CLOSURE(closure_val);
            SleepyChunk *chunk = current_frame(vm)->closure->function->chunk;
            SleepyObjString *kw_str = (SleepyObjString*)SLEEPY_AS_OBJ(chunk->constants[kw_idx]);
            SleepyObjString *name_str = (SleepyObjString*)SLEEPY_AS_OBJ(chunk->constants[name_idx]);
            bool handled = false;
            SleepyBridgeType *bt = vm->bridge_types;
            while (bt) {
                if (strcmp(bt->keyword, kw_str->chars) == 0) {
                    if (bt->handler) {
                        bt->handler(vm, kw_str->chars, name_str ? name_str->chars : NULL,
                                    NULL, cl, bt->userdata);
                    }
                    handled = true;
                    break;
                }
                bt = bt->next;
            }
            if (!handled || (bt && !bt->handler)) {
                if (name_str && name_str->length > 0) {
                    sleepy_obj_hash_set(vm->allocator, vm->globals,
                        SLEEPY_OBJ_VAL(name_str), SLEEPY_OBJ_VAL(cl));
                }
            }
            break;
        }
        case OP_IMPORT:
            read_short(current_frame(vm));
            read_short(current_frame(vm));
            break;
        case OP_BACKTICK:
            read_short(current_frame(vm));
            break;
        case OP_CLASS_LITERAL:
            read_short(current_frame(vm));
            break;
        case OP_ADDRESS: {
            uint16_t idx = read_short(current_frame(vm));
            SleepyValue name_val = current_frame(vm)->closure->function->chunk->constants[idx];
            SleepyValue val = sleepy_obj_hash_get(vm->globals, name_val);
            sleepy_vm_push(vm, val);
            break;
        }
        case OP_LOCAL:
            read_byte(current_frame(vm));
            break;
        case OP_THIS:
            break;
        case OP_UNPACK_TUPLE:
            read_byte(current_frame(vm));
            break;
        case OP_TAILCALL: {
            read_byte(current_frame(vm));
            break;
        }
        case OP_FOREACH_NEXT: {
            uint16_t offset = read_short(current_frame(vm));
            SleepyValue iterator = sleepy_vm_peek(vm, 0);
            SleepyValue collection = sleepy_vm_peek(vm, 1);
            
            bool done = true;
            SleepyValue key = SLEEPY_NULL_VAL;
            SleepyValue val = SLEEPY_NULL_VAL;
            
            if (SLEEPY_IS_OBJ(collection)) {
                SleepyObj *obj = SLEEPY_AS_OBJ(collection);
                int idx = (int)SLEEPY_AS_NUM(iterator);
                if (obj->type == SLEEPY_OBJ_ARRAY) {
                    SleepyObjArray *arr = (SleepyObjArray*)obj;
                    if (idx >= 0 && idx < arr->count) {
                        key = SLEEPY_NUM_VAL((double)idx);
                        val = arr->elements[idx];
                        done = false;
                    }
                } else if (obj->type == SLEEPY_OBJ_HASH) {
                    SleepyObjHash *hash = (SleepyObjHash*)obj;
                    int cur = 0;
                    for (int i = 0; i < hash->capacity; i++) {
                        if (!SLEEPY_IS_NULL(hash->entries[i].key)) {
                            if (cur == idx) {
                                key = hash->entries[i].key;
                                val = hash->entries[i].value;
                                done = false;
                                break;
                            }
                            cur++;
                        }
                    }
                } else if (obj->type == SLEEPY_OBJ_STRING) {
                    SleepyObjString *str = (SleepyObjString*)obj;
                    if (idx >= 0 && idx < (int)str->length) {
                        key = SLEEPY_NUM_VAL((double)idx);
                        char char_buf[2] = { str->chars[idx], '\0' };
                        val = SLEEPY_OBJ_VAL(sleepy_vm_copy_string(vm, char_buf, 1));
                        done = false;
                    }
                }
            }
            
            if (done) {
                current_frame(vm)->ip += offset;
            } else {
                vm->stack_top[-1] = SLEEPY_NUM_VAL(SLEEPY_AS_NUM(iterator) + 1);
                sleepy_vm_push(vm, key);
                sleepy_vm_push(vm, val);
            }
            break;
        }
        case OP_OBJ_EXPR:
            read_byte(current_frame(vm));
            break;
        case OP_MATCH:
            sleepy_vm_pop(vm); sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(false));
            break;
        case OP_NOT_MATCH:
            sleepy_vm_pop(vm); sleepy_vm_pop(vm);
            sleepy_vm_push(vm, SLEEPY_BOOL_VAL(true));
            break;
        case OP_UNARY_PREDICATE:
            read_short(current_frame(vm));
            break;
        case OP_BINARY_PREDICATE:
            read_short(current_frame(vm));
            break;
        case OP_NEGATED_BINARY_PREDICATE:
            read_short(current_frame(vm));
            break;
        case OP_CALLCC:
            read_byte(current_frame(vm));
            break;
        case OP_RESUME:
            break;
        case OP_NOP:
            break;
        default:
            sleepy_vm_runtime_error(vm, "Unknown opcode.");
            return SLEEPY_RUNTIME_ERROR;
        }
    }
    }
    return SLEEPY_OK;
}

SleepyResult sleepy_vm_interpret(SleepyVM *vm, const char *source) {
    SleepyParser parser;
    sleepy_parser_init(&parser, source, vm->allocator);
    SleepyASTNode *ast = sleepy_parser_parse(&parser);
    if (parser.had_error || !ast) {
        if (vm->error_fn)
            vm->error_fn(vm->error_userdata, parser.error_line,
                         parser.error_message ? parser.error_message : "Parse error");
        if (ast) sleepy_parser_free_node(ast, vm->allocator);
        return SLEEPY_COMPILE_ERROR;
    }

    SleepyObjFunction *fn = sleepy_compile(vm, ast, vm->allocator);
    sleepy_parser_free_node(ast, vm->allocator);

    if (!fn) return SLEEPY_COMPILE_ERROR;

    vm->stack_top = vm->stack;
    vm->frame_count = 0;
    vm->try_handler_count = 0;
    define_builtins(vm);

    bool had_sub = false;
    SleepyBridgeType *bt = vm->bridge_types;
    while (bt) {
        if (strcmp(bt->keyword, "sub") == 0 || strcmp(bt->keyword, "alias") == 0) {
            had_sub = true;
            break;
        }
        bt = bt->next;
    }
    if (!had_sub)
        sleepy_vm_register_bridge_type(vm, "sub", NULL, NULL);
    return sleepy_vm_run(vm, fn);
}

void sleepy_vm_register_bridge_type(SleepyVM *vm, const char *keyword,
    void (*handler)(SleepyVM*, const char*, const char*, const char*,
                    SleepyObjClosure*, void*),
    void *userdata) {
    SleepyBridgeType *bt = SLEEPY_ALLOCATE(vm->allocator, SleepyBridgeType);
    size_t kw_len = sleepy_utils_strlen(keyword);
    char *kw_copy = (char*)SLEEPY_MALLOC(vm->allocator, kw_len + 1);
    sleepy_utils_strcpy(kw_copy, keyword);
    bt->keyword = kw_copy;
    bt->handler = handler;
    bt->userdata = userdata;
    bt->next = vm->bridge_types;
    vm->bridge_types = bt;
}

void sleepy_vm_register_native(SleepyVM *vm, const char *name, SleepyNativeFn fn) {
    define_native(vm, name, fn);
}

void sleepy_vm_set_error_fn(SleepyVM *vm, SleepyVMErrorFn fn, void *ud) {
    vm->error_fn = fn;
    vm->error_userdata = ud;
}

void sleepy_vm_set_write_fn(SleepyVM *vm, SleepyVMWriteFn fn, void *ud) {
    vm->write_fn = fn;
    vm->write_userdata = ud;
}

void sleepy_vm_ffi_set_null(SleepyVM *vm, int slot) { vm->ffi_slots[slot] = SLEEPY_NULL_VAL; }
void sleepy_vm_ffi_set_bool(SleepyVM *vm, int slot, bool val) { vm->ffi_slots[slot] = SLEEPY_BOOL_VAL(val); }
void sleepy_vm_ffi_set_number(SleepyVM *vm, int slot, double val) { vm->ffi_slots[slot] = SLEEPY_NUM_VAL(val); }
void sleepy_vm_ffi_set_long(SleepyVM *vm, int slot, int64_t val) {
    SleepyObjLong *obj = sleepy_obj_long_new(vm->allocator, val);
    if (obj) { obj->obj.next = vm->objects; vm->objects = &obj->obj; }
    vm->ffi_slots[slot] = SLEEPY_OBJ_VAL(obj);
}
void sleepy_vm_ffi_set_string(SleepyVM *vm, int slot, const char *val) {
    SleepyObjString *str = sleepy_vm_copy_string(vm, val, (uint32_t)sleepy_utils_strlen(val));
    vm->ffi_slots[slot] = SLEEPY_OBJ_VAL(str);
}

bool sleepy_vm_ffi_is_null(SleepyVM *vm, int slot) { return SLEEPY_IS_NULL(vm->ffi_slots[slot]); }
bool sleepy_vm_ffi_is_bool(SleepyVM *vm, int slot) { return SLEEPY_IS_BOOL(vm->ffi_slots[slot]); }
bool sleepy_vm_ffi_is_number(SleepyVM *vm, int slot) { return SLEEPY_IS_NUM(vm->ffi_slots[slot]); }
bool sleepy_vm_ffi_is_string(SleepyVM *vm, int slot) {
    SleepyValue v = vm->ffi_slots[slot];
    return SLEEPY_IS_OBJ(v) && SLEEPY_OBJ_TYPE(v) == SLEEPY_OBJ_STRING;
}
bool sleepy_vm_ffi_get_bool(SleepyVM *vm, int slot) { return SLEEPY_AS_BOOL(vm->ffi_slots[slot]); }
double sleepy_vm_ffi_get_number(SleepyVM *vm, int slot) { return SLEEPY_AS_NUM(vm->ffi_slots[slot]); }
int64_t sleepy_vm_ffi_get_long(SleepyVM *vm, int slot) {
    SleepyValue v = vm->ffi_slots[slot];
    if (SLEEPY_IS_OBJ(v) && SLEEPY_OBJ_TYPE(v) == SLEEPY_OBJ_LONG)
        return ((SleepyObjLong*)SLEEPY_AS_OBJ(v))->value;
    return (int64_t)SLEEPY_AS_NUM(v);
}
const char *sleepy_vm_ffi_get_string(SleepyVM *vm, int slot) {
    SleepyValue v = vm->ffi_slots[slot];
    if (SLEEPY_IS_OBJ(v) && SLEEPY_OBJ_TYPE(v) == SLEEPY_OBJ_STRING)
        return ((SleepyObjString*)SLEEPY_AS_OBJ(v))->chars;
    return NULL;
}

/* --- File: ../src/sleepy_compiler.c --- */
#include <string.h>

static SleepyChunk *current_chunk(SleepyCompiler *compiler) {
    return compiler->function->chunk;
}

static void emit_byte(SleepyCompiler *compiler, uint8_t byte, int line) {
    sleepy_chunk_write(current_chunk(compiler), byte, line);
}

static void emit_short(SleepyCompiler *compiler, uint16_t val, int line) {
    sleepy_chunk_write_short(current_chunk(compiler), val, line);
}

static int emit_jump(SleepyCompiler *compiler, uint8_t opcode, int line) {
    emit_byte(compiler, opcode, line);
    emit_byte(compiler, 0xFF, line);
    emit_byte(compiler, 0xFF, line);
    return current_chunk(compiler)->count - 2;
}

static void patch_jump(SleepyCompiler *compiler, int offset) {
    int jump = current_chunk(compiler)->count - offset - 2;
    current_chunk(compiler)->code[offset] = (uint8_t)((jump >> 8) & 0xFF);
    current_chunk(compiler)->code[offset + 1] = (uint8_t)(jump & 0xFF);
}

static void emit_loop(SleepyCompiler *compiler, int loop_start, int line) {
    emit_byte(compiler, OP_LOOP, line);
    int offset = current_chunk(compiler)->count - loop_start + 2;
    emit_byte(compiler, (uint8_t)((offset >> 8) & 0xFF), line);
    emit_byte(compiler, (uint8_t)(offset & 0xFF), line);
}

static uint16_t make_constant(SleepyCompiler *compiler, SleepyValue value) {
    int idx = sleepy_chunk_add_constant(current_chunk(compiler), value);
    if (idx > UINT16_MAX) return 0;
    return (uint16_t)idx;
}

static void emit_constant(SleepyCompiler *compiler, SleepyValue value, int line) {
    emit_byte(compiler, OP_PUSH_CONST, line);
    emit_short(compiler, make_constant(compiler, value), line);
}

static void emit_return(SleepyCompiler *compiler, int line) {
    emit_byte(compiler, OP_PUSH_NULL, line);
    emit_byte(compiler, OP_RETURN, line);
}

static void __attribute__((unused)) compiler_error(SleepyCompiler *compiler, int line, const char *msg) {
    compiler->had_error = true;
    compiler->error_line = line;
    compiler->error_message = msg;
}

static void init_compiler(SleepyCompiler *compiler, SleepyCompiler *enclosing,
                          SleepyVM *vm, SleepyAllocator *allocator) {
    compiler->enclosing = enclosing;
    compiler->vm = vm;
    compiler->allocator = allocator;
    compiler->function = sleepy_obj_function_new(allocator);
    if (!compiler->function) return;
    compiler->function->obj.next = vm->objects;
    vm->objects = &compiler->function->obj;
    compiler->local_count = 0;
    compiler->scope_depth = 0;
    compiler->upvalue_count = 0;
    compiler->had_error = false;
    compiler->error_line = 0;
    compiler->error_message = NULL;

    SleepyLocal *local = &compiler->locals[compiler->local_count++];
    local->depth = 0;
    local->is_captured = false;
    local->name = NULL;
}

static void begin_scope(SleepyCompiler *compiler) {
    compiler->scope_depth++;
}

static void end_scope(SleepyCompiler *compiler) {
    compiler->scope_depth--;
    while (compiler->local_count > 1 &&
           compiler->locals[compiler->local_count - 1].depth > compiler->scope_depth) {
        if (compiler->locals[compiler->local_count - 1].is_captured)
            emit_byte(compiler, OP_CLOSE_UPVALUE, 0);
        else
            emit_byte(compiler, OP_POP, 0);
        compiler->local_count--;
    }
}

static SleepyObjString *intern_str(SleepyCompiler *compiler, const char *chars, uint32_t len) {
    return sleepy_vm_copy_string(compiler->vm, chars, len);
}

static int resolve_local(SleepyCompiler *compiler, const char *name, uint32_t len) {
    for (int i = compiler->local_count - 1; i >= 0; i--) {
        SleepyLocal *local = &compiler->locals[i];
        if (local->name && local->name->length == len) {
            bool match = true;
            for (uint32_t j = 0; j < len; j++) {
                if (local->name->chars[j] != name[j]) { match = false; break; }
            }
            if (match) return i;
        }
    }
    return -1;
}

static int add_upvalue(SleepyCompiler *compiler, uint8_t index, bool is_local) {
    int upvalue_count = compiler->upvalue_count;
    for (int i = 0; i < upvalue_count; i++) {
        SleepyCompilerUpvalue *uv = &compiler->upvalues[i];
        if (uv->index == index && uv->is_local == is_local)
            return i;
    }
    compiler->upvalues[upvalue_count].index = index;
    compiler->upvalues[upvalue_count].is_local = is_local;
    compiler->upvalue_count++;
    compiler->function->upvalue_count = compiler->upvalue_count;
    return upvalue_count;
}

static int resolve_upvalue(SleepyCompiler *compiler, const char *name, uint32_t len) {
    if (!compiler->enclosing) return -1;
    int local = resolve_local(compiler->enclosing, name, len);
    if (local != -1) {
        compiler->enclosing->locals[local].is_captured = true;
        return add_upvalue(compiler, (uint8_t)local, true);
    }
    int upvalue = resolve_upvalue(compiler->enclosing, name, len);
    if (upvalue != -1)
        return add_upvalue(compiler, (uint8_t)upvalue, false);
    return -1;
}

static void named_variable(SleepyCompiler *compiler, const char *name, uint32_t len, bool assign, int line);
static void compile_node(SleepyCompiler *compiler, SleepyASTNode *node);

static void compile_expr(SleepyCompiler *compiler, SleepyASTNode *node) {
    if (!node) {
        emit_byte(compiler, OP_PUSH_NULL, 0);
        return;
    }
    compile_node(compiler, node);
}

static void compile_node(SleepyCompiler *compiler, SleepyASTNode *node) {
    if (!node) return;
    int line = node->line;

    switch (node->type) {
    case SLEEPY_AST_SCRIPT:
    case SLEEPY_AST_BLOCK: {
        for (size_t i = 0; i < node->as.block.count; i++) {
            compile_node(compiler, node->as.block.statements[i]);
            if (node->as.block.statements[i]->type != SLEEPY_AST_ENV_BRIDGE &&
                node->as.block.statements[i]->type != SLEEPY_AST_IF &&
                node->as.block.statements[i]->type != SLEEPY_AST_WHILE &&
                node->as.block.statements[i]->type != SLEEPY_AST_FOR &&
                node->as.block.statements[i]->type != SLEEPY_AST_FOREACH &&
                node->as.block.statements[i]->type != SLEEPY_AST_ASSERT &&
                node->as.block.statements[i]->type != SLEEPY_AST_TRY_CATCH &&
                node->as.block.statements[i]->type != SLEEPY_AST_RETURN &&
                node->as.block.statements[i]->type != SLEEPY_AST_THROW &&
                node->as.block.statements[i]->type != SLEEPY_AST_YIELD &&
                node->as.block.statements[i]->type != SLEEPY_AST_BREAK &&
                node->as.block.statements[i]->type != SLEEPY_AST_CONTINUE &&
                node->as.block.statements[i]->type != SLEEPY_AST_NOP) {
                emit_byte(compiler, OP_POP, line);
            }
        }
        break;
    }
    case SLEEPY_AST_BOOLEAN:
        emit_byte(compiler, node->as.boolean ? OP_PUSH_TRUE : OP_PUSH_FALSE, line);
        break;
    case SLEEPY_AST_NULL:
        emit_byte(compiler, OP_PUSH_NULL, line);
        break;
    case SLEEPY_AST_NUMBER: {
        SleepyValue val = SLEEPY_NUM_VAL(node->as.double_val);
        emit_constant(compiler, val, line);
        break;
    }
    case SLEEPY_AST_LONG: {
        SleepyObjLong *obj = sleepy_obj_long_new(compiler->allocator, node->as.long_val);
        if (obj) { obj->obj.next = compiler->vm->objects; compiler->vm->objects = &obj->obj; }
        emit_constant(compiler, SLEEPY_OBJ_VAL(obj), line);
        break;
    }
    case SLEEPY_AST_STRING:
    case SLEEPY_AST_LITERAL: {
        if (node->as.string_val) {
            uint32_t slen = (uint32_t)sleepy_utils_strlen(node->as.string_val);
            SleepyObjString *str = intern_str(compiler, node->as.string_val, slen);
            emit_constant(compiler, SLEEPY_OBJ_VAL(str), line);
        } else {
            emit_byte(compiler, OP_PUSH_NULL, line);
        }
        break;
    }
    case SLEEPY_AST_SCALAR: {
        if (node->as.string_val) {
            uint32_t slen = (uint32_t)sleepy_utils_strlen(node->as.string_val);
            named_variable(compiler, node->as.string_val, slen, false, line);
        }
        break;
    }
    case SLEEPY_AST_ARRAY: {
        if (node->as.string_val) {
            uint32_t slen = (uint32_t)sleepy_utils_strlen(node->as.string_val);
            named_variable(compiler, node->as.string_val, slen, false, line);
        } else {
            emit_byte(compiler, OP_PUSH_NULL, line);
        }
        break;
    }
    case SLEEPY_AST_HASHTABLE: {
        if (node->as.string_val) {
            uint32_t slen = (uint32_t)sleepy_utils_strlen(node->as.string_val);
            named_variable(compiler, node->as.string_val, slen, false, line);
        } else {
            emit_byte(compiler, OP_PUSH_NULL, line);
        }
        break;
    }
    case SLEEPY_AST_IDENTIFIER: {
        if (node->as.string_val) {
            uint32_t slen = (uint32_t)sleepy_utils_strlen(node->as.string_val);
            named_variable(compiler, node->as.string_val, slen, false, line);
        }
        break;
    }
    case SLEEPY_AST_BINOP: {
        compile_expr(compiler, node->as.binop.left);
        if (node->as.binop.op.type == SLEEPY_TOKEN_LAND) {
            int end_jump = emit_jump(compiler, OP_AND, line);
            compile_expr(compiler, node->as.binop.right);
            patch_jump(compiler, end_jump);
            break;
        }
        if (node->as.binop.op.type == SLEEPY_TOKEN_LOR) {
            int end_jump = emit_jump(compiler, OP_OR, line);
            compile_expr(compiler, node->as.binop.right);
            patch_jump(compiler, end_jump);
            break;
        }
        compile_expr(compiler, node->as.binop.right);
        SleepyTokenType op_type = node->as.binop.op.type;
        if (node->as.binop.negate)
            emit_byte(compiler, OP_NOT, line);
        switch (op_type) {
        case SLEEPY_TOKEN_PLUS:      emit_byte(compiler, OP_ADD, line); break;
        case SLEEPY_TOKEN_MINUS:     emit_byte(compiler, OP_SUBTRACT, line); break;
        case SLEEPY_TOKEN_STAR:      emit_byte(compiler, OP_MULTIPLY, line); break;
        case SLEEPY_TOKEN_SLASH:     emit_byte(compiler, OP_DIVIDE, line); break;
        case SLEEPY_TOKEN_PERCENT:   emit_byte(compiler, OP_MODULO, line); break;
        case SLEEPY_TOKEN_EXP:       emit_byte(compiler, OP_POWER, line); break;
        case SLEEPY_TOKEN_DOT:       emit_byte(compiler, OP_CONCAT, line); break;
        case SLEEPY_TOKEN_LOWER_X:   emit_byte(compiler, OP_REPEAT, line); break;
        case SLEEPY_TOKEN_EQ:        emit_byte(compiler, OP_EQUAL, line); break;
        case SLEEPY_TOKEN_NE:        emit_byte(compiler, OP_NOT_EQUAL, line); break;
        case SLEEPY_TOKEN_LESS:      emit_byte(compiler, OP_LESS, line); break;
        case SLEEPY_TOKEN_GREATER:   emit_byte(compiler, OP_GREATER, line); break;
        case SLEEPY_TOKEN_LE:        emit_byte(compiler, OP_LESS_EQUAL, line); break;
        case SLEEPY_TOKEN_GE:        emit_byte(compiler, OP_GREATER_EQUAL, line); break;
        case SLEEPY_TOKEN_SPACESHIP: emit_byte(compiler, OP_SPACESHIP, line); break;
        case SLEEPY_TOKEN_EQI:       emit_byte(compiler, OP_MATCH, line); break;
        case SLEEPY_TOKEN_NEQI:      emit_byte(compiler, OP_NOT_MATCH, line); break;
        case SLEEPY_TOKEN_AMPERSAND: emit_byte(compiler, OP_BIT_AND, line); break;
        case SLEEPY_TOKEN_PIPE:      emit_byte(compiler, OP_BIT_OR, line); break;
        case SLEEPY_TOKEN_CARET:     emit_byte(compiler, OP_BIT_XOR, line); break;
        case SLEEPY_TOKEN_LSHIFT:    emit_byte(compiler, OP_LSHIFT, line); break;
        case SLEEPY_TOKEN_RSHIFT:    emit_byte(compiler, OP_RSHIFT, line); break;
        case SLEEPY_TOKEN_BUILTIN_BINARY_PREDICATE_BRIDGE: {
            SleepyObjString *pred_name = intern_str(compiler,
                node->as.binop.op.start, (uint32_t)node->as.binop.op.length);
            emit_byte(compiler, node->as.binop.negate ? OP_NEGATED_BINARY_PREDICATE : OP_BINARY_PREDICATE, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(pred_name)), line);
            break;
        }
        default:
            emit_byte(compiler, OP_ADD, line);
            break;
        }
        break;
    }
    case SLEEPY_AST_UNARYOP: {
        compile_expr(compiler, node->as.unaryop.operand);
        switch (node->as.unaryop.op.type) {
        case SLEEPY_TOKEN_MINUS: emit_byte(compiler, OP_NEGATE, line); break;
        case SLEEPY_TOKEN_BANG:  emit_byte(compiler, OP_NOT, line); break;
        case SLEEPY_TOKEN_INC:   emit_byte(compiler, OP_INCREMENT, line); break;
        case SLEEPY_TOKEN_DEC:   emit_byte(compiler, OP_DECREMENT, line); break;
        default: break;
        }
        break;
    }
    case SLEEPY_AST_ASSIGNMENT: {
        compile_expr(compiler, node->as.assign.right);
        if (node->as.assign.left) {
            switch (node->as.assign.left->type) {
            case SLEEPY_AST_SCALAR:
            case SLEEPY_AST_ARRAY:
            case SLEEPY_AST_HASHTABLE: {
                if (node->as.assign.left->as.string_val) {
                    uint32_t slen = (uint32_t)sleepy_utils_strlen(node->as.assign.left->as.string_val);
                    if (node->as.assign.op.type != SLEEPY_TOKEN_EQUAL) {
                        named_variable(compiler, node->as.assign.left->as.string_val, slen, false, line);
                    }
                    named_variable(compiler, node->as.assign.left->as.string_val, slen, true, line);
                }
                break;
            }
            case SLEEPY_AST_INDEX: {
                compile_expr(compiler, node->as.assign.left->as.index.container);
                compile_expr(compiler, node->as.assign.left->as.index.element);
                emit_byte(compiler, OP_INDEX_SET, line);
                break;
            }
            default:
                break;
            }
        }
        break;
    }
    case SLEEPY_AST_IF: {
        compile_expr(compiler, node->as.if_stmt.condition);
        int then_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, line);
        emit_byte(compiler, OP_POP, line);
        compile_expr(compiler, node->as.if_stmt.then_branch);
        int else_jump = emit_jump(compiler, OP_JUMP, line);
        patch_jump(compiler, then_jump);
        emit_byte(compiler, OP_POP, line);
        if (node->as.if_stmt.else_branch)
            compile_expr(compiler, node->as.if_stmt.else_branch);
        patch_jump(compiler, else_jump);
        break;
    }
    case SLEEPY_AST_WHILE: {
        int outer_loop_start = compiler->loop_start;
        int outer_loop_continue = compiler->loop_continue_target;
        int outer_loop_exit = compiler->loop_exit_jump;
        int outer_loop_depth = compiler->loop_scope_depth;
        int outer_break_count = compiler->break_jump_count;
        int outer_continue_count = compiler->continue_jump_count;
        compiler->loop_start = current_chunk(compiler)->count;
        compiler->loop_continue_target = compiler->loop_start;
        compiler->loop_scope_depth = compiler->scope_depth;
        compiler->break_jump_count = 0;
        compiler->continue_jump_count = 0;
        compile_expr(compiler, node->as.while_stmt.condition);
        int exit_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, line);
        compiler->loop_exit_jump = exit_jump;
        emit_byte(compiler, OP_POP, line);
        compile_expr(compiler, node->as.while_stmt.body);
        emit_loop(compiler, compiler->loop_start, line);
        patch_jump(compiler, exit_jump);
        emit_byte(compiler, OP_POP, line);
        for (int i = 0; i < compiler->break_jump_count; i++)
            patch_jump(compiler, compiler->break_jumps[i]);
        for (int i = 0; i < compiler->continue_jump_count; i++)
            patch_jump(compiler, compiler->continue_jumps[i]);
        compiler->loop_start = outer_loop_start;
        compiler->loop_continue_target = outer_loop_continue;
        compiler->loop_exit_jump = outer_loop_exit;
        compiler->loop_scope_depth = outer_loop_depth;
        compiler->break_jump_count = outer_break_count;
        compiler->continue_jump_count = outer_continue_count;
        break;
    }
    case SLEEPY_AST_FOR: {
        begin_scope(compiler);
        for (size_t i = 0; i < node->as.for_stmt.init_count; i++)
            compile_node(compiler, node->as.for_stmt.initializer[i]);
        int outer_loop_start = compiler->loop_start;
        int outer_loop_continue = compiler->loop_continue_target;
        int outer_loop_exit = compiler->loop_exit_jump;
        int outer_loop_depth = compiler->loop_scope_depth;
        int outer_break_count = compiler->break_jump_count;
        int outer_continue_count = compiler->continue_jump_count;
        compiler->loop_start = current_chunk(compiler)->count;
        compiler->loop_scope_depth = compiler->scope_depth;
        compiler->break_jump_count = 0;
        compiler->continue_jump_count = 0;
        if (node->as.for_stmt.condition) {
            compile_expr(compiler, node->as.for_stmt.condition);
            int exit_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, line);
            compiler->loop_exit_jump = exit_jump;
            emit_byte(compiler, OP_POP, line);
            compile_expr(compiler, node->as.for_stmt.body);
            int inc_start = current_chunk(compiler)->count;
            for (size_t i = 0; i < node->as.for_stmt.inc_count; i++)
                compile_node(compiler, node->as.for_stmt.increment[i]);
            emit_loop(compiler, compiler->loop_start, line);
            patch_jump(compiler, exit_jump);
            emit_byte(compiler, OP_POP, line);
            for (int i = 0; i < compiler->continue_jump_count; i++) {
                current_chunk(compiler)->code[compiler->continue_jumps[i]] =
                    (uint8_t)(((inc_start - compiler->continue_jumps[i] - 2) >> 8) & 0xFF);
                current_chunk(compiler)->code[compiler->continue_jumps[i] + 1] =
                    (uint8_t)((inc_start - compiler->continue_jumps[i] - 2) & 0xFF);
            }
        } else {
            compiler->loop_exit_jump = -1;
            compile_expr(compiler, node->as.for_stmt.body);
            int inc_start = current_chunk(compiler)->count;
            for (size_t i = 0; i < node->as.for_stmt.inc_count; i++)
                compile_node(compiler, node->as.for_stmt.increment[i]);
            emit_loop(compiler, compiler->loop_start, line);
            for (int i = 0; i < compiler->continue_jump_count; i++) {
                current_chunk(compiler)->code[compiler->continue_jumps[i]] =
                    (uint8_t)(((inc_start - compiler->continue_jumps[i] - 2) >> 8) & 0xFF);
                current_chunk(compiler)->code[compiler->continue_jumps[i] + 1] =
                    (uint8_t)((inc_start - compiler->continue_jumps[i] - 2) & 0xFF);
            }
        }
        for (int i = 0; i < compiler->break_jump_count; i++)
            patch_jump(compiler, compiler->break_jumps[i]);
        compiler->loop_start = outer_loop_start;
        compiler->loop_continue_target = outer_loop_continue;
        compiler->loop_exit_jump = outer_loop_exit;
        compiler->loop_scope_depth = outer_loop_depth;
        compiler->break_jump_count = outer_break_count;
        compiler->continue_jump_count = outer_continue_count;
        end_scope(compiler);
        break;
    }
    case SLEEPY_AST_FOREACH: {
        begin_scope(compiler);
        compile_expr(compiler, node->as.foreach.generator);
        emit_byte(compiler, OP_PUSH_CONST, line);
        emit_short(compiler, make_constant(compiler, SLEEPY_NUM_VAL(0)), line);

        int outer_loop_start = compiler->loop_start;
        int outer_loop_continue = compiler->loop_continue_target;
        int outer_loop_exit = compiler->loop_exit_jump;
        int outer_loop_depth = compiler->loop_scope_depth;
        int outer_break_count = compiler->break_jump_count;
        int outer_continue_count = compiler->continue_jump_count;
        compiler->loop_start = current_chunk(compiler)->count;
        compiler->loop_continue_target = compiler->loop_start;
        compiler->loop_scope_depth = compiler->scope_depth;
        compiler->break_jump_count = 0;
        compiler->continue_jump_count = 0;

        int exit_jump = emit_jump(compiler, OP_FOREACH_NEXT, line);
        compiler->loop_exit_jump = exit_jump;

        if (node->as.foreach.index) {
            SleepyObjString *vname = intern_str(compiler, node->as.foreach.value,
                (uint32_t)sleepy_utils_strlen(node->as.foreach.value));
            emit_byte(compiler, OP_STORE_GLOBAL, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(vname)), line);
            emit_byte(compiler, OP_POP, line);

            SleepyObjString *iname = intern_str(compiler, node->as.foreach.index,
                (uint32_t)sleepy_utils_strlen(node->as.foreach.index));
            emit_byte(compiler, OP_STORE_GLOBAL, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(iname)), line);
            emit_byte(compiler, OP_POP, line);
        } else {
            SleepyObjString *vname = intern_str(compiler, node->as.foreach.value,
                (uint32_t)sleepy_utils_strlen(node->as.foreach.value));
            emit_byte(compiler, OP_STORE_GLOBAL, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(vname)), line);
            emit_byte(compiler, OP_POP, line);
            emit_byte(compiler, OP_POP, line); // Pop unused key
        }

        compile_expr(compiler, node->as.foreach.body);

        for (int i = 0; i < compiler->continue_jump_count; i++)
            patch_jump(compiler, compiler->continue_jumps[i]);

        emit_loop(compiler, compiler->loop_start, line);
        patch_jump(compiler, exit_jump);

        for (int i = 0; i < compiler->break_jump_count; i++)
            patch_jump(compiler, compiler->break_jumps[i]);

        emit_byte(compiler, OP_POP, line); // Pop iterator state
        emit_byte(compiler, OP_POP, line); // Pop collection

        compiler->loop_start = outer_loop_start;
        compiler->loop_continue_target = outer_loop_continue;
        compiler->loop_exit_jump = outer_loop_exit;
        compiler->loop_scope_depth = outer_loop_depth;
        compiler->break_jump_count = outer_break_count;
        compiler->continue_jump_count = outer_continue_count;
        end_scope(compiler);
        break;
    }
    case SLEEPY_AST_CALL: {
        compile_expr(compiler, node->as.call.target);
        int pushed_count = 0;
        for (size_t i = 0; i < node->as.call.arg_count; i++) {
            SleepyASTNode *arg = node->as.call.args[i];
            compile_node(compiler, arg);
            if (arg->type == SLEEPY_AST_ARG || arg->type == SLEEPY_AST_KV_PAIR) {
                pushed_count += (arg->as.arg.name ? 2 : 1);
            } else {
                pushed_count += 1;
            }
        }
        emit_byte(compiler, OP_CALL, line);
        emit_byte(compiler, (uint8_t)pushed_count, line);
        break;
    }
    case SLEEPY_AST_RETURN:
        if (node->as.control.value)
            compile_expr(compiler, node->as.control.value);
        else
            emit_byte(compiler, OP_PUSH_NULL, line);
        emit_byte(compiler, OP_RETURN, line);
        break;
    case SLEEPY_AST_THROW:
        compile_expr(compiler, node->as.control.value);
        emit_byte(compiler, OP_THROW, line);
        break;
    case SLEEPY_AST_ASSERT:
        compile_expr(compiler, node->as.control.value);
        emit_byte(compiler, OP_ASSERT, line);
        break;
    case SLEEPY_AST_YIELD:
        if (node->as.control.value)
            compile_expr(compiler, node->as.control.value);
        else
            emit_byte(compiler, OP_PUSH_NULL, line);
        emit_byte(compiler, OP_YIELD, line);
        break;
    case SLEEPY_AST_BREAK:
        if (compiler->loop_start >= 0) {
            int jmp = emit_jump(compiler, OP_JUMP, line);
            if (compiler->break_jump_count < 256)
                compiler->break_jumps[compiler->break_jump_count++] = jmp;
        }
        break;
    case SLEEPY_AST_CONTINUE:
        if (compiler->loop_start >= 0) {
            int jmp = emit_jump(compiler, OP_JUMP, line);
            if (compiler->continue_jump_count < 256)
                compiler->continue_jumps[compiler->continue_jump_count++] = jmp;
        }
        break;
    case SLEEPY_AST_HALT:
        emit_byte(compiler, OP_HALT, line);
        break;
    case SLEEPY_AST_DONE:
        emit_byte(compiler, OP_DONE, line);
        break;
    case SLEEPY_AST_TRY_CATCH: {
        int catch_offset = emit_jump(compiler, OP_PUSH_HANDLER, line);
        compile_expr(compiler, node->as.try_catch.body);
        emit_byte(compiler, OP_POP_HANDLER, line);
        int exit_jump = emit_jump(compiler, OP_JUMP, line);
        patch_jump(compiler, catch_offset);
        if (node->as.try_catch.value) {
            SleepyObjString *vname = intern_str(compiler,
                node->as.try_catch.value,
                (uint32_t)sleepy_utils_strlen(node->as.try_catch.value));
            emit_byte(compiler, OP_STORE_GLOBAL, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(vname)), line);
        }
        compile_expr(compiler, node->as.try_catch.handler);
        patch_jump(compiler, exit_jump);
        break;
    }
    case SLEEPY_AST_ENV_BRIDGE: {
        SleepyCompiler sub_compiler;
        init_compiler(&sub_compiler, compiler, compiler->vm, compiler->allocator);
        if (node->as.env_bridge.body)
            compile_node(&sub_compiler, node->as.env_bridge.body);
        emit_return(&sub_compiler, line);
        if (node->as.env_bridge.keyword) {
            SleepyObjString *kw = intern_str(&sub_compiler, node->as.env_bridge.keyword,
                (uint32_t)sleepy_utils_strlen(node->as.env_bridge.keyword));
            sub_compiler.function->name = kw;
        }
        SleepyObjFunction *fn = sub_compiler.function;
        emit_byte(compiler, OP_CLOSURE, line);
        emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(fn)), line);
        for (int i = 0; i < fn->upvalue_count; i++) {
            emit_byte(compiler, sub_compiler.upvalues[i].is_local ? 1 : 0, line);
            emit_byte(compiler, sub_compiler.upvalues[i].index, line);
        }
        if (node->as.env_bridge.keyword) {
            SleepyObjString *kw = intern_str(compiler, node->as.env_bridge.keyword,
                (uint32_t)sleepy_utils_strlen(node->as.env_bridge.keyword));
            emit_byte(compiler, OP_BRIDGE_REGISTER, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(kw)), line);
            if (node->as.env_bridge.identifier) {
                SleepyObjString *name = intern_str(compiler, node->as.env_bridge.identifier,
                    (uint32_t)sleepy_utils_strlen(node->as.env_bridge.identifier));
                emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(name)), line);
            } else {
                emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(
                    intern_str(compiler, "", 0))), line);
            }
        }
        break;
    }
    case SLEEPY_AST_IMPORT: {
        if (node->as.import_stmt.target) {
            SleepyObjString *target = intern_str(compiler, node->as.import_stmt.target,
                (uint32_t)sleepy_utils_strlen(node->as.import_stmt.target));
            emit_byte(compiler, OP_IMPORT, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(target)), line);
            if (node->as.import_stmt.path) {
                SleepyObjString *path = intern_str(compiler, node->as.import_stmt.path,
                    (uint32_t)sleepy_utils_strlen(node->as.import_stmt.path));
                emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(path)), line);
            } else {
                emit_short(compiler, 0, line);
            }
        }
        break;
    }
    case SLEEPY_AST_INDEX:
        compile_expr(compiler, node->as.index.container);
        compile_expr(compiler, node->as.index.element);
        emit_byte(compiler, OP_INDEX_GET, line);
        break;
    case SLEEPY_AST_OBJ_EXPR:
        compile_expr(compiler, node->as.obj_expr.target);
        if (node->as.obj_expr.message)
            compile_expr(compiler, node->as.obj_expr.message);
        emit_byte(compiler, OP_OBJ_EXPR, line);
        emit_byte(compiler, (uint8_t)node->as.obj_expr.arg_count, line);
        break;
    case SLEEPY_AST_BACKTICK: {
        if (node->as.string_val) {
            SleepyObjString *cmd = intern_str(compiler, node->as.string_val,
                (uint32_t)sleepy_utils_strlen(node->as.string_val));
            emit_byte(compiler, OP_BACKTICK, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(cmd)), line);
        }
        break;
    }
    case SLEEPY_AST_CLASS_LITERAL: {
        if (node->as.string_val) {
            SleepyObjString *cls = intern_str(compiler, node->as.string_val,
                (uint32_t)sleepy_utils_strlen(node->as.string_val));
            emit_byte(compiler, OP_CLASS_LITERAL, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(cls)), line);
        }
        break;
    }
    case SLEEPY_AST_ADDRESS: {
        if (node->as.string_val) {
            SleepyObjString *addr = intern_str(compiler, node->as.string_val,
                (uint32_t)sleepy_utils_strlen(node->as.string_val));
            emit_byte(compiler, OP_ADDRESS, line);
            emit_short(compiler, make_constant(compiler, SLEEPY_OBJ_VAL(addr)), line);
        }
        break;
    }
    case SLEEPY_AST_LOCAL:
        emit_byte(compiler, OP_LOCAL, line);
        emit_byte(compiler, 1, line);
        break;
    case SLEEPY_AST_THIS:
        emit_byte(compiler, OP_THIS, line);
        break;
    case SLEEPY_AST_CALLCC:
        if (node->as.control.value)
            compile_expr(compiler, node->as.control.value);
        emit_byte(compiler, OP_CALLCC, line);
        emit_byte(compiler, node->as.control.value ? 1 : 0, line);
        break;
    case SLEEPY_AST_LVALUE_TUPLE:
        for (size_t i = 0; i < node->as.block.count; i++)
            compile_node(compiler, node->as.block.statements[i]);
        emit_byte(compiler, OP_UNPACK_TUPLE, line);
        emit_byte(compiler, (uint8_t)node->as.block.count, line);
        break;
    case SLEEPY_AST_ASSIGN_LOOP:
        compile_expr(compiler, node->as.assign_loop.generator);
        compile_expr(compiler, node->as.assign_loop.body);
        break;
    case SLEEPY_AST_ARG:
    case SLEEPY_AST_KV_PAIR:
        if (node->as.arg.name)
            compile_expr(compiler, node->as.arg.name);
        compile_expr(compiler, node->as.arg.value);
        break;
    case SLEEPY_AST_NOP:
        emit_byte(compiler, OP_NOP, line);
        break;
    default:
        break;
    }
}

static void named_variable(SleepyCompiler *compiler, const char *name, uint32_t len, bool assign, int line) {
    int arg = resolve_local(compiler, name, len);
    SleepyOpcode get_op, set_op;
    if (arg != -1) {
        if (arg < 8) {
            get_op = (SleepyOpcode)(OP_LOAD_LOCAL_0 + arg);
            set_op = (SleepyOpcode)(OP_STORE_LOCAL_0 + arg);
        } else {
            get_op = OP_LOAD_LOCAL;
            set_op = OP_STORE_LOCAL;
        }
    } else if ((arg = resolve_upvalue(compiler, name, len)) != -1) {
        get_op = OP_LOAD_UPVALUE;
        set_op = OP_STORE_UPVALUE;
    } else {
        get_op = OP_LOAD_GLOBAL;
        set_op = OP_STORE_GLOBAL;
        SleepyObjString *str = intern_str(compiler, name, len);
        arg = sleepy_chunk_add_constant(current_chunk(compiler), SLEEPY_OBJ_VAL(str));
    }

    if (assign) {
        if (set_op == OP_LOAD_LOCAL || set_op == OP_STORE_LOCAL) {
            emit_byte(compiler, OP_STORE_LOCAL, line);
            emit_byte(compiler, (uint8_t)arg, line);
        } else if (set_op == OP_STORE_UPVALUE) {
            emit_byte(compiler, OP_STORE_UPVALUE, line);
            emit_byte(compiler, (uint8_t)arg, line);
        } else {
            emit_byte(compiler, OP_STORE_GLOBAL, line);
            emit_short(compiler, (uint16_t)arg, line);
        }
    } else {
        if (get_op >= OP_LOAD_LOCAL_0 && get_op <= OP_LOAD_LOCAL_7) {
            emit_byte(compiler, get_op, line);
        } else if (get_op == OP_LOAD_LOCAL) {
            emit_byte(compiler, OP_LOAD_LOCAL, line);
            emit_byte(compiler, (uint8_t)arg, line);
        } else if (get_op == OP_LOAD_UPVALUE) {
            emit_byte(compiler, OP_LOAD_UPVALUE, line);
            emit_byte(compiler, (uint8_t)arg, line);
        } else {
            emit_byte(compiler, OP_LOAD_GLOBAL, line);
            emit_short(compiler, (uint16_t)arg, line);
        }
    }
}

SleepyObjFunction *sleepy_compile(SleepyVM *vm, SleepyASTNode *ast, SleepyAllocator *allocator) {
    SleepyCompiler compiler;
    init_compiler(&compiler, NULL, vm, allocator);
    if (!compiler.function) return NULL;
    compile_node(&compiler, ast);
    emit_return(&compiler, ast ? ast->line : 0);
    if (compiler.had_error) return NULL;
    return compiler.function;
}

/* --- File: ../src/sleepy_disasm.c --- */
#include <stdio.h>

const char *sleepy_opcode_name(SleepyOpcode op) {
    switch (op) {
    case OP_PUSH_NULL:   return "PUSH_NULL";
    case OP_PUSH_TRUE:   return "PUSH_TRUE";
    case OP_PUSH_FALSE:  return "PUSH_FALSE";
    case OP_PUSH_CONST:  return "PUSH_CONST";
    case OP_LOAD_LOCAL_0: return "LOAD_LOCAL_0";
    case OP_LOAD_LOCAL_1: return "LOAD_LOCAL_1";
    case OP_LOAD_LOCAL_2: return "LOAD_LOCAL_2";
    case OP_LOAD_LOCAL_3: return "LOAD_LOCAL_3";
    case OP_LOAD_LOCAL_4: return "LOAD_LOCAL_4";
    case OP_LOAD_LOCAL_5: return "LOAD_LOCAL_5";
    case OP_LOAD_LOCAL_6: return "LOAD_LOCAL_6";
    case OP_LOAD_LOCAL_7: return "LOAD_LOCAL_7";
    case OP_STORE_LOCAL_0: return "STORE_LOCAL_0";
    case OP_STORE_LOCAL_1: return "STORE_LOCAL_1";
    case OP_STORE_LOCAL_2: return "STORE_LOCAL_2";
    case OP_STORE_LOCAL_3: return "STORE_LOCAL_3";
    case OP_STORE_LOCAL_4: return "STORE_LOCAL_4";
    case OP_STORE_LOCAL_5: return "STORE_LOCAL_5";
    case OP_STORE_LOCAL_6: return "STORE_LOCAL_6";
    case OP_STORE_LOCAL_7: return "STORE_LOCAL_7";
    case OP_LOAD_LOCAL:  return "LOAD_LOCAL";
    case OP_STORE_LOCAL: return "STORE_LOCAL";
    case OP_LOAD_GLOBAL: return "LOAD_GLOBAL";
    case OP_STORE_GLOBAL: return "STORE_GLOBAL";
    case OP_LOAD_UPVALUE: return "LOAD_UPVALUE";
    case OP_STORE_UPVALUE: return "STORE_UPVALUE";
    case OP_ADD:         return "ADD";
    case OP_SUBTRACT:    return "SUBTRACT";
    case OP_MULTIPLY:    return "MULTIPLY";
    case OP_DIVIDE:      return "DIVIDE";
    case OP_MODULO:      return "MODULO";
    case OP_POWER:       return "POWER";
    case OP_NEGATE:      return "NEGATE";
    case OP_CONCAT:      return "CONCAT";
    case OP_REPEAT:      return "REPEAT";
    case OP_EQUAL:       return "EQUAL";
    case OP_NOT_EQUAL:   return "NOT_EQUAL";
    case OP_LESS:        return "LESS";
    case OP_GREATER:     return "GREATER";
    case OP_LESS_EQUAL:  return "LESS_EQUAL";
    case OP_GREATER_EQUAL: return "GREATER_EQUAL";
    case OP_SPACESHIP:   return "SPACESHIP";
    case OP_MATCH:       return "MATCH";
    case OP_NOT_MATCH:   return "NOT_MATCH";
    case OP_NOT:         return "NOT";
    case OP_AND:         return "AND";
    case OP_OR:          return "OR";
    case OP_BIT_AND:     return "BIT_AND";
    case OP_BIT_OR:      return "BIT_OR";
    case OP_BIT_XOR:     return "BIT_XOR";
    case OP_BIT_NOT:     return "BIT_NOT";
    case OP_LSHIFT:      return "LSHIFT";
    case OP_RSHIFT:      return "RSHIFT";
    case OP_INCREMENT:   return "INCREMENT";
    case OP_DECREMENT:   return "DECREMENT";
    case OP_UNARY_PREDICATE: return "UNARY_PREDICATE";
    case OP_BINARY_PREDICATE: return "BINARY_PREDICATE";
    case OP_NEGATED_BINARY_PREDICATE: return "NEGATED_BINARY_PREDICATE";
    case OP_POP:         return "POP";
    case OP_DUP:         return "DUP";
    case OP_JUMP:        return "JUMP";
    case OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
    case OP_JUMP_IF_TRUE: return "JUMP_IF_TRUE";
    case OP_LOOP:        return "LOOP";
    case OP_CALL:        return "CALL";
    case OP_RETURN:      return "RETURN";
    case OP_TAILCALL:    return "TAILCALL";
    case OP_CLOSURE:     return "CLOSURE";
    case OP_CLOSE_UPVALUE: return "CLOSE_UPVALUE";
    case OP_FOREACH_NEXT: return "FOREACH_NEXT";
    case OP_BUILD_ARRAY: return "BUILD_ARRAY";
    case OP_BUILD_HASH:  return "BUILD_HASH";
    case OP_INDEX_GET:   return "INDEX_GET";
    case OP_INDEX_SET:   return "INDEX_SET";
    case OP_OBJ_EXPR:    return "OBJ_EXPR";
    case OP_PUSH_HANDLER: return "PUSH_HANDLER";
    case OP_POP_HANDLER: return "POP_HANDLER";
    case OP_THROW:       return "THROW";
    case OP_ASSERT:      return "ASSERT";
    case OP_HALT:        return "HALT";
    case OP_DONE:        return "DONE";
    case OP_YIELD:       return "YIELD";
    case OP_CALLCC:      return "CALLCC";
    case OP_RESUME:      return "RESUME";
    case OP_BRIDGE_REGISTER: return "BRIDGE_REGISTER";
    case OP_IMPORT:      return "IMPORT";
    case OP_BACKTICK:    return "BACKTICK";
    case OP_CLASS_LITERAL: return "CLASS_LITERAL";
    case OP_ADDRESS:     return "ADDRESS";
    case OP_LOCAL:       return "LOCAL";
    case OP_THIS:        return "THIS";
    case OP_UNPACK_TUPLE: return "UNPACK_TUPLE";
    case OP_BREAK:       return "BREAK";
    case OP_CONTINUE:    return "CONTINUE";
    case OP_NOP:         return "NOP";
    case OP_END:         return "END";
    default:             return "UNKNOWN";
    }
}

static void print_value(SleepyValue val, FILE *out) {
    if (SLEEPY_IS_NULL(val))       fprintf(out, "null");
    else if (SLEEPY_IS_BOOL(val))  fprintf(out, SLEEPY_AS_BOOL(val) ? "true" : "false");
    else if (SLEEPY_IS_NUM(val))   fprintf(out, "%g", SLEEPY_AS_NUM(val));
    else if (SLEEPY_IS_OBJ(val)) {
        SleepyObj *obj = SLEEPY_AS_OBJ(val);
        switch (obj->type) {
        case SLEEPY_OBJ_STRING:
            fprintf(out, "\"%s\"", ((SleepyObjString*)obj)->chars);
            break;
        case SLEEPY_OBJ_LONG:
            fprintf(out, "%ldL", (long)((SleepyObjLong*)obj)->value);
            break;
        case SLEEPY_OBJ_FUNCTION:
            fprintf(out, "<fn %s>", ((SleepyObjFunction*)obj)->name
                ? ((SleepyObjFunction*)obj)->name->chars : "(script)");
            break;
        case SLEEPY_OBJ_CLOSURE:
            fprintf(out, "<closure>");
            break;
        case SLEEPY_OBJ_NATIVE:
            fprintf(out, "<native %s>", ((SleepyObjNative*)obj)->name
                ? ((SleepyObjNative*)obj)->name->chars : "?");
            break;
        default:
            fprintf(out, "<obj %d>", obj->type);
            break;
        }
    } else {
        fprintf(out, "?");
    }
}

static int is_short_instruction(uint8_t op) {
    switch (op) {
    case OP_PUSH_CONST:
    case OP_LOAD_LOCAL:
    case OP_STORE_LOCAL:
    case OP_LOAD_GLOBAL:
    case OP_STORE_GLOBAL:
    case OP_LOAD_UPVALUE:
    case OP_STORE_UPVALUE:
    case OP_JUMP:
    case OP_JUMP_IF_FALSE:
    case OP_JUMP_IF_TRUE:
    case OP_LOOP:
    case OP_AND:
    case OP_OR:
    case OP_PUSH_HANDLER:
    case OP_FOREACH_NEXT:
    case OP_BUILD_ARRAY:
    case OP_BUILD_HASH:
    case OP_OBJ_EXPR:
    case OP_BRIDGE_REGISTER:
    case OP_IMPORT:
    case OP_BACKTICK:
    case OP_CLASS_LITERAL:
        return 1;
    default:
        return 0;
    }
}

int sleepy_disasm_instruction(SleepyChunk *chunk, int offset, FILE *out) {
    fprintf(out, "%04d ", offset);
    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1])
        fprintf(out, "  | ");
    else
        fprintf(out, "%4d ", chunk->lines[offset]);

    uint8_t instruction = chunk->code[offset];
    const char *name = sleepy_opcode_name((SleepyOpcode)instruction);
    fprintf(out, "%-24s", name);

    if (instruction == OP_PUSH_CONST || instruction == OP_LOAD_GLOBAL ||
        instruction == OP_STORE_GLOBAL) {
        uint16_t idx = sleepy_chunk_read_short(chunk, offset + 1);
        fprintf(out, " %4d '", idx);
        if (idx < (uint16_t)chunk->constant_count)
            print_value(chunk->constants[idx], out);
        else
            fprintf(out, "???");
        fprintf(out, "'");
        return offset + 3;
    }

    if (instruction == OP_LOAD_LOCAL || instruction == OP_STORE_LOCAL ||
        instruction == OP_LOAD_UPVALUE || instruction == OP_STORE_UPVALUE) {
        uint16_t idx = sleepy_chunk_read_short(chunk, offset + 1);
        fprintf(out, " %4d", idx);
        return offset + 3;
    }

    if (is_short_instruction(instruction)) {
        uint16_t val = sleepy_chunk_read_short(chunk, offset + 1);
        if (instruction == OP_JUMP || instruction == OP_JUMP_IF_FALSE ||
            instruction == OP_JUMP_IF_TRUE || instruction == OP_LOOP ||
            instruction == OP_AND || instruction == OP_OR ||
            instruction == OP_PUSH_HANDLER) {
            int sign = (instruction == OP_LOOP) ? -1 : 1;
            fprintf(out, " -> %d", offset + 3 + sign * (int)val);
        } else {
            fprintf(out, " %4d", val);
        }
        return offset + 3;
    }

    if (instruction == OP_CALL) {
        fprintf(out, " %4d", chunk->code[offset + 1]);
        return offset + 2;
    }

    if (instruction == OP_CLOSURE) {
        uint16_t idx = sleepy_chunk_read_short(chunk, offset + 1);
        fprintf(out, " %4d '", idx);
        if (idx < (uint16_t)chunk->constant_count)
            print_value(chunk->constants[idx], out);
        fprintf(out, "'");
        SleepyObjFunction *fn = NULL;
        if (idx < (uint16_t)chunk->constant_count && SLEEPY_IS_OBJ(chunk->constants[idx])
            && SLEEPY_OBJ_TYPE(chunk->constants[idx]) == SLEEPY_OBJ_FUNCTION) {
            fn = (SleepyObjFunction*)SLEEPY_AS_OBJ(chunk->constants[idx]);
        }
        int pos = offset + 3;
        if (fn) {
            for (int i = 0; i < fn->upvalue_count; i++) {
                fprintf(out, "\n%04d %4d %-24s %4d %s",
                    pos, chunk->lines[pos], "", chunk->code[pos + 1],
                    chunk->code[pos] ? "upvalue" : "local");
                pos += 2;
            }
        }
        return pos;
    }

    return offset + 1;
}

void sleepy_disasm_chunk(SleepyChunk *chunk, const char *name, FILE *out) {
    fprintf(out, "=== %s ===\n", name);
    int offset = 0;
    while (offset < chunk->count) {
        offset = sleepy_disasm_instruction(chunk, offset, out);
        fprintf(out, "\n");
    }
}

/* --- File: ../src/sleepy_bytecode.c --- */
#include <string.h>

static void ensure_capacity(SleepyBytecodeWriter *w, size_t needed) {
    if (w->count + needed <= w->capacity) return;
    size_t new_cap = w->capacity * 2;
    if (new_cap < w->capacity + needed) new_cap = w->capacity + needed;
    if (new_cap < 64) new_cap = 64;
    w->buffer = (uint8_t*)w->allocator->reallocate(w->buffer, new_cap, w->allocator->user_data);
    w->capacity = new_cap;
}

void sleepy_bytecode_writer_init(SleepyBytecodeWriter *writer, SleepyAllocator *allocator) {
    writer->buffer = NULL;
    writer->capacity = 0;
    writer->count = 0;
    writer->allocator = allocator;
}

void sleepy_bytecode_writer_free(SleepyBytecodeWriter *writer) {
    if (writer->buffer)
        writer->allocator->reallocate(writer->buffer, 0, writer->allocator->user_data);
    writer->buffer = NULL;
    writer->capacity = 0;
    writer->count = 0;
}

void sleepy_bytecode_writer_write(SleepyBytecodeWriter *writer, const uint8_t *data, size_t len) {
    ensure_capacity(writer, len);
    memcpy(writer->buffer + writer->count, data, len);
    writer->count += len;
}

SleepyBytecodeReader sleepy_bytecode_reader_init(const uint8_t *data, size_t size) {
    SleepyBytecodeReader r;
    r.data = data;
    r.size = size;
    r.offset = 0;
    return r;
}

bool sleepy_bytecode_reader_read(SleepyBytecodeReader *reader, uint8_t *out, size_t len) {
    if (reader->offset + len > reader->size) return false;
    memcpy(out, reader->data + reader->offset, len);
    reader->offset += len;
    return true;
}

static void write_u8(SleepyBytecodeWriter *w, uint8_t val) {
    sleepy_bytecode_writer_write(w, &val, 1);
}

static void __attribute__((unused)) write_u16(SleepyBytecodeWriter *w, uint16_t val) {
    uint8_t buf[2];
    buf[0] = (uint8_t)((val >> 8) & 0xFF);
    buf[1] = (uint8_t)(val & 0xFF);
    sleepy_bytecode_writer_write(w, buf, 2);
}

static void write_u32(SleepyBytecodeWriter *w, uint32_t val) {
    uint8_t buf[4];
    buf[0] = (uint8_t)((val >> 24) & 0xFF);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >> 8) & 0xFF);
    buf[3] = (uint8_t)(val & 0xFF);
    sleepy_bytecode_writer_write(w, buf, 4);
}

static void write_u64(SleepyBytecodeWriter *w, uint64_t val) {
    uint8_t buf[8];
    for (int i = 0; i < 8; i++)
        buf[i] = (uint8_t)((val >> (56 - i * 8)) & 0xFF);
    sleepy_bytecode_writer_write(w, buf, 8);
}

static void write_f64(SleepyBytecodeWriter *w, double val) {
    uint64_t bits;
    memcpy(&bits, &val, sizeof(bits));
    write_u64(w, bits);
}

static bool read_u8(SleepyBytecodeReader *r, uint8_t *out) {
    return sleepy_bytecode_reader_read(r, out, 1);
}

static bool __attribute__((unused)) read_u16(SleepyBytecodeReader *r, uint16_t *out) {
    uint8_t buf[2];
    if (!sleepy_bytecode_reader_read(r, buf, 2)) return false;
    *out = (uint16_t)((buf[0] << 8) | buf[1]);
    return true;
}

static bool read_u32(SleepyBytecodeReader *r, uint32_t *out) {
    uint8_t buf[4];
    if (!sleepy_bytecode_reader_read(r, buf, 4)) return false;
    *out = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    return true;
}

static bool read_u64(SleepyBytecodeReader *r, uint64_t *out) {
    uint8_t buf[8];
    if (!sleepy_bytecode_reader_read(r, buf, 8)) return false;
    *out = 0;
    for (int i = 0; i < 8; i++)
        *out = (*out << 8) | buf[i];
    return true;
}

static bool read_f64(SleepyBytecodeReader *r, double *out) {
    uint64_t bits;
    if (!read_u64(r, &bits)) return false;
    memcpy(out, &bits, sizeof(bits));
    return true;
}

#define CONST_TAG_NULL    0
#define CONST_TAG_BOOL    1
#define CONST_TAG_NUMBER  2
#define CONST_TAG_STRING  3
#define CONST_TAG_LONG    4

static bool serialize_value(SleepyBytecodeWriter *w, SleepyValue val) {
    if (SLEEPY_IS_NULL(val)) {
        write_u8(w, CONST_TAG_NULL);
    } else if (SLEEPY_IS_BOOL(val)) {
        write_u8(w, CONST_TAG_BOOL);
        write_u8(w, SLEEPY_AS_BOOL(val) ? 1 : 0);
    } else if (SLEEPY_IS_NUM(val)) {
        write_u8(w, CONST_TAG_NUMBER);
        write_f64(w, SLEEPY_AS_NUM(val));
    } else if (SLEEPY_IS_OBJ(val)) {
        SleepyObj *obj = SLEEPY_AS_OBJ(val);
        if (obj->type == SLEEPY_OBJ_STRING) {
            SleepyObjString *s = (SleepyObjString*)obj;
            write_u8(w, CONST_TAG_STRING);
            write_u32(w, s->length);
            sleepy_bytecode_writer_write(w, (const uint8_t*)s->chars, s->length);
        } else if (obj->type == SLEEPY_OBJ_LONG) {
            SleepyObjLong *l = (SleepyObjLong*)obj;
            write_u8(w, CONST_TAG_LONG);
            write_u64(w, (uint64_t)l->value);
        } else {
            return false;
        }
    } else {
        return false;
    }
    return true;
}

static SleepyValue deserialize_value(SleepyBytecodeReader *r, SleepyAllocator *allocator) {
    uint8_t tag;
    if (!read_u8(r, &tag)) return SLEEPY_NULL_VAL;
    switch (tag) {
    case CONST_TAG_NULL:
        return SLEEPY_NULL_VAL;
    case CONST_TAG_BOOL: {
        uint8_t b;
        if (!read_u8(r, &b)) return SLEEPY_NULL_VAL;
        return SLEEPY_BOOL_VAL(b != 0);
    }
    case CONST_TAG_NUMBER: {
        double d;
        if (!read_f64(r, &d)) return SLEEPY_NULL_VAL;
        return SLEEPY_NUM_VAL(d);
    }
    case CONST_TAG_STRING: {
        uint32_t len;
        if (!read_u32(r, &len)) return SLEEPY_NULL_VAL;
        char *buf = (char*)allocator->reallocate(NULL, len + 1, allocator->user_data);
        if (!sleepy_bytecode_reader_read(r, (uint8_t*)buf, len)) {
            allocator->reallocate(buf, 0, allocator->user_data);
            return SLEEPY_NULL_VAL;
        }
        buf[len] = '\0';
        SleepyObjString *s = sleepy_obj_string_new(allocator, buf, len);
        allocator->reallocate(buf, 0, allocator->user_data);
        if (s) s->hash = sleepy_hash_string(s->chars, s->length);
        return s ? SLEEPY_OBJ_VAL(s) : SLEEPY_NULL_VAL;
    }
    case CONST_TAG_LONG: {
        uint64_t v;
        if (!read_u64(r, &v)) return SLEEPY_NULL_VAL;
        SleepyObjLong *l = sleepy_obj_long_new(allocator, (int64_t)v);
        return l ? SLEEPY_OBJ_VAL(l) : SLEEPY_NULL_VAL;
    }
    default:
        return SLEEPY_NULL_VAL;
    }
}

bool sleepy_bytecode_serialize_chunk(SleepyBytecodeWriter *writer, SleepyChunk *chunk) {
    write_u32(writer, (uint32_t)chunk->count);
    sleepy_bytecode_writer_write(writer, chunk->code, chunk->count);
    write_u32(writer, (uint32_t)chunk->constant_count);
    for (int i = 0; i < chunk->constant_count; i++) {
        if (!serialize_value(writer, chunk->constants[i]))
            return false;
    }
    return true;
}

SleepyChunk *sleepy_bytecode_deserialize_chunk(SleepyBytecodeReader *reader, SleepyAllocator *allocator) {
    uint32_t code_count;
    if (!read_u32(reader, &code_count)) return NULL;
    SleepyChunk *chunk = SLEEPY_ALLOCATE(allocator, SleepyChunk);
    if (!chunk) return NULL;
    sleepy_chunk_init(chunk, allocator);
    for (uint32_t i = 0; i < code_count; i++) {
        uint8_t byte;
        if (!read_u8(reader, &byte)) { sleepy_chunk_free(chunk); return NULL; }
        sleepy_chunk_write(chunk, byte, 0);
    }
    uint32_t const_count;
    if (!read_u32(reader, &const_count)) { sleepy_chunk_free(chunk); return NULL; }
    for (uint32_t i = 0; i < const_count; i++) {
        SleepyValue val = deserialize_value(reader, allocator);
        sleepy_chunk_add_constant(chunk, val);
    }
    return chunk;
}

bool sleepy_bytecode_serialize_function(SleepyBytecodeWriter *writer, SleepyObjFunction *fn) {
    write_u32(writer, SLEEPY_BYTECODE_MAGIC);
    write_u32(writer, SLEEPY_BYTECODE_VERSION);
    uint32_t name_len = fn->name ? fn->name->length : 0;
    write_u32(writer, name_len);
    if (name_len > 0 && fn->name)
        sleepy_bytecode_writer_write(writer, (const uint8_t*)fn->name->chars, name_len);
    write_u8(writer, (uint8_t)fn->arity);
    write_u8(writer, (uint8_t)fn->upvalue_count);
    return sleepy_bytecode_serialize_chunk(writer, fn->chunk);
}

SleepyObjFunction *sleepy_bytecode_deserialize_function(SleepyBytecodeReader *reader, SleepyAllocator *allocator) {
    uint32_t magic, version;
    if (!read_u32(reader, &magic) || magic != SLEEPY_BYTECODE_MAGIC) return NULL;
    if (!read_u32(reader, &version) || version != SLEEPY_BYTECODE_VERSION) return NULL;
    uint32_t name_len;
    if (!read_u32(reader, &name_len)) return NULL;
    SleepyObjFunction *fn = sleepy_obj_function_new(allocator);
    if (!fn) return NULL;
    if (name_len > 0) {
        char *name_buf = (char*)allocator->reallocate(NULL, name_len + 1, allocator->user_data);
        if (!sleepy_bytecode_reader_read(reader, (uint8_t*)name_buf, name_len)) {
            allocator->reallocate(name_buf, 0, allocator->user_data);
            SLEEPY_FREE(allocator, fn);
            return NULL;
        }
        name_buf[name_len] = '\0';
        fn->name = sleepy_obj_string_new(allocator, name_buf, name_len);
        if (fn->name) fn->name->hash = sleepy_hash_string(fn->name->chars, fn->name->length);
        allocator->reallocate(name_buf, 0, allocator->user_data);
    }
    uint8_t arity, upvalue_count;
    if (!read_u8(reader, &arity) || !read_u8(reader, &upvalue_count)) {
        SLEEPY_FREE(allocator, fn);
        return NULL;
    }
    fn->arity = arity;
    fn->upvalue_count = upvalue_count;
    sleepy_chunk_free(fn->chunk);
    fn->chunk = sleepy_bytecode_deserialize_chunk(reader, allocator);
    if (!fn->chunk) {
        SLEEPY_FREE(allocator, fn);
        return NULL;
    }
    fn->chunk->allocator = allocator;
    return fn;
}

