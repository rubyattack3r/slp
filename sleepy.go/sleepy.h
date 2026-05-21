/*
 * Sleepy Amalgamated Header
 * Generated automatically.
 */
#ifndef SLEEPY_H
#define SLEEPY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* --- File: ../include/sleepy_common.h --- */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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


/* --- File: ../include/sleepy_core.h --- */


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


/* --- File: ../include/sleepy_utils.h --- */


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


/* --- File: ../include/sleepy_lexer.h --- */


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


/* --- File: ../include/sleepy_parser.h --- */


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
  int error_line;
  const char *error_message;
  SleepyAllocator *allocator;
} SleepyParser;

void sleepy_parser_init(SleepyParser *parser, const char *source,
                        SleepyAllocator *allocator);

// Given a parser initialized on some source code, parse it.
// Returns a top-level AST node or NULL if it failed.
SleepyASTNode *sleepy_parser_parse(SleepyParser *parser);


/* --- File: ../include/sleepy_ast.h --- */


// Formats a Sleepy AST Node back into Sleepy source code natively.
// Returns a dynamically allocated string containing the code.
// The caller is responsible for freeing the resulting string using
// allocator->reallocate(ptr, 0).
char *sleepy_ast_format(SleepyASTNode *node, SleepyAllocator *allocator);

// AST Helper Functions for Bindings
void sleepy_ast_get_children(SleepyASTNode *node, SleepyASTNode ***out_children,
                             size_t *out_count, SleepyAllocator *allocator);
void sleepy_ast_free_children(SleepyASTNode **children,
                              SleepyAllocator *allocator);
const char *sleepy_ast_get_string(SleepyASTNode *node);
const char *sleepy_ast_get_op(SleepyASTNode *node);
size_t sleepy_ast_get_op_length(SleepyASTNode *node);
long sleepy_ast_get_long(SleepyASTNode *node);
double sleepy_ast_get_double(SleepyASTNode *node);
bool sleepy_ast_get_bool(SleepyASTNode *node);

// Bridge specific getters
const char *sleepy_ast_get_env_bridge_id(SleepyASTNode *node);
const char *sleepy_ast_get_env_bridge_string(SleepyASTNode *node);
const char *sleepy_ast_get_foreach_index(SleepyASTNode *node);
const char *sleepy_ast_get_foreach_value(SleepyASTNode *node);
const char *sleepy_ast_get_import_path(SleepyASTNode *node);
const char *sleepy_ast_get_try_catch_var(SleepyASTNode *node);
size_t sleepy_ast_get_for_init_count(SleepyASTNode *node);
size_t sleepy_ast_get_for_inc_count(SleepyASTNode *node);

// AST Builder APIs for FFI
SleepyASTNode *sleepy_ast_build_node(SleepyASTNodeType type, int line,
                                     SleepyAllocator *allocator);
void sleepy_ast_set_string_val(SleepyASTNode *node, const char *str,
                               SleepyAllocator *allocator);
void sleepy_ast_set_long_val(SleepyASTNode *node, long val);
void sleepy_ast_set_double_val(SleepyASTNode *node, double val);
void sleepy_ast_set_bool_val(SleepyASTNode *node, bool val);

// List/Array Setters
SleepyASTNode **sleepy_ast_allocate_children(size_t count,
                                             SleepyAllocator *allocator);
void sleepy_ast_set_children(SleepyASTNode *node, SleepyASTNode **children,
                             size_t count, SleepyAllocator *allocator);

// Complex Setters
void sleepy_ast_set_binop(SleepyASTNode *node, SleepyASTNode *left,
                          SleepyASTNode *right);
void sleepy_ast_set_binop_with_op(SleepyASTNode *node, SleepyASTNode *left,
                                  SleepyASTNode *right, const char *op,
                                  SleepyAllocator *allocator);
void sleepy_ast_set_unaryop_with_op(SleepyASTNode *node, SleepyASTNode *operand,
                                    const char *op, SleepyAllocator *allocator);
void sleepy_ast_set_if(SleepyASTNode *node, SleepyASTNode *condition,
                       SleepyASTNode *then_branch, SleepyASTNode *else_branch);
void sleepy_ast_set_while(SleepyASTNode *node, SleepyASTNode *condition,
                          SleepyASTNode *body);
