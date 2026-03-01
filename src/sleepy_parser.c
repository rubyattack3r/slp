#include "sleepy_parser.h"
#include "sleepy_utils.h"

void sleepy_parser_init(SleepyParser *parser, const char *source,
                        SleepyAllocator *allocator) {
  sleepy_lexer_init(&parser->lexer, source, allocator);
  parser->allocator = allocator;
  parser->had_error = false;
  parser->panic_mode = false;
}

static void error_at(SleepyParser *parser, SleepyToken *token,
                     const char *message) {
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
// PRATT PARSER TABLE
// -----------------------------------------------------------------------------

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT, // =, +=, -=, etc.
  PREC_LOR,        // ||
  PREC_LAND,       // &&
  PREC_BIT_OR,     // |
  PREC_BIT_XOR,    // ^
  PREC_BIT_AND,    // &
  PREC_EQUALITY,   // ==, !=, <=>
  PREC_COMPARISON, // <, >, <=, >=
  PREC_SHIFT,      // <<, >>
  PREC_CONCAT,     // .
  PREC_TERM,       // +, -
  PREC_FACTOR,     // *, /, %, x
  PREC_BRIDGE,     // Builtin Binary Predicate Bridge
  PREC_UNARY,      // ! - +
  PREC_INCDEC,     // ++ --
  PREC_EXP,        // **
  PREC_CALL,       // . () []
  PREC_PRIMARY
} ParsePrecedence;

// Prefix: Left is always NULL
// Infix: Left is the fully parsed LHS expression
typedef SleepyASTNode *(*ParseFn)(SleepyParser *parser, SleepyASTNode *left,
                                  bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  ParsePrecedence precedence;
} ParseRule;

static SleepyASTNode *expression(SleepyParser *parser);
static SleepyASTNode *statement(SleepyParser *parser);
static SleepyASTNode *declaration(SleepyParser *parser);
static SleepyASTNode *block(SleepyParser *parser);
static SleepyASTNode *scalar(SleepyParser *parser, SleepyASTNode *left,
                             bool canAssign); // Forward declaration for scalar

static SleepyASTNode *if_statement(SleepyParser *parser) {
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_IF);
  consume(parser, SLEEPY_TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  node->as.if_stmt.condition = expression(parser);
  consume(parser, SLEEPY_TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  node->as.if_stmt.then_branch = statement(parser);

  if (match(parser, SLEEPY_TOKEN_ELSE)) {
    node->as.if_stmt.else_branch = statement(parser);
  } else {
    node->as.if_stmt.else_branch = NULL;
  }
  return node;
}

static SleepyASTNode *while_statement(SleepyParser *parser) {
  SleepyASTNode *assign_var = NULL;
  if (match(parser, SLEEPY_TOKEN_SCALAR)) {
    assign_var = scalar(parser, NULL, false);
  }

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
  // TODO(parser): properly parse init/cond/inc
  while (!match(parser, SLEEPY_TOKEN_RIGHT_PAREN) &&
         !check(parser, SLEEPY_TOKEN_EOF)) {
    advance(parser);
  }
  node->as.for_stmt.body = statement(parser);
  return node;
}

static SleepyASTNode *foreach_statement(SleepyParser *parser) {
  SleepyASTNode *key_node = NULL;
  SleepyASTNode *val_node = NULL;

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
  node->as.foreach.index = (key_node && key_node->type == SLEEPY_AST_SCALAR)
                               ? key_node->as.string_val
                               : NULL;
  node->as.foreach.value = (val_node && val_node->type == SLEEPY_AST_SCALAR)
                               ? val_node->as.string_val
                               : "stub_var";
  node->as.foreach.generator = source;
  node->as.foreach.body = body;

  return node;
}

static SleepyASTNode *try_statement(SleepyParser *parser) {
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_TRY_CATCH);
  node->as.try_catch.body = block(parser);

  if (match(parser, SLEEPY_TOKEN_CATCH)) {
    consume(parser, SLEEPY_TOKEN_SCALAR,
            "Expect exception variable after catch.");
    node->as.try_catch.value = "stub_ex";
    consume(parser, SLEEPY_TOKEN_LEFT_BRACE,
            "Expect '{' after catch variable.");
    node->as.try_catch.handler = block(parser);
  } else {
    node->as.try_catch.handler = NULL;
  }
  return node;
}

