#include "sleepy_parser.h"
#include "sleepy_utils.h"

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
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_NUMBER);
  node->as.double_val = 0; // stub
  return node;
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

static SleepyASTNode *sub_declaration(SleepyParser *parser) {
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_CALL);
  // sub is essentially: sub = identifier(name, block)
  SleepyASTNode *sub_id = allocate_node(parser, SLEEPY_AST_IDENTIFIER);
  sub_id->as.string_val =
      sleepy_lexer_copy_lexeme(&parser->lexer, &parser->previous);

  // Get the function name
  consume(parser, SLEEPY_TOKEN_ID, "Expect function name after 'sub'.");
  SleepyASTNode *name = identifier(parser, NULL, false);

  // Parse the body as a block
  SleepyASTNode *body = block(parser);

  // Consume the semicolon after the sub declaration
  match(parser, SLEEPY_TOKEN_SEMICOLON);

  // Create a call node: sub(name, body)
  node->as.call.target = sub_id;
  node->as.call.arg_count = 2;
  node->as.call.args = (SleepyASTNode **)parser->allocator->reallocate(
      NULL, sizeof(SleepyASTNode *) * 2, parser->allocator->user_data);

  SleepyASTNode *arg1 = allocate_node(parser, SLEEPY_AST_ARG);
  arg1->as.arg.name = NULL;
  arg1->as.arg.value = name;

  SleepyASTNode *arg2 = allocate_node(parser, SLEEPY_AST_ARG);
  arg2->as.arg.name = NULL;
  arg2->as.arg.value = body;

  node->as.call.args[0] = arg1;
  node->as.call.args[1] = arg2;

  return node;
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
  if (match(parser, SLEEPY_TOKEN_SUB)) {
    return sub_declaration(parser);
  }
  if (match(parser, SLEEPY_TOKEN_IMPORT)) {
    return import_statement(parser);
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