void sleepy_ast_set_arg(SleepyASTNode *node, SleepyASTNode *value);
void sleepy_ast_set_arg_with_name(SleepyASTNode *node, SleepyASTNode *name, SleepyASTNode *value);
void sleepy_ast_set_assignment(SleepyASTNode *node, SleepyASTNode *target,
                               SleepyASTNode *value);
void sleepy_ast_set_obj_expr(SleepyASTNode *node, SleepyASTNode *target,
                             SleepyASTNode *message, SleepyASTNode **args,
                             size_t arg_count, SleepyAllocator *allocator);
void sleepy_ast_set_env_bridge(SleepyASTNode *node, const char *id,
                               const char *str, SleepyASTNode **children,
                               size_t count, SleepyAllocator *allocator);
void sleepy_ast_set_env_bridge_keyword(SleepyASTNode *node, const char *keyword,
                                       SleepyAllocator *allocator);
void sleepy_ast_set_env_bridge_id(SleepyASTNode *node, const char *id,
                                  SleepyAllocator *allocator);
void sleepy_ast_set_env_bridge_string(SleepyASTNode *node, const char *str,
                                      SleepyAllocator *allocator);
void sleepy_ast_set_env_bridge_body(SleepyASTNode *node, SleepyASTNode *body);
void sleepy_ast_set_call_target(SleepyASTNode *node, const char *target,
                                SleepyAllocator *allocator);
void sleepy_ast_set_foreach(SleepyASTNode *node, const char *index,
                            const char *value, SleepyASTNode *generator,
                            SleepyASTNode *body, SleepyAllocator *allocator);
void sleepy_ast_set_for(SleepyASTNode *node, SleepyASTNode **init,
                        size_t init_cnt, SleepyASTNode *cond,
                        SleepyASTNode **inc, size_t inc_cnt,
                        SleepyASTNode *body, SleepyAllocator *allocator);
void sleepy_ast_set_return(SleepyASTNode *node, SleepyASTNode *value);
void sleepy_ast_set_break(SleepyASTNode *node);
void sleepy_ast_set_continue(SleepyASTNode *node);
void sleepy_ast_set_yield(SleepyASTNode *node, SleepyASTNode *value);
void sleepy_ast_set_throw(SleepyASTNode *node, SleepyASTNode *value);
void sleepy_ast_set_assert(SleepyASTNode *node, SleepyASTNode *value);
void sleepy_ast_set_try_catch(SleepyASTNode *node, SleepyASTNode *body,
                              const char *var, SleepyASTNode *handler,
                              SleepyAllocator *allocator);
void sleepy_ast_set_import(SleepyASTNode *node, const char *path,
                           SleepyAllocator *allocator);
void sleepy_ast_set_backtick(SleepyASTNode *node, const char *cmd,
                             SleepyAllocator *allocator);

// Memory Management
void sleepy_parser_free_node(SleepyASTNode *node, SleepyAllocator *allocator);

void sleepy_ast_set_index(SleepyASTNode *node, SleepyASTNode *container, SleepyASTNode *element);
void sleepy_ast_set_kv_pair(SleepyASTNode *node, SleepyASTNode *name, SleepyASTNode *value);


/* --- File: ../include/sleepy_opcodes.h --- */