static SleepyASTNode *expression_statement(SleepyParser *parser) {
  SleepyASTNode *expr = expression(parser);
  if (match(parser, SLEEPY_TOKEN_SEMICOLON)) {
    // Optional semicolon in sleepy? usually semicolons are required or
    // optional. For now, we optionally consume it.
  }
  return expr; // We should probably wrap it in an ExprStmt node, but returning
               // it is fine for now
}

static SleepyASTNode *statement(SleepyParser *parser) {
  if (match(parser, SLEEPY_TOKEN_RETURN)) {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_RETURN);
    if (!check(parser, SLEEPY_TOKEN_SEMICOLON) &&
        !check(parser, SLEEPY_TOKEN_EOF)) {
      node->as.control.value = expression(parser);
    }
    match(parser, SLEEPY_TOKEN_SEMICOLON);
    return node;
  }
  if (match(parser, SLEEPY_TOKEN_THROW)) {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_THROW);
    node->as.control.value = expression(parser);
    match(parser, SLEEPY_TOKEN_SEMICOLON);
    return node;
  }
  if (match(parser, SLEEPY_TOKEN_YIELD)) {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_YIELD);
    if (!check(parser, SLEEPY_TOKEN_SEMICOLON) &&
        !check(parser, SLEEPY_TOKEN_EOF)) {
      node->as.control.value = expression(parser);
    }
    match(parser, SLEEPY_TOKEN_SEMICOLON);
    return node;
  }
  if (match(parser, SLEEPY_TOKEN_BREAK)) {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_BREAK);
    match(parser, SLEEPY_TOKEN_SEMICOLON);
    return node;
  }
  if (match(parser, SLEEPY_TOKEN_CONTINUE)) {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_CONTINUE);
    match(parser, SLEEPY_TOKEN_SEMICOLON);
    return node;
  }
  if (match(parser, SLEEPY_TOKEN_HALT)) {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_HALT);
    match(parser, SLEEPY_TOKEN_SEMICOLON);
    return node;
  }
  if (match(parser, SLEEPY_TOKEN_DONE)) {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_DONE);
    match(parser, SLEEPY_TOKEN_SEMICOLON);
    return node;
  }
  if (match(parser, SLEEPY_TOKEN_ASSERT)) {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_ASSERT);
    node->as.control.value = expression(parser);
    // TODO: Support optional error message in assert (if needed)
    if (match(parser, SLEEPY_TOKEN_COLON)) {
      expression(parser); // Skip for now until we add opt_value back or use
                          // separate node
    }
    match(parser, SLEEPY_TOKEN_SEMICOLON);
    return node;
  }
  if (match(parser, SLEEPY_TOKEN_IMPORT)) {
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_IMPORT);
    node->as.import_stmt.path = "stub_path";
    node->as.import_stmt.target = "stub_target";
    // TODO(parser): consume identifiers and strings as needed
    match(parser, SLEEPY_TOKEN_SEMICOLON);
    return node;
  }
  if (match(parser, SLEEPY_TOKEN_IF)) {
    return if_statement(parser);
  }
  if (match(parser, SLEEPY_TOKEN_WHILE)) {
    return while_statement(parser);
  }
  if (match(parser, SLEEPY_TOKEN_FOR)) {
    return for_statement(parser);
  }
  if (match(parser, SLEEPY_TOKEN_FOREACH)) {
    return foreach_statement(parser);
  }
  if (match(parser, SLEEPY_TOKEN_TRY)) {
    return try_statement(parser);
  }
  if (match(parser, SLEEPY_TOKEN_LEFT_BRACE)) {
    return block(parser);
  }
  return expression_statement(parser);
}

static SleepyASTNode *declaration(SleepyParser *parser) {
  if (match(parser, SLEEPY_TOKEN_SUB)) {
    consume(parser, SLEEPY_TOKEN_ID, "Expect subroutine name.");
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_ENV_BRIDGE);
    node->as.env_bridge.name =
        sleepy_lexer_copy_lexeme(&parser->lexer, &parser->previous);
    node->as.env_bridge.body = block(parser);
    return node;
  }
  return statement(parser);
}

