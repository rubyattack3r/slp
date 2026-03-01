#ifndef SLEEPY_LEXER_H
#define SLEEPY_LEXER_H

#include "sleepy_common.h"
#include "sleepy_core.h"

typedef enum {
  // Single-character and basic symbols
  SLEEPY_TOKEN_BANG = 0,      // !
  SLEEPY_TOKEN_DOT,           // .
  SLEEPY_TOKEN_AMPERSAND,     // &
  SLEEPY_TOKEN_AT,            // @
  SLEEPY_TOKEN_COMMA,         // ,
  SLEEPY_TOKEN_SLASH,         // /
  SLEEPY_TOKEN_EQUAL,         // =
  SLEEPY_TOKEN_PERCENT,       // %
  SLEEPY_TOKEN_BACKSLASH,     // backslash
  SLEEPY_TOKEN_MINUS,         // -
  SLEEPY_TOKEN_PLUS,          // +
  SLEEPY_TOKEN_LOWER_X,       // x
  SLEEPY_TOKEN_STAR,          // *
  SLEEPY_TOKEN_CARET,         // ^
  SLEEPY_TOKEN_PIPE,          // |
  SLEEPY_TOKEN_COLON,         // :
  SLEEPY_TOKEN_SEMICOLON,     // ;
  SLEEPY_TOKEN_RIGHT_BRACE,   // }
  SLEEPY_TOKEN_LEFT_BRACE,    // {
  SLEEPY_TOKEN_LEFT_BRACKET,  // [
  SLEEPY_TOKEN_RIGHT_BRACKET, // ]
  SLEEPY_TOKEN_LEFT_PAREN,    // (
  SLEEPY_TOKEN_RIGHT_PAREN,   // )
  SLEEPY_TOKEN_LESS,          // <
  SLEEPY_TOKEN_GREATER,       // >

  // Keywords
  SLEEPY_TOKEN_ASSERT,
  SLEEPY_TOKEN_BREAK,
  SLEEPY_TOKEN_CALLCC,
  SLEEPY_TOKEN_CATCH,
  SLEEPY_TOKEN_CONTINUE,
  SLEEPY_TOKEN_DONE,
  SLEEPY_TOKEN_ELSE,
  SLEEPY_TOKEN_FOR,
  SLEEPY_TOKEN_FOREACH,
  SLEEPY_TOKEN_FROM,
  SLEEPY_TOKEN_HALT,
  SLEEPY_TOKEN_IF,
  SLEEPY_TOKEN_IMPORT,
  SLEEPY_TOKEN_RETURN,
  SLEEPY_TOKEN_THROW,
  SLEEPY_TOKEN_TRY,
  SLEEPY_TOKEN_WHILE,
  SLEEPY_TOKEN_YIELD,
  SLEEPY_TOKEN_INLINE,
  SLEEPY_TOKEN_SUB,
  SLEEPY_TOKEN_COPY,
  SLEEPY_TOKEN_ADDALL,
  SLEEPY_TOKEN_PRINTLN,
  SLEEPY_TOKEN_REMOVEALL,
  SLEEPY_TOKEN_RETAINALL,
  SLEEPY_TOKEN_LOCAL,
  SLEEPY_TOKEN_THIS,

  // Assignment Operators
  SLEEPY_TOKEN_ANDEQUAL,    // &=
  SLEEPY_TOKEN_CATEQUAL,    // .=
  SLEEPY_TOKEN_DIVEQUAL,    // /=
  SLEEPY_TOKEN_LSHIFTEQUAL, // <<=
  SLEEPY_TOKEN_MINUSEQUAL,  // -=
  SLEEPY_TOKEN_OREQUAL,     // |=
  SLEEPY_TOKEN_PLUSEQUAL,   // +=
  SLEEPY_TOKEN_RSHIFTEQUAL, // >>=
  SLEEPY_TOKEN_TIMESEQUAL,  // *=
  SLEEPY_TOKEN_XOREQUAL,    // ^=

  // Other multi-character tokens
  SLEEPY_TOKEN_ADDRESS,
  SLEEPY_TOKEN_ARG_PASSED_BY_NAME,
  SLEEPY_TOKEN_ARROW,         // =>
  SLEEPY_TOKEN_BACKTICK_EXPR, // `something`
  SLEEPY_TOKEN_CLASS_LITERAL, // ^Class
  SLEEPY_TOKEN_DEC,           // --
  SLEEPY_TOKEN_DOUBLE,
  SLEEPY_TOKEN_EOF,
  SLEEPY_TOKEN_EQ,       // ==
  SLEEPY_TOKEN_EQI,      // =~
  SLEEPY_TOKEN_EXP,      // **
  SLEEPY_TOKEN_EXPEQUAL, // **=
  SLEEPY_TOKEN_TRUE,     // true
  SLEEPY_TOKEN_FALSE,    // false
  SLEEPY_TOKEN_GE,       // >=
  SLEEPY_TOKEN_ID,
  SLEEPY_TOKEN_IMPORT_PATH,
  SLEEPY_TOKEN_INC,     // ++
  SLEEPY_TOKEN_LAND,    // &&
  SLEEPY_TOKEN_LE,      // <=
  SLEEPY_TOKEN_LITERAL, // 'something'
  SLEEPY_TOKEN_LONG,
  SLEEPY_TOKEN_LOR,    // ||
  SLEEPY_TOKEN_LSHIFT, // <<
  SLEEPY_TOKEN_NE,     // !=
  SLEEPY_TOKEN_NEQI,   // !=~
  SLEEPY_TOKEN_NULL,   // $null
  SLEEPY_TOKEN_NUMBER,
  SLEEPY_TOKEN_RSHIFT,    // >>
  SLEEPY_TOKEN_SCALAR,    // $var or $+
  SLEEPY_TOKEN_SPACESHIP, // <=>
  SLEEPY_TOKEN_STRING,    // "something"
  SLEEPY_TOKEN_UNARY_PREDICATE_BRIDGE,
  SLEEPY_TOKEN_BUILTIN_BINARY_PREDICATE_BRIDGE,
  SLEEPY_TOKEN_BIN_PRED, // For custom binary predicates

  SLEEPY_TOKEN_ERROR // Used for scanner errors
} SleepyTokenType;

typedef struct {
  SleepyTokenType type;
  const char *start;
  size_t length;
  int line;
} SleepyToken;

typedef struct {
  const char *start;
  const char *current;
  int line;
  SleepyAllocator *allocator;
} SleepyLexer;

void sleepy_lexer_init(SleepyLexer *lexer, const char *source,
                       SleepyAllocator *allocator);

SleepyToken sleepy_lexer_scan_token(SleepyLexer *lexer);
const char *sleepy_lexer_copy_lexeme(SleepyLexer *lexer, SleepyToken *token);

#endif // SLEEPY_LEXER_H