typedef enum {
    OP_PUSH_NULL,
    OP_PUSH_TRUE,
    OP_PUSH_FALSE,
    OP_PUSH_CONST,

    OP_LOAD_LOCAL_0,
    OP_LOAD_LOCAL_1,
    OP_LOAD_LOCAL_2,
    OP_LOAD_LOCAL_3,
    OP_LOAD_LOCAL_4,
    OP_LOAD_LOCAL_5,
    OP_LOAD_LOCAL_6,
    OP_LOAD_LOCAL_7,

    OP_STORE_LOCAL_0,
    OP_STORE_LOCAL_1,
    OP_STORE_LOCAL_2,
    OP_STORE_LOCAL_3,
    OP_STORE_LOCAL_4,
    OP_STORE_LOCAL_5,
    OP_STORE_LOCAL_6,
    OP_STORE_LOCAL_7,

    OP_LOAD_LOCAL,
    OP_STORE_LOCAL,
    OP_LOAD_GLOBAL,
    OP_STORE_GLOBAL,
    OP_LOAD_UPVALUE,
    OP_STORE_UPVALUE,

    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MODULO,
    OP_POWER,
    OP_NEGATE,

    OP_CONCAT,
    OP_REPEAT,

    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_LESS,
    OP_GREATER,
    OP_LESS_EQUAL,
    OP_GREATER_EQUAL,
    OP_SPACESHIP,

    OP_MATCH,
    OP_NOT_MATCH,

    OP_NOT,
    OP_AND,
    OP_OR,

    OP_BIT_AND,
    OP_BIT_OR,
    OP_BIT_XOR,
    OP_BIT_NOT,
    OP_LSHIFT,
    OP_RSHIFT,

    OP_INCREMENT,
    OP_DECREMENT,

    OP_UNARY_PREDICATE,
    OP_BINARY_PREDICATE,
    OP_NEGATED_BINARY_PREDICATE,

    OP_POP,
    OP_DUP,

    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    OP_LOOP,

    OP_CALL,
    OP_RETURN,
    OP_TAILCALL,

    OP_CLOSURE,
    OP_CLOSE_UPVALUE,

    OP_FOREACH_NEXT,

    OP_BUILD_ARRAY,
    OP_BUILD_HASH,

    OP_INDEX_GET,
    OP_INDEX_SET,

    OP_OBJ_EXPR,

    OP_PUSH_HANDLER,
    OP_POP_HANDLER,
    OP_THROW,

    OP_ASSERT,
    OP_HALT,
    OP_DONE,
    OP_YIELD,
    OP_CALLCC,

    OP_RESUME,

    OP_BRIDGE_REGISTER,

    OP_IMPORT,

    OP_BACKTICK,
    OP_CLASS_LITERAL,
    OP_ADDRESS,

    OP_LOCAL,
    OP_THIS,

    OP_UNPACK_TUPLE,

    OP_BREAK,
    OP_CONTINUE,

    OP_NOP,

    OP_END
} SleepyOpcode;


/* --- File: ../include/sleepy_value.h --- */

#include <stdint.h>

#ifdef SLEEPY_NAN_TAGGING

#define SLEEPY_SIGN_BIT    ((uint64_t)1 << 63)
#define SLEEPY_QNAN        ((uint64_t)0x7ffc000000000000)
#define SLEEPY_TAG_NULL    1
#define SLEEPY_TAG_FALSE   2
#define SLEEPY_TAG_TRUE    3
#define SLEEPY_TAG_NAN     4

#define SLEEPY_IS_NUM(v)   (((v) & SLEEPY_QNAN) != SLEEPY_QNAN)
#define SLEEPY_IS_OBJ(v)   (((v) & (SLEEPY_QNAN | SLEEPY_SIGN_BIT)) == (SLEEPY_QNAN | SLEEPY_SIGN_BIT))
#define SLEEPY_IS_BOOL(v)  (((v) | 3) == (SLEEPY_QNAN | 3))
#define SLEEPY_IS_NULL(v)  ((v) == SLEEPY_NULL_VAL)
#define SLEEPY_IS_TRUE(v)  ((v) == SLEEPY_TRUE_VAL)
#define SLEEPY_IS_FALSE(v) ((v) == SLEEPY_FALSE_VAL)

#define SLEEPY_AS_NUM(v)   sleepy_value_to_double(v)
#define SLEEPY_AS_OBJ(v)   ((SleepyObj*)(uintptr_t)((v) & ~(SLEEPY_QNAN | SLEEPY_SIGN_BIT)))
#define SLEEPY_AS_BOOL(v)  ((v) == SLEEPY_TRUE_VAL)

#define SLEEPY_NUM_VAL(n)  sleepy_double_to_value(n)
#define SLEEPY_OBJ_VAL(o)  (SLEEPY_QNAN | SLEEPY_SIGN_BIT | (uint64_t)(uintptr_t)(o))
#define SLEEPY_BOOL_VAL(b) ((b) ? SLEEPY_TRUE_VAL : SLEEPY_FALSE_VAL)

#define SLEEPY_NULL_VAL    (SLEEPY_QNAN | SLEEPY_TAG_NULL)
#define SLEEPY_TRUE_VAL    (SLEEPY_QNAN | SLEEPY_TAG_TRUE)
#define SLEEPY_FALSE_VAL   (SLEEPY_QNAN | SLEEPY_TAG_FALSE)

typedef uint64_t SleepyValue;

static inline double sleepy_value_to_double(SleepyValue v) {
    double d;
    sleepy_utils_memcpy(&d, &v, sizeof(double));
    return d;
}