static SleepyASTNode *block(SleepyParser *parser) {
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_BLOCK);
  node->as.block.capacity = 8;
  node->as.block.count = 0;
  node->as.block.statements = (SleepyASTNode **)parser->allocator->reallocate(
      NULL, sizeof(SleepyASTNode *) * node->as.block.capacity,
      parser->allocator->user_data);

  while (!check(parser, SLEEPY_TOKEN_RIGHT_BRACE) &&
         !check(parser, SLEEPY_TOKEN_EOF)) {
    SleepyASTNode *stmt = declaration(parser);
    if (stmt != NULL) {
      if (node->as.block.count >= node->as.block.capacity) {
        size_t newCap = node->as.block.capacity * 2;
        node->as.block.statements =
            (SleepyASTNode **)parser->allocator->reallocate(
                node->as.block.statements, sizeof(SleepyASTNode *) * newCap,
                parser->allocator->user_data);
        node->as.block.capacity = newCap;
      }
      node->as.block.statements[node->as.block.count++] = stmt;
    }
  }

  consume(parser, SLEEPY_TOKEN_RIGHT_BRACE, "Expect '}' after block.");
  return node;
}
static ParseRule *get_rule(SleepyTokenType type);
static SleepyASTNode *parse_precedence(SleepyParser *parser,
                                       ParsePrecedence precedence);

static SleepyASTNode *binary(SleepyParser *parser, SleepyASTNode *left,
                             bool canAssign);
static SleepyASTNode *negated_binary(SleepyParser *parser, SleepyASTNode *left,
                                     bool canAssign);
static SleepyASTNode *unary(SleepyParser *parser, SleepyASTNode *left,
                            bool canAssign);
static SleepyASTNode *literal(SleepyParser *parser, SleepyASTNode *left,
                              bool canAssign);
static SleepyASTNode *number(SleepyParser *parser, SleepyASTNode *left,
                             bool canAssign);
static SleepyASTNode *string_val(SleepyParser *parser, SleepyASTNode *left,
                                 bool canAssign);
static SleepyASTNode *identifier(SleepyParser *parser, SleepyASTNode *left,
                                 bool canAssign);
static SleepyASTNode *grouping(SleepyParser *parser, SleepyASTNode *left,
                               bool canAssign);
static SleepyASTNode *logical(SleepyParser *parser, SleepyASTNode *left,
                              bool canAssign);
static SleepyASTNode *scalar(SleepyParser *parser, SleepyASTNode *left,
                             bool canAssign);
static SleepyASTNode *array(SleepyParser *parser, SleepyASTNode *left,
                            bool canAssign);
static SleepyASTNode *hash(SleepyParser *parser, SleepyASTNode *left,
                           bool canAssign);
static SleepyASTNode *index_acc(SleepyParser *parser, SleepyASTNode *left,
                                bool canAssign);
static SleepyASTNode *call(SleepyParser *parser, SleepyASTNode *left,
                           bool canAssign);
static SleepyASTNode *obj_expr(SleepyParser *parser, SleepyASTNode *left,
                               bool canAssign);
static SleepyASTNode *backtick_expr(SleepyParser *parser, SleepyASTNode *left,
                                    bool canAssign);
static SleepyASTNode *assignment(SleepyParser *parser, SleepyASTNode *left,
                                 bool canAssign);
static SleepyASTNode *block_expr(SleepyParser *parser, SleepyASTNode *left,
                                 bool canAssign);
