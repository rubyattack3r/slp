#ifndef SLP_LEXER_H
#define SLP_LEXER_H

#include "slp_common.h"
#include "slp_core.h"

typedef enum {
  // Single-character and basic symbols
  SLP_TOKEN_BANG = 0,      // !
  SLP_TOKEN_DOT,           // .
  SLP_TOKEN_AMPERSAND,     // &
  SLP_TOKEN_AT,            // @
  SLP_TOKEN_COMMA,         // ,
  SLP_TOKEN_SLASH,         // /
  SLP_TOKEN_EQUAL,         // =
  SLP_TOKEN_PERCENT,       // %
  SLP_TOKEN_BACKSLASH,     // backslash
  SLP_TOKEN_MINUS,         // -
  SLP_TOKEN_PLUS,          // +
  SLP_TOKEN_LOWER_X,       // x
  SLP_TOKEN_STAR,          // *
  SLP_TOKEN_CARET,         // ^
  SLP_TOKEN_PIPE,          // |
  SLP_TOKEN_COLON,         // :
  SLP_TOKEN_SEMICOLON,     // ;
  SLP_TOKEN_RIGHT_BRACE,   // }
  SLP_TOKEN_LEFT_BRACE,    // {
  SLP_TOKEN_LEFT_BRACKET,  // [
  SLP_TOKEN_RIGHT_BRACKET, // ]
  SLP_TOKEN_LEFT_PAREN,    // (
  SLP_TOKEN_RIGHT_PAREN,   // )
  SLP_TOKEN_LESS,          // <
  SLP_TOKEN_GREATER,       // >

  // Keywords
  SLP_TOKEN_ASSERT,
  SLP_TOKEN_BREAK,
  SLP_TOKEN_CALLCC,
  SLP_TOKEN_CATCH,
  SLP_TOKEN_CONTINUE,
  SLP_TOKEN_DONE,
  SLP_TOKEN_ELSE,
  SLP_TOKEN_FOR,
  SLP_TOKEN_FOREACH,
  SLP_TOKEN_FROM,
  SLP_TOKEN_HALT,
  SLP_TOKEN_IF,
  SLP_TOKEN_IMPORT,
  SLP_TOKEN_RETURN,
  SLP_TOKEN_THROW,
  SLP_TOKEN_TRY,
  SLP_TOKEN_WHILE,
  SLP_TOKEN_YIELD,
  SLP_TOKEN_INLINE,
  SLP_TOKEN_SUB,
  SLP_TOKEN_COPY,
  SLP_TOKEN_ADDALL,
  SLP_TOKEN_PRINTLN,
  SLP_TOKEN_REMOVEALL,
  SLP_TOKEN_RETAINALL,
  SLP_TOKEN_LOCAL,
  SLP_TOKEN_THIS,

  // Assignment Operators
  SLP_TOKEN_ANDEQUAL,    // &=
  SLP_TOKEN_CATEQUAL,    // .=
  SLP_TOKEN_DIVEQUAL,    // /=
  SLP_TOKEN_LSHIFTEQUAL, // <<=
  SLP_TOKEN_MINUSEQUAL,  // -=
  SLP_TOKEN_OREQUAL,     // |=
  SLP_TOKEN_PLUSEQUAL,   // +=
  SLP_TOKEN_RSHIFTEQUAL, // >>=
  SLP_TOKEN_TIMESEQUAL,  // *=
  SLP_TOKEN_XOREQUAL,    // ^=

  // Other multi-character tokens
  SLP_TOKEN_ADDRESS,
  SLP_TOKEN_ARG_PASSED_BY_NAME,
  SLP_TOKEN_ARROW,         // =>
  SLP_TOKEN_BACKTICK_EXPR, // `something`
  SLP_TOKEN_CLASS_LITERAL, // ^Class
  SLP_TOKEN_DEC,           // --
  SLP_TOKEN_DOUBLE,
  SLP_TOKEN_EOF,
  SLP_TOKEN_EQ,       // ==
  SLP_TOKEN_EQI,      // =~
  SLP_TOKEN_EXP,      // **
  SLP_TOKEN_EXPEQUAL, // **=
  SLP_TOKEN_TRUE,     // true
  SLP_TOKEN_FALSE,    // false
  SLP_TOKEN_GE,       // >=
  SLP_TOKEN_ID,
  SLP_TOKEN_IMPORT_PATH,
  SLP_TOKEN_INC,     // ++
  SLP_TOKEN_LAND,    // &&
  SLP_TOKEN_LE,      // <=
  SLP_TOKEN_LITERAL, // 'something'
  SLP_TOKEN_LONG,
  SLP_TOKEN_LOR,    // ||
  SLP_TOKEN_LSHIFT, // <<
  SLP_TOKEN_NE,     // !=
  SLP_TOKEN_NEQI,   // !=~
  SLP_TOKEN_NULL,   // $null
  SLP_TOKEN_NUMBER,
  SLP_TOKEN_RSHIFT,    // >>
  SLP_TOKEN_SCALAR,    // $var or $+
  SLP_TOKEN_SPACESHIP, // <=>
  SLP_TOKEN_STRING,    // "something"
  SLP_TOKEN_UNARY_PREDICATE_BRIDGE,
  SLP_TOKEN_BUILTIN_BINARY_PREDICATE_BRIDGE,
  SLP_TOKEN_BIN_PRED, // For custom binary predicates

  SLP_TOKEN_ERROR // Used for scanner errors
} SlpTokenType;

typedef struct {
  SlpTokenType type;
  const char *start;
  size_t length;
  int line;
} SlpToken;

typedef struct {
  const char *start;
  const char *current;
  int line;
  SlpAllocator *allocator;
} SlpLexer;

void slp_lexer_init(SlpLexer *lexer, const char *source,
                       SlpAllocator *allocator);

SlpToken slp_lexer_scan_token(SlpLexer *lexer);
const char *slp_lexer_copy_lexeme(SlpLexer *lexer, SlpToken *token);

#endif // SLP_LEXER_H