static inline SleepyValue sleepy_double_to_value(double d) {
    SleepyValue v;
    sleepy_utils_memcpy(&v, &d, sizeof(SleepyValue));
    return v;
}

#else

typedef enum {
    SLEEPY_VAL_NULL,
    SLEEPY_VAL_BOOL,
    SLEEPY_VAL_NUM,
    SLEEPY_VAL_OBJ
} SleepyValueType;

typedef struct {
    SleepyValueType type;
    union {
        bool boolean;
        double num;
        struct SleepyObj *obj;
    } as;
} SleepyValue;

#define SLEEPY_IS_NUM(v)   ((v).type == SLEEPY_VAL_NUM)
#define SLEEPY_IS_OBJ(v)   ((v).type == SLEEPY_VAL_OBJ)
#define SLEEPY_IS_BOOL(v)  ((v).type == SLEEPY_VAL_BOOL)
#define SLEEPY_IS_NULL(v)  ((v).type == SLEEPY_VAL_NULL)
#define SLEEPY_IS_TRUE(v)  ((v).type == SLEEPY_VAL_BOOL && (v).as.boolean)
#define SLEEPY_IS_FALSE(v) ((v).type == SLEEPY_VAL_BOOL && !(v).as.boolean)

#define SLEEPY_AS_NUM(v)   ((v).as.num)
#define SLEEPY_AS_OBJ(v)   ((v).as.obj)
#define SLEEPY_AS_BOOL(v)  ((v).as.boolean)

#define SLEEPY_NUM_VAL(n)   ((SleepyValue){SLEEPY_VAL_NUM, {.num = (n)}})
#define SLEEPY_OBJ_VAL(o)   ((SleepyValue){SLEEPY_VAL_OBJ, {.obj = (struct SleepyObj*)(o)}})
#define SLEEPY_BOOL_VAL(b)  ((SleepyValue){SLEEPY_VAL_BOOL, {.boolean = (b)}})
#define SLEEPY_NULL_VAL     ((SleepyValue){SLEEPY_VAL_NULL, {.num = 0}})
#define SLEEPY_TRUE_VAL     SLEEPY_BOOL_VAL(true)
#define SLEEPY_FALSE_VAL    SLEEPY_BOOL_VAL(false)

#endif

typedef enum {
    SLEEPY_OBJ_STRING,
    SLEEPY_OBJ_LONG,
    SLEEPY_OBJ_ARRAY,
    SLEEPY_OBJ_HASH,
    SLEEPY_OBJ_FUNCTION,
    SLEEPY_OBJ_CLOSURE,
    SLEEPY_OBJ_UPVALUE,
    SLEEPY_OBJ_CONTINUATION,
    SLEEPY_OBJ_NATIVE,
    SLEEPY_OBJ_BRIDGE
} SleepyObjType;

typedef struct SleepyObj {
    SleepyObjType type;
    bool is_marked;
    struct SleepyObj *next;
} SleepyObj;

typedef struct {
    SleepyObj obj;
    uint32_t length;
    uint32_t hash;
    char chars[];
} SleepyObjString;

typedef struct {
    SleepyObj obj;
    int64_t value;
} SleepyObjLong;

typedef struct SleepyChunk SleepyChunk;

typedef struct {
    SleepyObj obj;
    int arity;
    int upvalue_count;
    SleepyChunk *chunk;
    SleepyObjString *name;
} SleepyObjFunction;

typedef struct SleepyObjUpvalue SleepyObjUpvalue;

typedef struct {
    SleepyObj obj;
    SleepyObjFunction *function;
    SleepyObjUpvalue **upvalues;
} SleepyObjClosure;

struct SleepyObjUpvalue {
    SleepyObj obj;
    SleepyValue *location;
    SleepyValue closed;
    SleepyObjUpvalue *next;
};

typedef struct {
    SleepyObj obj;
    SleepyValue *elements;
    int count;
    int capacity;
} SleepyObjArray;

typedef struct {
    SleepyValue key;
    SleepyValue value;
} SleepyHashEntry;

typedef struct {
    SleepyObj obj;
    SleepyHashEntry *entries;
    int capacity;
    int count;
} SleepyObjHash;

typedef struct SleepyCallFrame SleepyCallFrame;

typedef struct {
    SleepyObj obj;
    SleepyCallFrame *frames;
    int frame_count;
    SleepyValue *stack;
    int stack_count;
    uint8_t *saved_ip;
} SleepyObjContinuation;

