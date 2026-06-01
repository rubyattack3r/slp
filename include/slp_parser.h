#ifndef SLP_PARSER_H
#define SLP_PARSER_H

#include "slp_common.h"
#include "slp_core.h"
#include "slp_lexer.h"

// Forward declaration of an AST node
typedef struct SlpASTNode SlpASTNode;

// Node types matching slp python AST classes
typedef enum {
  SLP_AST_SCRIPT,
  SLP_AST_BLOCK,
  SLP_AST_BOOLEAN,
  SLP_AST_NULL,
  SLP_AST_LONG,
  SLP_AST_LITERAL,
  SLP_AST_STRING,
  SLP_AST_NUMBER,
  SLP_AST_SCALAR,
  SLP_AST_ARRAY,
  SLP_AST_HASHTABLE,
  SLP_AST_IDENTIFIER,
  SLP_AST_CLASS_LITERAL,
  SLP_AST_BACKTICK,
  SLP_AST_ADDRESS,
  SLP_AST_CALL,
  SLP_AST_ASSIGNMENT,
  SLP_AST_BINOP,
  SLP_AST_UNARYOP,
  SLP_AST_IF,
  SLP_AST_WHILE,
  SLP_AST_FOR,
  SLP_AST_FOREACH,
  SLP_AST_RETURN,
  SLP_AST_BREAK,
  SLP_AST_CONTINUE,
  SLP_AST_YIELD,
  SLP_AST_ASSIGN_LOOP,
  SLP_AST_INDEX,
  SLP_AST_OBJ_EXPR,
  SLP_AST_TRY_CATCH,
  SLP_AST_THROW,
  SLP_AST_ASSERT,
  SLP_AST_ENV_BRIDGE,
  SLP_AST_IMPORT,
  SLP_AST_LVALUE_TUPLE,
  SLP_AST_KV_PAIR,
  SLP_AST_ARG,
  SLP_AST_HALT,
  SLP_AST_DONE,
  SLP_AST_CALLCC,
  SLP_AST_LOCAL,
  SLP_AST_THIS,
  SLP_AST_NOP,
  SLP_AST_ERROR // For parse error nodes
} SlpASTNodeType;

struct SlpASTNode {
  SlpASTNodeType type;
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
      struct SlpASTNode *left;
      SlpToken op;
      struct SlpASTNode *right;
    } assign;

    // Binary Operation (and assignments)
    struct {
      struct SlpASTNode *left;
      SlpToken op;
      struct SlpASTNode *right;
      bool negate;
    } binop;

    // Unary Operation
    struct {
      SlpToken op;
      struct SlpASTNode *operand;
      bool is_postfix;
    } unaryop;

    // List of statements (Block, Script, LvalueTuple)
    struct {
      struct SlpASTNode **statements;
      size_t count;
      size_t capacity;
    } block;

    // If statement
    struct {
      struct SlpASTNode *condition;
      struct SlpASTNode *then_branch;
      struct SlpASTNode *else_branch;
    } if_stmt;

    // While statement / Foreach generator
    struct {
      struct SlpASTNode *condition;
      struct SlpASTNode *body;
    } while_stmt;

    // For statement
    struct {
      struct SlpASTNode **initializer;
      size_t init_count;
      struct SlpASTNode *condition;
      struct SlpASTNode **increment;
      size_t inc_count;
      struct SlpASTNode *body;
    } for_stmt;

    // Foreach
    struct {
      const char *index; // Nullable
      const char *value;
      struct SlpASTNode *generator;
      struct SlpASTNode *body;
    } foreach;

    // Assign Loop (while scalar (expr) block)
    struct {
      const char *value;
      struct SlpASTNode *generator;
      struct SlpASTNode *body;
    } assign_loop;

    // Function Call
    struct {
      struct SlpASTNode *target;
      struct SlpASTNode **args; // Array of AST_ARG
      size_t arg_count;
    } call;

    // Arguments & KV Pairs
    struct {
      struct SlpASTNode *name;  // KV Pair / Optional Arg name
      struct SlpASTNode *value; // Value expressions
      bool trailing_sep;
    } arg;

    // Index expression
    struct {
      struct SlpASTNode *container;
      struct SlpASTNode *element;
    } index;

    // Object Expression [target message: args]
    struct {
      struct SlpASTNode *target;
      struct SlpASTNode *message; // usually string or identifier
      struct SlpASTNode **args;
      size_t arg_count;
    } obj_expr;

    // Environment Bridge (keyword identifier string block)
    struct {
      const char *keyword;
      const char *identifier;
      const char *string;
      struct SlpASTNode *body;
    } env_bridge;

    // Try / Catch
    struct {
      struct SlpASTNode *body;
      const char *value; // Exception variable (e.g., $ex)
      struct SlpASTNode *handler;
    } try_catch;

    // Returns, Yields, Throws, Asserts
    struct {
      struct SlpASTNode *value;
    } control;

    // Import
    struct {
      const char *target;
      const char *path;
    } import_stmt;

  } as;
  bool is_rewritten;
};

typedef struct {
  SlpLexer lexer;
  SlpToken current;
  SlpToken previous;
  bool had_error;
  bool panic_mode;
  int error_line;
  const char *error_message;
  int depth; // recursion-depth guard against stack exhaustion on nested input
  SlpAllocator *allocator;
} SlpParser;

void slp_parser_init(SlpParser *parser, const char *source,
                        SlpAllocator *allocator);

// Given a parser initialized on some source code, parse it.
// Returns a top-level AST node or NULL if it failed.
SlpASTNode *slp_parser_parse(SlpParser *parser);

#endif // SLP_PARSER_H