static SleepyASTNode **parse_args(SleepyParser *parser, size_t *count,
                                  SleepyTokenType endToken);

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
    [SLEEPY_TOKEN_BANG] = {unary, negated_binary, PREC_BRIDGE},
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
                                                      PREC_BRIDGE},
    [SLEEPY_TOKEN_UNARY_PREDICATE_BRIDGE] = {unary, NULL, PREC_UNARY},
    [SLEEPY_TOKEN_AMPERSAND] = {NULL, binary, PREC_BIT_AND},
    [SLEEPY_TOKEN_PIPE] = {NULL, binary, PREC_BIT_OR},
    [SLEEPY_TOKEN_CARET] = {NULL, binary, PREC_BIT_XOR},
    [SLEEPY_TOKEN_LSHIFT] = {NULL, binary, PREC_SHIFT},
    [SLEEPY_TOKEN_RSHIFT] = {NULL, binary, PREC_SHIFT},
    [SLEEPY_TOKEN_EXP] = {NULL, binary, PREC_EXP},
    [SLEEPY_TOKEN_EOF] = {NULL, NULL, PREC_NONE},
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
    [SLEEPY_TOKEN_ADDRESS] = {identifier, NULL, PREC_NONE},
    [SLEEPY_TOKEN_ARG_PASSED_BY_NAME] = {identifier, NULL, PREC_NONE},
}; // We'll map the rest of the operators here incrementally as we expand

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
    if (infix_rule != NULL) {
      expr = infix_rule(parser, expr, canAssign);
    }
  }

  return expr;
}

static SleepyASTNode *expression(SleepyParser *parser) {
  return parse_precedence(parser, PREC_ASSIGNMENT);
}

static SleepyASTNode *binary(SleepyParser *parser, SleepyASTNode *left,
                             bool canAssign) {
  (void)canAssign; // unused for now
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
  SleepyASTNode *right = parse_precedence(parser, PREC_BRIDGE);
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_BINOP);
  node->as.binop.left = left;
  node->as.binop.op = predicateToken;
  node->as.binop.right = right;
  node->as.binop.negate = true;
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

static SleepyASTNode *block_expr(SleepyParser *parser, SleepyASTNode *left,
                                 bool canAssign) {
  (void)left;
  (void)canAssign;
  return block(parser);
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

static SleepyASTNode *identifier(SleepyParser *parser, SleepyASTNode *left,
                                 bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_IDENTIFIER);
  node->as.string_val =
      sleepy_lexer_copy_lexeme(&parser->lexer, &parser->previous);
  return node;
}

static SleepyASTNode *scalar(SleepyParser *parser, SleepyASTNode *left,
                             bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_SCALAR);
  // The token itself now contains '$name', skip the '$'
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
  consume(parser, SLEEPY_TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
  return expr;
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

static SleepyASTNode *array(SleepyParser *parser, SleepyASTNode *left,
                            bool canAssign) {
  (void)canAssign;
  (void)left;
  // If it's just '@', it's a literal/creation: @(...)
  if (parser->previous.length == 1) {
    consume(parser, SLEEPY_TOKEN_LEFT_PAREN, "Expect '(' after '@'.");
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_CALL);
    SleepyASTNode *target = allocate_node(parser, SLEEPY_AST_ARRAY);
    target->as.string_val = (char *)parser->lexer.allocator->reallocate(
        NULL, 2, parser->lexer.allocator->user_data);
    ((char *)target->as.string_val)[0] = '@';
    ((char *)target->as.string_val)[1] = '\0';
    node->as.call.target = target;
    node->as.call.args =
        parse_args(parser, &node->as.call.arg_count, SLEEPY_TOKEN_RIGHT_PAREN);
    consume(parser, SLEEPY_TOKEN_RIGHT_PAREN,
            "Expect ')' after array arguments.");
    return node;
  }
  // Otherwise it's '@name'
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
  // If it's just '%', it's a literal/creation: %(...)
  if (parser->previous.length == 1) {
    consume(parser, SLEEPY_TOKEN_LEFT_PAREN, "Expect '(' after '%'.");
    SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_CALL);
    SleepyASTNode *target = allocate_node(parser, SLEEPY_AST_HASHTABLE);
    target->as.string_val = (char *)parser->lexer.allocator->reallocate(
        NULL, 2, parser->lexer.allocator->user_data);
    ((char *)target->as.string_val)[0] = '%';
    ((char *)target->as.string_val)[1] = '\0';
    node->as.call.target = target;
    node->as.call.args =
        parse_args(parser, &node->as.call.arg_count, SLEEPY_TOKEN_RIGHT_PAREN);
    consume(parser, SLEEPY_TOKEN_RIGHT_PAREN,
            "Expect ')' after hash arguments.");
    return node;
  }
  // Otherwise it's '%name'
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_HASHTABLE);
  SleepyToken idToken = parser->previous;
  idToken.start++;
  idToken.length--;
  node->as.string_val = sleepy_lexer_copy_lexeme(&parser->lexer, &idToken);
  return node;
}