typedef struct SleepyVM SleepyVM;
typedef SleepyValue (*SleepyNativeFn)(SleepyVM *vm, SleepyValue *args, int argc);

typedef struct {
    SleepyObj obj;
    SleepyNativeFn fn;
    SleepyObjString *name;
} SleepyObjNative;

typedef struct {
    SleepyObj obj;
    SleepyObjString *keyword;
    SleepyObjString *name;
    SleepyObjClosure *closure;
} SleepyObjBridge;

#define SLEEPY_OBJ_TYPE(v) (SLEEPY_AS_OBJ(v)->type)

#define SLEEPY_AS_STRING(v)    ((SleepyObjString*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_LONG(v)      ((SleepyObjLong*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_ARRAY(v)     ((SleepyObjArray*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_HASH(v)      ((SleepyObjHash*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_FUNCTION(v)  ((SleepyObjFunction*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_CLOSURE(v)   ((SleepyObjClosure*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_UPVALUE(v)   ((SleepyObjUpvalue*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_NATIVE(v)    ((SleepyObjNative*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_BRIDGE(v)    ((SleepyObjBridge*)SLEEPY_AS_OBJ(v))
#define SLEEPY_AS_CONTINUATION(v) ((SleepyObjContinuation*)SLEEPY_AS_OBJ(v))

bool sleepy_value_is_falsy(SleepyValue v);
bool sleepy_value_equals(SleepyValue a, SleepyValue b);
SleepyObjString *sleepy_obj_string_new(SleepyAllocator *alloc, const char *chars, uint32_t length);
SleepyObjString *sleepy_obj_string_copy(SleepyAllocator *alloc, const char *chars, uint32_t length);
SleepyObjLong *sleepy_obj_long_new(SleepyAllocator *alloc, int64_t value);
SleepyObjArray *sleepy_obj_array_new(SleepyAllocator *alloc);
SleepyObjHash *sleepy_obj_hash_new(SleepyAllocator *alloc);
SleepyObjFunction *sleepy_obj_function_new(SleepyAllocator *alloc);
SleepyObjClosure *sleepy_obj_closure_new(SleepyAllocator *alloc, SleepyObjFunction *fn);
SleepyObjUpvalue *sleepy_obj_upvalue_new(SleepyAllocator *alloc, SleepyValue *slot);
SleepyObjNative *sleepy_obj_native_new(SleepyAllocator *alloc, SleepyNativeFn fn, SleepyObjString *name);
SleepyObjBridge *sleepy_obj_bridge_new(SleepyAllocator *alloc, SleepyObjString *keyword, SleepyObjString *name, SleepyObjClosure *closure);
SleepyObjContinuation *sleepy_obj_continuation_new(SleepyAllocator *alloc);

void sleepy_obj_array_push(SleepyAllocator *alloc, SleepyObjArray *arr, SleepyValue val);
SleepyValue sleepy_obj_array_pop(SleepyObjArray *arr);
SleepyValue sleepy_obj_array_get(SleepyObjArray *arr, int index);
void sleepy_obj_array_set(SleepyAllocator *alloc, SleepyObjArray *arr, int index, SleepyValue val);

bool sleepy_obj_hash_set(SleepyAllocator *alloc, SleepyObjHash *hash, SleepyValue key, SleepyValue value);
SleepyValue sleepy_obj_hash_get(SleepyObjHash *hash, SleepyValue key);
bool sleepy_obj_hash_delete(SleepyAllocator *alloc, SleepyObjHash *hash, SleepyValue key);

uint32_t sleepy_hash_string(const char *key, uint32_t length);
uint32_t sleepy_hash_value(SleepyValue v);
SleepyObjString *sleepy_find_interned_string(SleepyObj *head, const char *chars, uint32_t length, uint32_t hash);


/* --- File: ../include/sleepy_chunk.h --- */


struct SleepyChunk {
    SleepyAllocator *allocator;
    uint8_t *code;
    int *lines;
    int count;
    int capacity;
    SleepyValue *constants;
    int constant_count;
    int constant_capacity;
};

void sleepy_chunk_init(SleepyChunk *chunk, SleepyAllocator *allocator);
void sleepy_chunk_free(SleepyChunk *chunk);
int sleepy_chunk_write(SleepyChunk *chunk, uint8_t byte, int line);
int sleepy_chunk_add_constant(SleepyChunk *chunk, SleepyValue value);
uint16_t sleepy_chunk_read_short(SleepyChunk *chunk, int offset);
void sleepy_chunk_write_short(SleepyChunk *chunk, uint16_t val, int line);


