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
  if (buffer->capacity < buffer->length + 1) {
    size_t new_capacity = buffer->capacity == 0 ? 8 : buffer->capacity * 2;
    buffer->data =
        (char *)SLEEPY_REALLOC(buffer->allocator, buffer->data, new_capacity);
    buffer->capacity = new_capacity;
  }

  buffer->data[buffer->length++] = c;
}

void sleepy_string_buffer_append_string(SleepyStringBuffer *buffer,
                                        const char *str, size_t length) {
  if (buffer->capacity < buffer->length + length) {
    size_t new_capacity = buffer->capacity == 0 ? 8 : buffer->capacity * 2;
    while (new_capacity < buffer->length + length) {
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
}

static void error_at(SleepyParser *parser, SleepyToken *token,
                     const char *message) {
  (void)token;
  (void)message;
  if (parser->panic_mode)
    return;
  parser->panic_mode = true;
  parser->had_error = true;
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
  node->type = type;
  node->line = parser->previous.line;
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

static SleepyASTNode *string_val(SleepyParser *parser, SleepyASTNode *left,
                                 bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_STRING);
  node->as.string_val =
      sleepy_lexer_copy_lexeme(&parser->lexer, &parser->previous);
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
    target->as.string_val = "@";
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
    target->as.string_val = "%";
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
  node->as.foreach.value = (val_node) ? val_node->as.string_val : "stub_var";
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
    } while (match(parser, SLEEPY_TOKEN_COMMA) && !check(parser, endToken));
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

// Forward declaration of internal recursive formatter
static void format_node(SleepyASTNode *node, SleepyStringBuffer *buffer,
                        int depth);

char *sleepy_ast_format(SleepyASTNode *node, SleepyAllocator *allocator) {
  if (!node || !allocator)
    return NULL;

  SleepyStringBuffer buffer;
  sleepy_string_buffer_init(&buffer, allocator);

  format_node(node, &buffer, 0);

  // Return the formatted string; caller assumes ownership.
  return buffer.data;
}

// --- Bindings Helpers ---

void sleepy_ast_get_children(SleepyASTNode *node, SleepyASTNode ***out_children,
                             size_t *out_count) {
  *out_children = NULL;
  *out_count = 0;
  if (!node)
    return;

  switch (node->type) {
  case SLEEPY_AST_SCRIPT:
  case SLEEPY_AST_BLOCK:
    *out_children = node->as.block.statements;
    *out_count = node->as.block.count;
    break;
  case SLEEPY_AST_CALL:
    *out_children = node->as.call.args;
    *out_count = node->as.call.arg_count;
    break;
  case SLEEPY_AST_BINOP: {
    static SleepyASTNode *binop_children[2];
    binop_children[0] = node->as.binop.left;
    binop_children[1] = node->as.binop.right;
    *out_children = binop_children;
    *out_count = 2;
    break;
  }
  case SLEEPY_AST_ASSIGNMENT: {
    static SleepyASTNode *assign_children[2];
    assign_children[0] = node->as.assign.left;
    assign_children[1] = node->as.assign.right;
    *out_children = assign_children;
    *out_count = 2;
    break;
  }
  case SLEEPY_AST_ARG: {
    static SleepyASTNode *arg_children[1];
    arg_children[0] = node->as.arg.value;
    *out_children = arg_children;
    *out_count = 1;
    break;
  }
  case SLEEPY_AST_ENV_BRIDGE: {
    static SleepyASTNode *bridge_children[3];
    size_t count = 0;
    if (node->as.env_bridge.body) {
      bridge_children[count++] = node->as.env_bridge.body;
    }
    *out_children = bridge_children;
    *out_count = count;
    break;
  }
  default:
    break;
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
    return node->as.string_val;
  case SLEEPY_AST_ENV_BRIDGE:
    return node->as.env_bridge.keyword;
  case SLEEPY_AST_CALL:
    if (node->as.call.target &&
        node->as.call.target->type == SLEEPY_AST_IDENTIFIER) {
      return node->as.call.target->as.string_val;
    }
    return NULL;
  default:
    return NULL;
  }
}

long sleepy_ast_get_long(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_LONG)
    return node->as.long_val;
  return 0;
}

double sleepy_ast_get_double(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_NUMBER)
    return node->as.double_val;
  return 0.0;
}

bool sleepy_ast_get_bool(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_BOOLEAN)
    return node->as.boolean;
  return false;
}

const char *sleepy_ast_get_env_bridge_id(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_ENV_BRIDGE)
    return node->as.env_bridge.identifier;
  return NULL;
}

const char *sleepy_ast_get_env_bridge_string(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_ENV_BRIDGE)
    return node->as.env_bridge.string;
  return NULL;
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
    // Use %g for nice double formatting
    snprintf(buf, sizeof(buf), "%g", node->as.double_val);
    sleepy_string_buffer_append_string(buffer, buf, sleepy_utils_strlen(buf));
    break;
  }

  case SLEEPY_AST_LITERAL:
  case SLEEPY_AST_STRING: {
    // Parser already includes quotes
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    break;
  }

  case SLEEPY_AST_SCALAR: {
    sleepy_string_buffer_append_char(buffer, '$');
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    break;
  }

  case SLEEPY_AST_IDENTIFIER:
  case SLEEPY_AST_ARRAY:
  case SLEEPY_AST_HASHTABLE:
  case SLEEPY_AST_CLASS_LITERAL: {
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
    sleepy_string_buffer_append_string(buffer, node->as.binop.op.start,
                                       node->as.binop.op.length);
    sleepy_string_buffer_append_char(buffer, ' ');
    format_node(node->as.binop.right, buffer, depth);
    break;
  }

  case SLEEPY_AST_ASSIGNMENT: {
    format_node(node->as.assign.left, buffer, depth);
    sleepy_string_buffer_append_char(buffer, ' ');
    sleepy_string_buffer_append_string(buffer, node->as.assign.op.start,
                                       node->as.assign.op.length);
    sleepy_string_buffer_append_char(buffer, ' ');
    format_node(node->as.assign.right, buffer, depth);
    break;
  }

  case SLEEPY_AST_UNARYOP: {
    // Very basic check for post ops
    bool is_post = (node->as.unaryop.op.start[0] == '+' ||
                    node->as.unaryop.op.start[0] == '-') &&
                   (node->as.unaryop.op.length == 2) && (depth > 0);
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
    sleepy_string_buffer_append_string(
        buffer, node->as.foreach.value,
        sleepy_utils_strlen(node->as.foreach.value));
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
    sleepy_string_buffer_append_string(
        buffer, node->as.try_catch.value,
        sleepy_utils_strlen(node->as.try_catch.value));
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
    sleepy_string_buffer_append_string(
        buffer, node->as.env_bridge.keyword,
        sleepy_utils_strlen(node->as.env_bridge.keyword));
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
    sleepy_string_buffer_append_string(
        buffer, node->as.import_stmt.target,
        sleepy_utils_strlen(node->as.import_stmt.target));
    if (node->as.import_stmt.path) {
      sleepy_string_buffer_append_string(buffer, " from: ", 7);
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

  default:
    sleepy_string_buffer_append_string(buffer, "/* unknown node type */", 23);
    break;
  }
}