static SleepyASTNode *index_acc(SleepyParser *parser, SleepyASTNode *left,
                                bool canAssign) {
  (void)canAssign;
  // This is an infix rule triggered on SLEEPY_TOKEN_LEFT_BRACKET '['
  // Left is the container, e.g., a scalar or an array $arr[expr]
  SleepyASTNode *element = expression(parser);
  consume(parser, SLEEPY_TOKEN_RIGHT_BRACKET, "Expect ']' after index.");

  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_INDEX);
  node->as.index.container = left;
  node->as.index.element = element;
  return node;
}

static SleepyASTNode **parse_args(SleepyParser *parser, size_t *count,
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

      SleepyASTNode *expr = expression(parser);
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
    } while (match(parser, SLEEPY_TOKEN_COMMA));
  }

  return args;
}

static SleepyASTNode *call(SleepyParser *parser, SleepyASTNode *left,
                           bool canAssign) {
  (void)canAssign;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_CALL);
  node->as.call.target = left;
  node->as.call.args =
      parse_args(parser, &node->as.call.arg_count, SLEEPY_TOKEN_RIGHT_PAREN);
  consume(parser, SLEEPY_TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return node;
}

static SleepyASTNode *obj_expr(SleepyParser *parser, SleepyASTNode *left,
                               bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_OBJ_EXPR);
  node->as.obj_expr.target = expression(parser);
  if (match(parser, SLEEPY_TOKEN_COLON)) {
    // It's [target : args]
    node->as.obj_expr.message = NULL;
    node->as.obj_expr.args = parse_args(parser, &node->as.obj_expr.arg_count,
                                        SLEEPY_TOKEN_RIGHT_BRACKET);
  } else if (!check(parser, SLEEPY_TOKEN_RIGHT_BRACKET)) {
    // It's [target message] or [target message : args]
    node->as.obj_expr.message = expression(parser);
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

static SleepyASTNode *backtick_expr(SleepyParser *parser, SleepyASTNode *left,
                                    bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_BACKTICK);
  // TODO(parser): assign string value
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

static SleepyASTNode *number(SleepyParser *parser, SleepyASTNode *left,
                             bool canAssign) {
  (void)canAssign;
  (void)left;
  SleepyASTNode *node = allocate_node(parser, SLEEPY_AST_NUMBER);
  // TODO(parser): Parse actual double value from lexeme
  node->as.double_val = 0;
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

SleepyASTNode *sleepy_parser_parse(SleepyParser *parser) {
  advance(parser); // prime the pump
  if (match(parser, SLEEPY_TOKEN_EOF)) {
    return NULL;
  }

  // Create root SCRIPT node
  SleepyASTNode *script = allocate_node(parser, SLEEPY_AST_SCRIPT);
  script->as.block.capacity = 8;
  script->as.block.count = 0;
  script->as.block.statements = (SleepyASTNode **)parser->allocator->reallocate(
      NULL, sizeof(SleepyASTNode *) * script->as.block.capacity,
      parser->allocator->user_data);

  while (!match(parser, SLEEPY_TOKEN_EOF) && !parser->had_error) {
    SleepyASTNode *stmt = declaration(parser);
    if (stmt != NULL) {
      if (script->as.block.count >= script->as.block.capacity) {
        size_t newCap = script->as.block.capacity * 2;
        script->as.block.statements =
            (SleepyASTNode **)parser->allocator->reallocate(
                script->as.block.statements, sizeof(SleepyASTNode *) * newCap,
                parser->allocator->user_data);
        script->as.block.capacity = newCap;
      }
      script->as.block.statements[script->as.block.count++] = stmt;
    }
  }

  return script;
}