/* --- File: ../include/sleepy_gc.h --- */


void sleepy_gc_init(SleepyVM *vm);
void sleepy_gc_free(SleepyVM *vm);
void sleepy_gc_collect(SleepyVM *vm);
void sleepy_gc_mark_obj(SleepyVM *vm, SleepyObj *obj);
void sleepy_gc_mark_value(SleepyVM *vm, SleepyValue value);
void sleepy_gc_sweep(SleepyVM *vm);
SleepyObj *sleepy_gc_allocate_object(SleepyVM *vm, size_t size, SleepyObjType type);
size_t sleepy_gc_object_size(SleepyObj *obj);


/* --- File: ../include/sleepy_vm.h --- */


#define SLEEPY_STACK_MAX   4096
#define SLEEPY_MAX_FRAMES  512
#define SLEEPY_MAX_HANDLERS 256
#define SLEEPY_INTERN_INIT_CAP 256

struct SleepyCallFrame {
    uint8_t *ip;
    SleepyObjClosure *closure;
    SleepyValue *slots;
};

struct SleepyTryHandler {
    uint8_t *catch_ip;
    int frame_count;
};

typedef struct SleepyTryHandler SleepyTryHandler;

typedef void (*SleepyVMErrorFn)(void *ud, int line, const char *msg);
typedef void (*SleepyVMWriteFn)(void *ud, const char *text);

typedef struct SleepyBridgeType SleepyBridgeType;

struct SleepyBridgeType {
    char *keyword;
    void (*handler)(SleepyVM *vm, const char *keyword, const char *identifier,
                    const char *string_arg, SleepyObjClosure *closure,
                    void *userdata);
    void *userdata;
    SleepyBridgeType *next;
};

struct SleepyVM {
    SleepyAllocator *allocator;

    SleepyValue stack[SLEEPY_STACK_MAX];
    SleepyValue *stack_top;

    SleepyCallFrame frames[SLEEPY_MAX_FRAMES];
    int frame_count;

    SleepyObjHash *globals;

    SleepyObj *objects;

    SleepyObjString **interned;
    int interned_count;
    int interned_capacity;

    SleepyObjUpvalue *open_upvalues;

    SleepyBridgeType *bridge_types;
    SleepyObjHash *natives;

    size_t bytes_allocated;
    size_t next_gc_threshold;

    SleepyObj **gc_gray_stack;
    int gc_gray_count;
    int gc_gray_capacity;

    SleepyVMErrorFn error_fn;
    void *error_userdata;
    SleepyVMWriteFn write_fn;
    void *write_userdata;

    SleepyValue ffi_slots[256];

    bool halted;
    SleepyValue thrown_exception;

    SleepyTryHandler try_handlers[SLEEPY_MAX_HANDLERS];
    int try_handler_count;
};

typedef enum {
    SLEEPY_OK,
    SLEEPY_COMPILE_ERROR,
    SLEEPY_RUNTIME_ERROR,
    SLEEPY_HALT
} SleepyResult;

SleepyVM *sleepy_vm_new(SleepyAllocator *allocator);
void sleepy_vm_free(SleepyVM *vm);

SleepyResult sleepy_vm_interpret(SleepyVM *vm, const char *source);
SleepyResult sleepy_vm_run(SleepyVM *vm, SleepyObjFunction *fn);

void sleepy_vm_push(SleepyVM *vm, SleepyValue value);
SleepyValue sleepy_vm_pop(SleepyVM *vm);
SleepyValue sleepy_vm_peek(SleepyVM *vm, int distance);

SleepyObjString *sleepy_vm_intern_string(SleepyVM *vm, const char *chars, uint32_t length);
SleepyObjString *sleepy_vm_copy_string(SleepyVM *vm, const char *chars, uint32_t length);

void sleepy_vm_register_bridge_type(SleepyVM *vm, const char *keyword,
    void (*handler)(SleepyVM*, const char*, const char*, const char*,
                    SleepyObjClosure*, void*),
    void *userdata);
void sleepy_vm_register_native(SleepyVM *vm, const char *name, SleepyNativeFn fn);

