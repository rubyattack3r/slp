/* 
 * Sleepy Amalgamated Header
 * Generated automatically.
 */
#ifndef SLEEPY_H
#define SLEEPY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* --- File: include/sleepy_common.h --- */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Includes for common types used across the VM
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ----------------------------------------------------------------------------
// COMMON MACROS
// ----------------------------------------------------------------------------

#define SLEEPY_MALLOC(allocator, size)                                         \
  ((allocator)->reallocate(NULL, (size), (allocator)->user_data))

#define SLEEPY_REALLOC(allocator, ptr, new_size)                               \
  ((allocator)->reallocate((ptr), (new_size), (allocator)->user_data))

#define SLEEPY_FREE(allocator, ptr)                                            \
  ((allocator)->reallocate((ptr), 0, (allocator)->user_data))

#define SLEEPY_ALLOCATE_ARRAY(allocator, type, count)                          \
  ((type *)SLEEPY_MALLOC(allocator, sizeof(type) * (count)))

#define SLEEPY_ALLOCATE(allocator, type)                                       \
  ((type *)SLEEPY_MALLOC(allocator, sizeof(type)))

#define SLEEPY_FREE_ARRAY(allocator, type, pointer, count)                     \
  SLEEPY_FREE(allocator, pointer)


/* --- File: include/sleepy_core.h --- */


// A generic allocation function. It should act like `realloc`.
// If `ptr` is NULL and `new_size` > 0, it behaves like `malloc`.
// If `new_size` is 0, it behaves like `free` and should return NULL.
// Otherwise, it resizes the memory block to `new_size`.
typedef void *(*SleepyReallocateFn)(void *ptr, size_t new_size,
                                    void *user_data);

typedef struct {
  SleepyReallocateFn reallocate;
  void *user_data;
} SleepyAllocator;


/* --- File: include/sleepy_utils.h --- */


// Reusable dynamic array of bytes/strings (like Wren's StringBuffer)
typedef struct {
  char *data;
  size_t length;
  size_t capacity;
  SleepyAllocator *allocator;
} SleepyStringBuffer;

void sleepy_string_buffer_init(SleepyStringBuffer *buffer,
                               SleepyAllocator *allocator);
void sleepy_string_buffer_clear(SleepyStringBuffer *buffer);
void sleepy_string_buffer_free(SleepyStringBuffer *buffer);
void sleepy_string_buffer_append_char(SleepyStringBuffer *buffer, char c);
void sleepy_string_buffer_append_string(SleepyStringBuffer *buffer,
                                        const char *str, size_t length);

// Memory and string utilities to avoid libc dependencies
void *sleepy_utils_memcpy(void *dest, const void *src, size_t n);
size_t sleepy_utils_strlen(const char *s);
char *sleepy_utils_strcpy(char *dest, const char *src);


/* --- File: include/sleepy_lexer.h --- */


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


/* --- File: include/sleepy_parser.h --- */


// Forward declaration of an AST node
typedef struct SleepyASTNode SleepyASTNode;

// Node types matching sleepy python AST classes
typedef enum {
  SLEEPY_AST_SCRIPT,
  SLEEPY_AST_BLOCK,
  SLEEPY_AST_BOOLEAN,
  SLEEPY_AST_NULL,
  SLEEPY_AST_LONG,
  SLEEPY_AST_LITERAL,
  SLEEPY_AST_STRING,
  SLEEPY_AST_NUMBER,
  SLEEPY_AST_SCALAR,
  SLEEPY_AST_ARRAY,
  SLEEPY_AST_HASHTABLE,
  SLEEPY_AST_IDENTIFIER,
  SLEEPY_AST_CLASS_LITERAL,
  SLEEPY_AST_BACKTICK,
  SLEEPY_AST_ADDRESS,
  SLEEPY_AST_CALL,
  SLEEPY_AST_ASSIGNMENT,
  SLEEPY_AST_BINOP,
  SLEEPY_AST_UNARYOP,
  SLEEPY_AST_IF,
  SLEEPY_AST_WHILE,
  SLEEPY_AST_FOR,
  SLEEPY_AST_FOREACH,
  SLEEPY_AST_RETURN,
  SLEEPY_AST_BREAK,
  SLEEPY_AST_CONTINUE,
  SLEEPY_AST_YIELD,
  SLEEPY_AST_ASSIGN_LOOP,
  SLEEPY_AST_INDEX,
  SLEEPY_AST_OBJ_EXPR,
  SLEEPY_AST_TRY_CATCH,
  SLEEPY_AST_THROW,
  SLEEPY_AST_ASSERT,
  SLEEPY_AST_ENV_BRIDGE,
  SLEEPY_AST_IMPORT,
  SLEEPY_AST_LVALUE_TUPLE,
  SLEEPY_AST_KV_PAIR,
  SLEEPY_AST_ARG,
  SLEEPY_AST_HALT,
  SLEEPY_AST_DONE,
  SLEEPY_AST_CALLCC,
  SLEEPY_AST_LOCAL,
  SLEEPY_AST_THIS,
  SLEEPY_AST_NOP,
  SLEEPY_AST_ERROR // For parse error nodes
} SleepyASTNodeType;

