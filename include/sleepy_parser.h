#ifndef SLEEPY_PARSER_H
#define SLEEPY_PARSER_H

#include "sleepy_common.h"
#include "sleepy_core.h"
#include "sleepy_lexer.h"

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

#endif // SLEEPY_PARSER_H