void sleepy_vm_set_error_fn(SleepyVM *vm, SleepyVMErrorFn fn, void *ud);
void sleepy_vm_set_write_fn(SleepyVM *vm, SleepyVMWriteFn fn, void *ud);

void sleepy_vm_ffi_set_null(SleepyVM *vm, int slot);
void sleepy_vm_ffi_set_bool(SleepyVM *vm, int slot, bool val);
void sleepy_vm_ffi_set_number(SleepyVM *vm, int slot, double val);
void sleepy_vm_ffi_set_long(SleepyVM *vm, int slot, int64_t val);
void sleepy_vm_ffi_set_string(SleepyVM *vm, int slot, const char *val);

bool sleepy_vm_ffi_is_null(SleepyVM *vm, int slot);
bool sleepy_vm_ffi_is_bool(SleepyVM *vm, int slot);
bool sleepy_vm_ffi_is_number(SleepyVM *vm, int slot);
bool sleepy_vm_ffi_is_string(SleepyVM *vm, int slot);
bool sleepy_vm_ffi_get_bool(SleepyVM *vm, int slot);
double sleepy_vm_ffi_get_number(SleepyVM *vm, int slot);
int64_t sleepy_vm_ffi_get_long(SleepyVM *vm, int slot);
const char *sleepy_vm_ffi_get_string(SleepyVM *vm, int slot);

void sleepy_vm_runtime_error(SleepyVM *vm, const char *msg);


/* --- File: ../include/sleepy_compiler.h --- */


typedef struct {
    SleepyObjString *name;
    int depth;
    bool is_captured;
} SleepyLocal;

typedef struct {
    uint8_t index;
    bool is_local;
} SleepyCompilerUpvalue;

typedef struct SleepyCompiler {
    struct SleepyCompiler *enclosing;

    SleepyObjFunction *function;
    SleepyVM *vm;
    SleepyAllocator *allocator;

    SleepyLocal locals[256];
    int local_count;
    int scope_depth;

    SleepyCompilerUpvalue upvalues[256];
    int upvalue_count;

    int loop_start;
    int loop_continue_target;
    int loop_exit_jump;
    int loop_scope_depth;
    int break_jumps[256];
    int break_jump_count;
    int continue_jumps[256];
    int continue_jump_count;

    bool had_error;
    int error_line;
    const char *error_message;
} SleepyCompiler;

SleepyObjFunction *sleepy_compile(SleepyVM *vm, SleepyASTNode *ast, SleepyAllocator *allocator);


/* --- File: ../include/sleepy_disasm.h --- */

#include <stdio.h>

const char *sleepy_opcode_name(SleepyOpcode op);
int sleepy_disasm_instruction(SleepyChunk *chunk, int offset, FILE *out);
void sleepy_disasm_chunk(SleepyChunk *chunk, const char *name, FILE *out);


/* --- File: ../include/sleepy_bytecode.h --- */


#define SLEEPY_BYTECODE_MAGIC   0x534C5950
#define SLEEPY_BYTECODE_VERSION 1

typedef struct SleepyBytecodeWriter {
    uint8_t *buffer;
    size_t capacity;
    size_t count;
    SleepyAllocator *allocator;
} SleepyBytecodeWriter;

typedef struct SleepyBytecodeReader {
    const uint8_t *data;
    size_t size;
    size_t offset;
} SleepyBytecodeReader;

void sleepy_bytecode_writer_init(SleepyBytecodeWriter *writer, SleepyAllocator *allocator);
void sleepy_bytecode_writer_free(SleepyBytecodeWriter *writer);
void sleepy_bytecode_writer_write(SleepyBytecodeWriter *writer, const uint8_t *data, size_t len);

SleepyBytecodeReader sleepy_bytecode_reader_init(const uint8_t *data, size_t size);
bool sleepy_bytecode_reader_read(SleepyBytecodeReader *reader, uint8_t *out, size_t len);

bool sleepy_bytecode_serialize_chunk(SleepyBytecodeWriter *writer, SleepyChunk *chunk);
SleepyChunk *sleepy_bytecode_deserialize_chunk(SleepyBytecodeReader *reader, SleepyAllocator *allocator);

bool sleepy_bytecode_serialize_function(SleepyBytecodeWriter *writer, SleepyObjFunction *fn);
SleepyObjFunction *sleepy_bytecode_deserialize_function(SleepyBytecodeReader *reader, SleepyAllocator *allocator);


#endif // SLEEPY_H