struct SleepyASTNode {
  SleepyASTNodeType type;
  int line; // Line number for error reporting

  // A simple discriminated union for node-specific data
  union {
    // Values and Literals
    bool boolean;
    long long_val;
    double double_val;
    const char *string_val; // Shared for string, literal, scalar, array,
                            // hashtable, ID, class.

    // Assignment ($a = 5)
    struct {
      struct SleepyASTNode *left;
      SleepyToken op;
      struct SleepyASTNode *right;
    } assign;

    // Binary Operation (and assignments)
    struct {
      struct SleepyASTNode *left;
      SleepyToken op;
      struct SleepyASTNode *right;
      bool negate;
    } binop;

    // Unary Operation
    struct {
      SleepyToken op;
      struct SleepyASTNode *operand;
    } unaryop;

    // List of statements (Block, Script, LvalueTuple)
    struct {
      struct SleepyASTNode **statements;
      size_t count;
      size_t capacity;
    } block;

    // If statement
    struct {
      struct SleepyASTNode *condition;
      struct SleepyASTNode *then_branch;
      struct SleepyASTNode *else_branch;
    } if_stmt;

    // While statement / Foreach generator
    struct {
      struct SleepyASTNode *condition;
      struct SleepyASTNode *body;
    } while_stmt;

    // For statement
    struct {
      struct SleepyASTNode **initializer;
      size_t init_count;
      struct SleepyASTNode *condition;
      struct SleepyASTNode **increment;
      size_t inc_count;
      struct SleepyASTNode *body;
    } for_stmt;

    // Foreach
    struct {
      const char *index; // Nullable
      const char *value;
      struct SleepyASTNode *generator;
      struct SleepyASTNode *body;
    } foreach;

    // Assign Loop (while scalar (expr) block)
    struct {
      const char *value;
      struct SleepyASTNode *generator;
      struct SleepyASTNode *body;
    } assign_loop;

    // Function Call
    struct {
      struct SleepyASTNode *target;
      struct SleepyASTNode **args; // Array of AST_ARG
      size_t arg_count;
    } call;

    // Arguments & KV Pairs
    struct {
      struct SleepyASTNode *name;  // KV Pair / Optional Arg name
      struct SleepyASTNode *value; // Value expressions
      bool trailing_sep;
    } arg;

    // Index expression
    struct {
      struct SleepyASTNode *container;
      struct SleepyASTNode *element;
    } index;

    // Object Expression [target message: args]
    struct {
      struct SleepyASTNode *target;
      struct SleepyASTNode *message; // usually string or identifier
      struct SleepyASTNode **args;
      size_t arg_count;
    } obj_expr;

    // Environment Bridge (keyword identifier string block)
    struct {
      const char *keyword;
      const char *identifier;
      const char *string;
      struct SleepyASTNode *body;
    } env_bridge;

    // Try / Catch
    struct {
      struct SleepyASTNode *body;
      const char *value; // Exception variable (e.g., $ex)
      struct SleepyASTNode *handler;
    } try_catch;

    // Returns, Yields, Throws, Asserts
    struct {
      struct SleepyASTNode *value;
    } control;

    // Import
    struct {
      const char *target;
      const char *path;
    } import_stmt;

  } as;
};

typedef struct {
  SleepyLexer lexer;
  SleepyToken current;
  SleepyToken previous;
  bool had_error;
  bool panic_mode;
  SleepyAllocator *allocator;
} SleepyParser;

void sleepy_parser_init(SleepyParser *parser, const char *source,
                        SleepyAllocator *allocator);

// Given a parser initialized on some source code, parse it.
// Returns a top-level AST node or NULL if it failed.
SleepyASTNode *sleepy_parser_parse(SleepyParser *parser);


#endif // SLEEPY_H
