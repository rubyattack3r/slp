#include "slp_parser.h"
#include "slp_utils.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static void advance(SlpParser *parser);
static void error_at(SlpParser *parser, SlpToken *token,
                     const char *message);
static void error(SlpParser *parser, const char *message);
static void error_at_current(SlpParser *parser, const char *message);
static void consume(SlpParser *parser, SlpTokenType type,
                    const char *message);
static bool match(SlpParser *parser, SlpTokenType type);
static bool check(SlpParser *parser, SlpTokenType type);
static SlpASTNode *allocate_node(SlpParser *parser,
                                    SlpASTNodeType type);

static SlpASTNode *expression(SlpParser *parser);
static SlpASTNode *statement(SlpParser *parser);
static SlpASTNode *declaration(SlpParser *parser);
static SlpASTNode *block(SlpParser *parser);
static SlpASTNode **parse_args(SlpParser *parser, size_t *count,
                                  SlpTokenType endToken);

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

typedef SlpASTNode *(*ParseFn)(SlpParser *parser, SlpASTNode *left,
                                  bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  ParsePrecedence precedence;
} ParseRule;

static ParseRule *get_rule(SlpTokenType type);
static SlpASTNode *parse_precedence(SlpParser *parser,
                                       ParsePrecedence precedence);

// -----------------------------------------------------------------------------
// Parser Core
// -----------------------------------------------------------------------------

void slp_parser_init(SlpParser *parser, const char *source,
                        SlpAllocator *allocator) {
  slp_lexer_init(&parser->lexer, source, allocator);
  parser->allocator = allocator;
  parser->had_error = false;
  parser->panic_mode = false;
  parser->previous.type = SLP_TOKEN_ERROR;
  parser->previous.start = NULL;
  parser->previous.length = 0;
  parser->previous.line = 0;
  parser->current = parser->previous;
  parser->error_line = 0;
  parser->error_message = NULL;
}

static void error_at(SlpParser *parser, SlpToken *token,
                     const char *message) {
  if (parser->panic_mode)
    return;
  parser->panic_mode = true;
  parser->had_error = true;
  parser->error_line = token->line;
  parser->error_message = message;
}

static void error(SlpParser *parser, const char *message) {
  error_at(parser, &parser->previous, message);
}

static void error_at_current(SlpParser *parser, const char *message) {
  error_at(parser, &parser->current, message);
}

static void advance(SlpParser *parser) {
  parser->previous = parser->current;
  for (;;) {
    parser->current = slp_lexer_scan_token(&parser->lexer);
    if (parser->current.type != SLP_TOKEN_ERROR)
      break;
    error_at_current(parser, parser->current.start);
  }
}

static void consume(SlpParser *parser, SlpTokenType type,
                    const char *message) {
  if (parser->current.type == type) {
    advance(parser);
    return;
  }
  error_at_current(parser, message);
}

static bool match(SlpParser *parser, SlpTokenType type) {
  if (parser->current.type != type)
    return false;
  advance(parser);
  return true;
}

static bool check(SlpParser *parser, SlpTokenType type) {
  return parser->current.type == type;
}

static SlpASTNode *allocate_node(SlpParser *parser,
                                    SlpASTNodeType type) {
  SlpASTNode *node = SLP_ALLOCATE(parser->allocator, SlpASTNode);
  if (node) {
    memset(node, 0, sizeof(SlpASTNode));
    node->type = type;
    node->line = parser->previous.line;
  }
  return node;
}

// -----------------------------------------------------------------------------
// Expression implementation
// -----------------------------------------------------------------------------

static SlpASTNode *binary(SlpParser *parser, SlpASTNode *left,
                             bool canAssign) {
  (void)canAssign;
  SlpToken operatorToken = parser->previous;
  ParseRule *rule = get_rule(operatorToken.type);
  SlpASTNode *right =
      parse_precedence(parser, (ParsePrecedence)(rule->precedence + 1));
  SlpASTNode *node = allocate_node(parser, SLP_AST_BINOP);
  node->as.binop.left = left;
  node->as.binop.op = operatorToken;
  node->as.binop.right = right;
  node->as.binop.negate = false;
  return node;
}

static SlpASTNode *negated_binary(SlpParser *parser, SlpASTNode *left,
                                     bool canAssign) {
  (void)canAssign;
  advance(parser); // consume the predicate
  SlpToken predicateToken = parser->previous;
  SlpASTNode *right = parse_precedence(parser, PREC_EQUALITY);
  SlpASTNode *node = allocate_node(parser, SLP_AST_BINOP);
  node->as.binop.left = left;
  node->as.binop.op = predicateToken;
  node->as.binop.right = right;
  node->as.binop.negate = true;
  return node;
}

static SlpASTNode *unary(SlpParser *parser, SlpASTNode *left,
                            bool canAssign) {
  (void)canAssign;
  (void)left;
  SlpToken operatorToken = parser->previous;
  SlpASTNode *operand = parse_precedence(parser, PREC_UNARY);
  SlpASTNode *node = allocate_node(parser, SLP_AST_UNARYOP);
  node->as.unaryop.op = operatorToken;
  node->as.unaryop.operand = operand;
  node->as.unaryop.is_postfix = false;
  return node;
}

static SlpASTNode *assignment(SlpParser *parser, SlpASTNode *left,
                                 bool canAssign) {
  (void)canAssign;
  SlpToken operatorToken = parser->previous;
  SlpASTNode *right = parse_precedence(parser, PREC_ASSIGNMENT);
  SlpASTNode *node = allocate_node(parser, SLP_AST_ASSIGNMENT);
  node->as.assign.left = left;
  node->as.assign.op = operatorToken;
  node->as.assign.right = right;
  return node;
}

static SlpASTNode *identifier(SlpParser *parser, SlpASTNode *left,
                                 bool canAssign) {
  (void)canAssign;
  (void)left;
  SlpASTNode *node = allocate_node(parser, SLP_AST_IDENTIFIER);
  node->as.string_val =
      slp_lexer_copy_lexeme(&parser->lexer, &parser->previous);
  return node;
}

static const char *copy_unquoted_lexeme(SlpParser *parser, SlpToken *token) {
    if (token->length < 2) return slp_lexer_copy_lexeme(&parser->lexer, token);
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

static char *duplicate_string(SlpParser *parser, const char *src, size_t len) {
    char *dest = (char *)parser->allocator->reallocate(NULL, len + 1, NULL);
    if (dest) {
        slp_utils_memcpy(dest, src, len);
        dest[len] = '\0';
    }
    return dest;
}

static SlpASTNode *parse_double_quoted_string(SlpParser *parser, SlpToken *token) {
    if (token->length < 2) {
        SlpASTNode *node = allocate_node(parser, SLP_AST_STRING);
        node->as.string_val = duplicate_string(parser, "", 0);
        return node;
    }

    const char *src = token->start + 1;
    size_t inner_len = token->length - 2;
    const char *end = src + inner_len;

    char *lit_buf = (char *)parser->allocator->reallocate(NULL, inner_len + 1, NULL);
    if (!lit_buf) return NULL;
    size_t lit_len = 0;

    SlpASTNode *parts[512];
    int part_count = 0;

    const char *p = src;
    while (p < end) {
        // 1. Backslash escapes
        if (*p == '\\' && p + 1 < end) {
            char next = p[1];
            if (next == '$' || next == '@' || next == '%' || next == '\\' || next == '"' || next == '\'') {
                lit_buf[lit_len++] = next;
                p += 2;
            } else {
                switch (next) {
                    case 'n': lit_buf[lit_len++] = '\n'; break;
                    case 't': lit_buf[lit_len++] = '\t'; break;
                    case 'r': lit_buf[lit_len++] = '\r'; break;
                    default: lit_buf[lit_len++] = '\\'; lit_buf[lit_len++] = next; break;
                }
                p += 2;
            }
            continue;
        }

        // 2. $+ operator (strip preceding/succeeding spaces)
        if (*p == '$' && p + 1 < end && p[1] == '+') {
            // Strip trailing spaces from our collected literal buffer
            while (lit_len > 0 && (lit_buf[lit_len - 1] == ' ' || lit_buf[lit_len - 1] == '\t')) {
                lit_len--;
            }
            // Flush literal buffer if not empty
            if (lit_len > 0) {
                lit_buf[lit_len] = '\0';
                SlpASTNode *node = allocate_node(parser, SLP_AST_STRING);
                node->as.string_val = duplicate_string(parser, lit_buf, lit_len);
                parts[part_count++] = node;
                lit_len = 0;
            }

            p += 2; // skip $+

            // Skip leading spaces/tabs after $+
            while (p < end && (*p == ' ' || *p == '\t')) {
                p++;
            }
            continue;
        }

        // 3. Variable interpolation
        if ((*p == '$' || *p == '@' || *p == '%') && p + 1 < end) {
            char sigil = *p;
            const char *var_start = p + 1;
            const char *var_p = var_start;
            while (var_p < end && ((*var_p >= 'a' && *var_p <= 'z') || 
                                   (*var_p >= 'A' && *var_p <= 'Z') || 
                                   (*var_p >= '0' && *var_p <= '9') || 
                                   *var_p == '_')) {
                var_p++;
            }
            if (var_p > var_start) {
                // We have a valid variable name!
                // First flush the literal buffer collected so far
                if (lit_len > 0) {
                    lit_buf[lit_len] = '\0';
                    SlpASTNode *node = allocate_node(parser, SLP_AST_STRING);
                    node->as.string_val = duplicate_string(parser, lit_buf, lit_len);
                    parts[part_count++] = node;
                    lit_len = 0;
                }

                // Create the variable AST node
                size_t var_name_len = var_p - var_start;
                char *var_name = duplicate_string(parser, var_start, var_name_len);

                SlpASTNode *node = NULL;
                if (sigil == '$') {
                    node = allocate_node(parser, SLP_AST_SCALAR);
                } else if (sigil == '@') {
                    node = allocate_node(parser, SLP_AST_ARRAY);
                } else {
                    node = allocate_node(parser, SLP_AST_HASHTABLE);
                }
                node->as.string_val = var_name;
                parts[part_count++] = node;

                p = var_p;
                continue;
            }
        }

        // 4. Regular character
        lit_buf[lit_len++] = *p;
        p++;
    }

    // Flush any remaining literal characters
    if (lit_len > 0) {
        lit_buf[lit_len] = '\0';
        SlpASTNode *node = allocate_node(parser, SLP_AST_STRING);
        node->as.string_val = duplicate_string(parser, lit_buf, lit_len);
        parts[part_count++] = node;
    }

    parser->allocator->reallocate(lit_buf, 0, NULL);

    // Build the tree of concatenations
    if (part_count == 0) {
        SlpASTNode *node = allocate_node(parser, SLP_AST_STRING);
        node->as.string_val = duplicate_string(parser, "", 0);
        return node;
    }
    if (part_count == 1) {
        return parts[0];
    }

    SlpASTNode *result = parts[0];
    for (int i = 1; i < part_count; i++) {
        SlpASTNode *binop = allocate_node(parser, SLP_AST_BINOP);
        binop->as.binop.left = result;
        binop->as.binop.right = parts[i];
        binop->as.binop.op.type = SLP_TOKEN_DOT;
        binop->as.binop.op.start = ".";
        binop->as.binop.op.length = 1;
        binop->as.binop.op.line = token->line;
        binop->as.binop.negate = false;
        result = binop;
    }
    return result;
}

static SlpASTNode *string_val(SlpParser *parser, SlpASTNode *left,
                                 bool canAssign) {
  (void)canAssign;
  (void)left;
  SlpToken token = parser->previous;
  if (token.type == SLP_TOKEN_STRING) {
      return parse_double_quoted_string(parser, &token);
  }
  SlpASTNode *node = allocate_node(parser, SLP_AST_STRING);
  node->as.string_val = copy_unquoted_lexeme(parser, &token);
  return node;
}

static SlpASTNode *number(SlpParser *parser, SlpASTNode *left,
                             bool canAssign) {
  (void)canAssign;
  (void)left;
  SlpToken token = parser->previous;
  const char *lexeme = slp_lexer_copy_lexeme(&parser->lexer, &token);

  if (token.type == SLP_TOKEN_LONG) {
    SlpASTNode *node = allocate_node(parser, SLP_AST_LONG);
    node->as.long_val = strtoll(lexeme, NULL, 10);
    return node;
  } else {
    SlpASTNode *node = allocate_node(parser, SLP_AST_NUMBER);
    node->as.double_val = strtod(lexeme, NULL);
    return node;
  }
}

static SlpASTNode *literal(SlpParser *parser, SlpASTNode *left,
                              bool canAssign) {
  (void)canAssign;
  (void)left;
  switch (parser->previous.type) {
  case SLP_TOKEN_FALSE: {
    SlpASTNode *node = allocate_node(parser, SLP_AST_BOOLEAN);
    node->as.boolean = false;
    return node;
  }
  case SLP_TOKEN_TRUE: {
    SlpASTNode *node = allocate_node(parser, SLP_AST_BOOLEAN);
    node->as.boolean = true;
    return node;
  }
  case SLP_TOKEN_NULL:
    return allocate_node(parser, SLP_AST_NULL);
  default:
    return NULL;
  }
}

static SlpASTNode *scalar(SlpParser *parser, SlpASTNode *left,
                             bool canAssign) {
  (void)canAssign;
  (void)left;
  SlpASTNode *node = allocate_node(parser, SLP_AST_SCALAR);
  SlpToken idToken = parser->previous;
  if (idToken.length > 0) {
    idToken.start++;
    idToken.length--;
  }
  node->as.string_val = slp_lexer_copy_lexeme(&parser->lexer, &idToken);
  return node;
}

static SlpASTNode *array(SlpParser *parser, SlpASTNode *left,
                            bool canAssign) {
  (void)canAssign;
  (void)left;
  if (parser->previous.length == 1) { // @(...)
    consume(parser, SLP_TOKEN_LEFT_PAREN, "Expect '(' after '@'.");
    SlpASTNode *node = allocate_node(parser, SLP_AST_CALL);
    SlpASTNode *target = allocate_node(parser, SLP_AST_ARRAY);
    char *at = (char *)parser->allocator->reallocate(NULL, 2, parser->allocator->user_data);
    if (at) slp_utils_strcpy(at, "@");
    target->as.string_val = at;
    node->as.call.target = target;
    extern SlpASTNode **parse_args(SlpParser * parser, size_t *count,
                                      SlpTokenType endToken);
    node->as.call.args =
        parse_args(parser, &node->as.call.arg_count, SLP_TOKEN_RIGHT_PAREN);
    consume(parser, SLP_TOKEN_RIGHT_PAREN,
            "Expect ')' after array arguments.");
    return node;
  }
  SlpASTNode *node = allocate_node(parser, SLP_AST_ARRAY);
  SlpToken idToken = parser->previous;
  idToken.start++;
  idToken.length--;
  node->as.string_val = slp_lexer_copy_lexeme(&parser->lexer, &idToken);
  return node;
}

static SlpASTNode *hash(SlpParser *parser, SlpASTNode *left,
                           bool canAssign) {
  (void)canAssign;
  (void)left;
  if (parser->previous.length == 1) { // %(...)
    consume(parser, SLP_TOKEN_LEFT_PAREN, "Expect '(' after '%'.");
    SlpASTNode *node = allocate_node(parser, SLP_AST_CALL);
    SlpASTNode *target = allocate_node(parser, SLP_AST_HASHTABLE);
    char *pct = (char *)parser->allocator->reallocate(NULL, 2, parser->allocator->user_data);
    if (pct) slp_utils_strcpy(pct, "%");
    target->as.string_val = pct;
    node->as.call.target = target;
    extern SlpASTNode **parse_args(SlpParser * parser, size_t *count,
                                      SlpTokenType endToken);
    node->as.call.args =
        parse_args(parser, &node->as.call.arg_count, SLP_TOKEN_RIGHT_PAREN);
    consume(parser, SLP_TOKEN_RIGHT_PAREN,
            "Expect ')' after hash arguments.");
    return node;
  }
  SlpASTNode *node = allocate_node(parser, SLP_AST_HASHTABLE);
  SlpToken idToken = parser->previous;
  idToken.start++;
  idToken.length--;
  node->as.string_val = slp_lexer_copy_lexeme(&parser->lexer, &idToken);
  return node;
}

static SlpASTNode *grouping(SlpParser *parser, SlpASTNode *left,
                               bool canAssign) {
  (void)canAssign;
  (void)left;
  SlpASTNode *expr = expression(parser);
  if (check(parser, SLP_TOKEN_COMMA)) {
    SlpASTNode *node = allocate_node(parser, SLP_AST_LVALUE_TUPLE);
    node->as.block.capacity = 4;
    node->as.block.count = 0;
    node->as.block.statements = (SlpASTNode **)parser->allocator->reallocate(
        NULL, sizeof(SlpASTNode *) * node->as.block.capacity,
        parser->allocator->user_data);
    node->as.block.statements[node->as.block.count++] = expr;
    while (match(parser, SLP_TOKEN_COMMA)) {
      if (node->as.block.count >= node->as.block.capacity) {
        node->as.block.capacity *= 2;
        node->as.block.statements =
            (SlpASTNode **)parser->allocator->reallocate(
                node->as.block.statements,
                sizeof(SlpASTNode *) * node->as.block.capacity,
                parser->allocator->user_data);
      }
      node->as.block.statements[node->as.block.count++] = expression(parser);
    }
    consume(parser, SLP_TOKEN_RIGHT_PAREN, "Expect ')' after tuple.");
    return node;
  }
  consume(parser, SLP_TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
  return expr;
}

static SlpASTNode *postfix(SlpParser *parser, SlpASTNode *left,
                              bool canAssign) {
  (void)canAssign;
  SlpASTNode *node = allocate_node(parser, SLP_AST_UNARYOP);
  node->as.unaryop.op = parser->previous;
  node->as.unaryop.operand = left;
  node->as.unaryop.is_postfix = true;
  return node;
}

static SlpASTNode *logical(SlpParser *parser, SlpASTNode *left,
                              bool canAssign) {
  (void)canAssign;
  SlpToken operatorToken = parser->previous;
  ParseRule *rule = get_rule(operatorToken.type);
  SlpASTNode *right =
      parse_precedence(parser, (ParsePrecedence)(rule->precedence));
  SlpASTNode *node = allocate_node(parser, SLP_AST_BINOP);
  node->as.binop.left = left;
  node->as.binop.op = operatorToken;
  node->as.binop.right = right;
  node->as.binop.negate = false;
  return node;
}

static SlpASTNode *index_acc(SlpParser *parser, SlpASTNode *left,
                                bool canAssign) {
  (void)canAssign;
  SlpASTNode *element = expression(parser);
  consume(parser, SLP_TOKEN_RIGHT_BRACKET, "Expect ']' after index.");
  SlpASTNode *node = allocate_node(parser, SLP_AST_INDEX);
  node->as.index.container = left;
  node->as.index.element = element;
  return node;
}

static SlpASTNode *obj_expr(SlpParser *parser, SlpASTNode *left,
                               bool canAssign) {
  (void)canAssign;
  (void)left;
  SlpASTNode *node = allocate_node(parser, SLP_AST_OBJ_EXPR);
  node->as.obj_expr.target = expression(parser);
  extern SlpASTNode **parse_args(SlpParser * parser, size_t *count,
                                    SlpTokenType endToken);
  if (match(parser, SLP_TOKEN_COLON)) {
    node->as.obj_expr.message = NULL;
    node->as.obj_expr.args = parse_args(parser, &node->as.obj_expr.arg_count,
                                        SLP_TOKEN_RIGHT_BRACKET);
  } else if (!check(parser, SLP_TOKEN_RIGHT_BRACKET)) {
    // Parse the message - keywords can be used as message names in object
    // expressions
    SlpASTNode *message = NULL;

    // Check if current token is a command keyword (return, yield, etc.)
    // These should be treated as identifiers in object expressions
    bool is_command_keyword = false;
    switch (parser->current.type) {
    case SLP_TOKEN_RETURN:
    case SLP_TOKEN_THROW:
    case SLP_TOKEN_YIELD:
    case SLP_TOKEN_ASSERT:
    case SLP_TOKEN_CALLCC:
    case SLP_TOKEN_HALT:
    case SLP_TOKEN_DONE:
    case SLP_TOKEN_LOCAL:
    case SLP_TOKEN_THIS:
    case SLP_TOKEN_BREAK:
    case SLP_TOKEN_CONTINUE:
      is_command_keyword = true;
      break;
    default:
      break;
    }

    if (is_command_keyword) {
      // Treat this keyword as an identifier/message name
      advance(parser);
      message = allocate_node(parser, SLP_AST_IDENTIFIER);
      message->as.string_val =
          slp_lexer_copy_lexeme(&parser->lexer, &parser->previous);
    } else {
      message = expression(parser);
    }
    node->as.obj_expr.message = message;

    if (match(parser, SLP_TOKEN_COLON)) {
      node->as.obj_expr.args = parse_args(parser, &node->as.obj_expr.arg_count,
                                          SLP_TOKEN_RIGHT_BRACKET);
    } else {
      node->as.obj_expr.args = NULL;
      node->as.obj_expr.arg_count = 0;
    }
  } else {
    node->as.obj_expr.message = NULL;
    node->as.obj_expr.args = NULL;
    node->as.obj_expr.arg_count = 0;
  }
  consume(parser, SLP_TOKEN_RIGHT_BRACKET,
          "Expect ']' after object expression.");
  return node;
}

static SlpASTNode *call(SlpParser *parser, SlpASTNode *left,
                           bool canAssign) {
  (void)canAssign;
  SlpASTNode *node = allocate_node(parser, SLP_AST_CALL);
  node->as.call.target = left;
  extern SlpASTNode **parse_args(SlpParser * parser, size_t *count,
                                    SlpTokenType endToken);
  node->as.call.args =
      parse_args(parser, &node->as.call.arg_count, SLP_TOKEN_RIGHT_PAREN);
  consume(parser, SLP_TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return node;
}

static SlpASTNode *command(SlpParser *parser, SlpASTNode *left,
                              bool canAssign) {
  (void)left;
  (void)canAssign;
  SlpToken op = parser->previous;
  SlpASTNodeType type = SLP_AST_NOP;
  if (op.type == SLP_TOKEN_RETURN)
    type = SLP_AST_RETURN;
  else if (op.type == SLP_TOKEN_THROW)
    type = SLP_AST_THROW;
  else if (op.type == SLP_TOKEN_YIELD)
    type = SLP_AST_YIELD;
  else if (op.type == SLP_TOKEN_ASSERT)
    type = SLP_AST_ASSERT;
  else if (op.type == SLP_TOKEN_CALLCC)
    type = SLP_AST_CALLCC;
  else if (op.type == SLP_TOKEN_HALT)
    type = SLP_AST_HALT;
  else if (op.type == SLP_TOKEN_DONE)
    type = SLP_AST_DONE;
  else if (op.type == SLP_TOKEN_LOCAL)
    type = SLP_AST_LOCAL;
  else if (op.type == SLP_TOKEN_THIS)
    type = SLP_AST_THIS;
  else if (op.type == SLP_TOKEN_BREAK)
    type = SLP_AST_BREAK;
  else if (op.type == SLP_TOKEN_CONTINUE)
    type = SLP_AST_CONTINUE;

  SlpASTNode *node = allocate_node(parser, type);
  node->as.control.value = NULL;

  // For assert, we may have: assert expr : "message"
  if (op.type == SLP_TOKEN_ASSERT) {
    if (!check(parser, SLP_TOKEN_SEMICOLON) &&
        !check(parser, SLP_TOKEN_EOF) &&
        !check(parser, SLP_TOKEN_RIGHT_PAREN) &&
        !check(parser, SLP_TOKEN_RIGHT_BRACE)) {
      node->as.control.value = expression(parser);
      // Check for optional : "message"
      if (match(parser, SLP_TOKEN_COLON)) {
        // The message is parsed as part of the expression - for now just
        // consume it In a full implementation, we'd store this separately
        if (!check(parser, SLP_TOKEN_SEMICOLON) &&
            !check(parser, SLP_TOKEN_EOF)) {
          expression(parser); // Parse but discard the message for now
        }
      }
    }
  } else {
    // For other commands
    if (!check(parser, SLP_TOKEN_SEMICOLON) &&
        !check(parser, SLP_TOKEN_EOF) &&
        !check(parser, SLP_TOKEN_RIGHT_PAREN) &&
        !check(parser, SLP_TOKEN_RIGHT_BRACE)) {
      node->as.control.value = expression(parser);
    }
  }
  return node;
}

static SlpASTNode *bridge(SlpParser *parser, SlpASTNode *left,
                             bool canAssign) {
  (void)left;
  (void)canAssign;
  SlpToken keyword = parser->previous;
  SlpASTNode *node = allocate_node(parser, SLP_AST_ENV_BRIDGE);
  node->as.env_bridge.keyword =
      slp_lexer_copy_lexeme(&parser->lexer, &keyword);
  node->as.env_bridge.identifier = NULL;
  node->as.env_bridge.string = NULL;
  node->as.env_bridge.body = NULL;

  if (match(parser, SLP_TOKEN_ID)) {
    node->as.env_bridge.identifier =
        slp_lexer_copy_lexeme(&parser->lexer, &parser->previous);
  }
  if (match(parser, SLP_TOKEN_STRING) ||
      match(parser, SLP_TOKEN_LITERAL)) {
    node->as.env_bridge.string =
        slp_lexer_copy_lexeme(&parser->lexer, &parser->previous);
  }
  if (check(parser, SLP_TOKEN_LEFT_BRACE)) {
    node->as.env_bridge.body = block(parser);
  }
  return node;
}

static SlpASTNode *backtick_expr(SlpParser *parser, SlpASTNode *left,
                                    bool canAssign) {
  (void)canAssign;
  (void)left;
  return allocate_node(parser, SLP_AST_BACKTICK);
}

static SlpASTNode *address_expr(SlpParser *parser, SlpASTNode *left,
                                   bool canAssign) {
  (void)canAssign;
  (void)left;
  SlpASTNode *node = allocate_node(parser, SLP_AST_ADDRESS);
  SlpToken t = parser->previous;
  if (t.length > 0) {
    t.start++;
    t.length--;
  }
  node->as.string_val = slp_lexer_copy_lexeme(&parser->lexer, &t);
  return node;
}

static SlpASTNode *class_expr(SlpParser *parser, SlpASTNode *left,
                                 bool canAssign) {
  (void)canAssign;
  (void)left;
  SlpASTNode *node = allocate_node(parser, SLP_AST_CLASS_LITERAL);
  SlpToken t = parser->previous;
  if (t.length > 0) {
    t.start++;
    t.length--;
  }
  node->as.string_val = slp_lexer_copy_lexeme(&parser->lexer, &t);
  return node;
}

static SlpASTNode *block_expr(SlpParser *parser, SlpASTNode *left,
                                 bool canAssign) {
  (void)left;
  (void)canAssign;
  // The '{' has already been consumed by parse_precedence
  SlpASTNode *node = allocate_node(parser, SLP_AST_BLOCK);
  node->as.block.capacity = 8;
  node->as.block.count = 0;
  node->as.block.statements = (SlpASTNode **)parser->allocator->reallocate(
      NULL, sizeof(SlpASTNode *) * node->as.block.capacity,
      parser->allocator->user_data);
  while (!check(parser, SLP_TOKEN_RIGHT_BRACE) &&
         !check(parser, SLP_TOKEN_EOF) && !parser->had_error) {
    SlpASTNode *stmt = declaration(parser);
    if (stmt) {
      if (node->as.block.count >= node->as.block.capacity) {
        node->as.block.capacity *= 2;
        node->as.block.statements =
            (SlpASTNode **)parser->allocator->reallocate(
                node->as.block.statements,
                sizeof(SlpASTNode *) * node->as.block.capacity,
                parser->allocator->user_data);
      }
      node->as.block.statements[node->as.block.count++] = stmt;
    }
  }
  consume(parser, SLP_TOKEN_RIGHT_BRACE, "Expect '}' after block.");
  return node;
}

// -----------------------------------------------------------------------------
// Rules Table
// -----------------------------------------------------------------------------

ParseRule rules[] = {
    [SLP_TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [SLP_TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [SLP_TOKEN_LEFT_BRACE] = {block_expr, NULL, PREC_NONE},
    [SLP_TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [SLP_TOKEN_LEFT_BRACKET] = {obj_expr, index_acc, PREC_CALL},
    [SLP_TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [SLP_TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [SLP_TOKEN_DOT] = {NULL, binary, PREC_CONCAT},
    [SLP_TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [SLP_TOKEN_PLUS] = {unary, binary, PREC_TERM},
    [SLP_TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [SLP_TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [SLP_TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [SLP_TOKEN_BANG] = {unary, negated_binary, PREC_UNARY},
    [SLP_TOKEN_NE] = {NULL, binary, PREC_EQUALITY},
    [SLP_TOKEN_EQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLP_TOKEN_EQ] = {NULL, binary, PREC_EQUALITY},
    [SLP_TOKEN_EQI] = {NULL, binary, PREC_EQUALITY},
    [SLP_TOKEN_NEQI] = {NULL, binary, PREC_EQUALITY},
    [SLP_TOKEN_SPACESHIP] = {NULL, binary, PREC_EQUALITY},
    [SLP_TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [SLP_TOKEN_GE] = {NULL, binary, PREC_COMPARISON},
    [SLP_TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [SLP_TOKEN_LE] = {NULL, binary, PREC_COMPARISON},
    [SLP_TOKEN_ID] = {identifier, NULL, PREC_NONE},
    [SLP_TOKEN_STRING] = {string_val, NULL, PREC_NONE},
    [SLP_TOKEN_LITERAL] = {string_val, NULL, PREC_NONE},
    [SLP_TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [SLP_TOKEN_DOUBLE] = {number, NULL, PREC_NONE},
    [SLP_TOKEN_LONG] = {number, NULL, PREC_NONE},
    [SLP_TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [SLP_TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [SLP_TOKEN_NULL] = {literal, NULL, PREC_NONE},
    [SLP_TOKEN_LAND] = {NULL, logical, PREC_LAND},
    [SLP_TOKEN_LOR] = {NULL, logical, PREC_LOR},
    [SLP_TOKEN_SCALAR] = {scalar, NULL, PREC_NONE},
    [SLP_TOKEN_AT] = {array, NULL, PREC_NONE},
    [SLP_TOKEN_PERCENT] = {hash, binary, PREC_FACTOR},
    [SLP_TOKEN_LOWER_X] = {NULL, binary, PREC_FACTOR},
    [SLP_TOKEN_BACKTICK_EXPR] = {backtick_expr, NULL, PREC_NONE},
    [SLP_TOKEN_BUILTIN_BINARY_PREDICATE_BRIDGE] = {NULL, binary,
                                                      PREC_EQUALITY},
    [SLP_TOKEN_UNARY_PREDICATE_BRIDGE] = {unary, NULL, PREC_UNARY},
    [SLP_TOKEN_AMPERSAND] = {NULL, binary, PREC_BIT_AND},
    [SLP_TOKEN_PIPE] = {NULL, binary, PREC_BIT_OR},
    [SLP_TOKEN_CARET] = {NULL, binary, PREC_BIT_XOR},
    [SLP_TOKEN_LSHIFT] = {NULL, binary, PREC_SHIFT},
    [SLP_TOKEN_RSHIFT] = {NULL, binary, PREC_SHIFT},
    [SLP_TOKEN_EXP] = {NULL, binary, PREC_EXP},
    [SLP_TOKEN_EOF] = {NULL, NULL, PREC_NONE},
    [SLP_TOKEN_ADDRESS] = {address_expr, NULL, PREC_NONE},
    [SLP_TOKEN_CLASS_LITERAL] = {class_expr, NULL, PREC_NONE},
    [SLP_TOKEN_ARG_PASSED_BY_NAME] = {address_expr, NULL, PREC_NONE},
    [SLP_TOKEN_COPY] = {identifier, NULL, PREC_NONE},
    [SLP_TOKEN_ADDALL] = {identifier, NULL, PREC_NONE},
    [SLP_TOKEN_PRINTLN] = {identifier, NULL, PREC_NONE},
    [SLP_TOKEN_REMOVEALL] = {identifier, NULL, PREC_NONE},
    [SLP_TOKEN_RETAINALL] = {identifier, NULL, PREC_NONE},
    [SLP_TOKEN_ANDEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLP_TOKEN_CATEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLP_TOKEN_DIVEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLP_TOKEN_LSHIFTEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLP_TOKEN_MINUSEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLP_TOKEN_OREQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLP_TOKEN_PLUSEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLP_TOKEN_RSHIFTEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLP_TOKEN_TIMESEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLP_TOKEN_XOREQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLP_TOKEN_EXPEQUAL] = {NULL, assignment, PREC_ASSIGNMENT},
    [SLP_TOKEN_ASSERT] = {command, NULL, PREC_NONE},
    [SLP_TOKEN_BREAK] = {command, NULL, PREC_NONE},
    [SLP_TOKEN_CONTINUE] = {command, NULL, PREC_NONE},
    [SLP_TOKEN_DONE] = {command, NULL, PREC_NONE},
    [SLP_TOKEN_HALT] = {command, NULL, PREC_NONE},
    [SLP_TOKEN_RETURN] = {command, NULL, PREC_NONE},
    [SLP_TOKEN_THROW] = {command, NULL, PREC_NONE},
    [SLP_TOKEN_YIELD] = {command, NULL, PREC_NONE},
    [SLP_TOKEN_CALLCC] = {command, NULL, PREC_NONE},
    [SLP_TOKEN_SUB] = {bridge, NULL, PREC_NONE},
    [SLP_TOKEN_INLINE] = {bridge, NULL, PREC_NONE},
    [SLP_TOKEN_LOCAL] = {command, NULL, PREC_NONE},
    [SLP_TOKEN_THIS] = {command, NULL, PREC_NONE},
    [SLP_TOKEN_INC] = {unary, postfix, PREC_INCDEC},
    [SLP_TOKEN_DEC] = {unary, postfix, PREC_INCDEC},
};

static ParseRule *get_rule(SlpTokenType type) { return &rules[type]; }

static SlpASTNode *parse_precedence(SlpParser *parser,
                                       ParsePrecedence precedence) {
  advance(parser);
  ParseFn prefix_rule = get_rule(parser->previous.type)->prefix;
  if (prefix_rule == NULL) {
    error(parser, "Expect expression.");
    return NULL;
  }
  bool canAssign = precedence <= PREC_ASSIGNMENT;
  SlpASTNode *expr = prefix_rule(parser, NULL, canAssign);

  // Special case: string juxtaposition concatenation
  // If the expression we just parsed is a string literal, and the NEXT token
  // is ALSO a string literal, we should concatenate them into a single node.
  if (expr &&
      (expr->type == SLP_AST_STRING || expr->type == SLP_AST_LITERAL) &&
      (check(parser, SLP_TOKEN_STRING) ||
       check(parser, SLP_TOKEN_LITERAL))) {

    SlpStringBuffer buffer;
    slp_string_buffer_init(&buffer, parser->allocator);

    // Append the first string
    slp_string_buffer_append_string(
        &buffer, expr->as.string_val, slp_utils_strlen(expr->as.string_val));

    // While the next token is a string literal, consume and append it
    while (check(parser, SLP_TOKEN_STRING) ||
           check(parser, SLP_TOKEN_LITERAL)) {
      advance(parser);
      char *next =
          (char *)slp_lexer_copy_lexeme(&parser->lexer, &parser->previous);
      slp_string_buffer_append_string(&buffer, next,
                                         slp_utils_strlen(next));
      parser->allocator->reallocate(next, 0, parser->allocator->user_data);
    }

    // Update the expression to be the concatenated string
    expr->type = SLP_AST_STRING;
    // Free the old string value
    parser->allocator->reallocate((void *)expr->as.string_val, 0,
                                  parser->allocator->user_data);

    char *new_str = (char *)parser->allocator->reallocate(
        NULL, buffer.length + 1, parser->allocator->user_data);
    slp_utils_memcpy(new_str, buffer.data, buffer.length);
    new_str[buffer.length] = '\0';
    expr->as.string_val = new_str;

    slp_string_buffer_free(&buffer);
  }

  while (precedence <= get_rule(parser->current.type)->precedence) {
    advance(parser);
    ParseFn infix_rule = get_rule(parser->previous.type)->infix;
    if (infix_rule != NULL)
      expr = infix_rule(parser, expr, canAssign);
  }
  return expr;
}

static SlpASTNode *expression(SlpParser *parser) {
  return parse_precedence(parser, PREC_ASSIGNMENT);
}

// -----------------------------------------------------------------------------
// Statements and Declarations
// -----------------------------------------------------------------------------

static SlpASTNode *expression_statement(SlpParser *parser) {
  SlpASTNode *expr = expression(parser);
  match(parser, SLP_TOKEN_SEMICOLON);
  return expr;
}

static SlpASTNode *if_statement(SlpParser *parser) {
  SlpASTNode *node = allocate_node(parser, SLP_AST_IF);
  consume(parser, SLP_TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  node->as.if_stmt.condition = expression(parser);
  consume(parser, SLP_TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
  node->as.if_stmt.then_branch = statement(parser);
  if (match(parser, SLP_TOKEN_ELSE))
    node->as.if_stmt.else_branch = statement(parser);
  else
    node->as.if_stmt.else_branch = NULL;
  return node;
}

static SlpASTNode *while_statement(SlpParser *parser) {
  SlpASTNode *assign_var = NULL;
  if (match(parser, SLP_TOKEN_SCALAR))
    assign_var = scalar(parser, NULL, false);
  consume(parser, SLP_TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
  SlpASTNode *condition = expression(parser);
  consume(parser, SLP_TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
  SlpASTNode *body = block(parser);
  if (assign_var) {
    SlpASTNode *node = allocate_node(parser, SLP_AST_ASSIGN_LOOP);
    node->as.assign_loop.value = assign_var->as.string_val;
    node->as.assign_loop.generator = condition;
    node->as.assign_loop.body = body;
    return node;
  }
  SlpASTNode *node = allocate_node(parser, SLP_AST_WHILE);
  node->as.while_stmt.condition = condition;
  node->as.while_stmt.body = body;
  return node;
}

static SlpASTNode *for_statement(SlpParser *parser) {
  SlpASTNode *node = allocate_node(parser, SLP_AST_FOR);
  consume(parser, SLP_TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

  // Parse initializer(s)
  node->as.for_stmt.initializer = NULL;
  node->as.for_stmt.init_count = 0;
  if (!check(parser, SLP_TOKEN_SEMICOLON)) {
    size_t init_capacity = 4;
    node->as.for_stmt.initializer =
        (SlpASTNode **)parser->allocator->reallocate(
            NULL, sizeof(SlpASTNode *) * init_capacity,
            parser->allocator->user_data);

    while (!check(parser, SLP_TOKEN_SEMICOLON) &&
           !check(parser, SLP_TOKEN_EOF)) {
      if (node->as.for_stmt.init_count >= init_capacity) {
        init_capacity *= 2;
        node->as.for_stmt.initializer =
            (SlpASTNode **)parser->allocator->reallocate(
                node->as.for_stmt.initializer,
                sizeof(SlpASTNode *) * init_capacity,
                parser->allocator->user_data);
      }
      node->as.for_stmt.initializer[node->as.for_stmt.init_count++] =
          expression(parser);
      if (!match(parser, SLP_TOKEN_COMMA))
        break;
    }
  }
  consume(parser, SLP_TOKEN_SEMICOLON,
          "Expect ';' after for loop initializer.");

  // Parse condition
  node->as.for_stmt.condition = NULL;
  if (!check(parser, SLP_TOKEN_SEMICOLON)) {
    node->as.for_stmt.condition = expression(parser);
  }
  consume(parser, SLP_TOKEN_SEMICOLON,
          "Expect ';' after for loop condition.");

  // Parse increment(s)
  node->as.for_stmt.increment = NULL;
  node->as.for_stmt.inc_count = 0;
  if (!check(parser, SLP_TOKEN_RIGHT_PAREN) &&
      !check(parser, SLP_TOKEN_EOF)) {
    size_t inc_capacity = 4;
    node->as.for_stmt.increment =
        (SlpASTNode **)parser->allocator->reallocate(
            NULL, sizeof(SlpASTNode *) * inc_capacity,
            parser->allocator->user_data);

    while (!check(parser, SLP_TOKEN_RIGHT_PAREN) &&
           !check(parser, SLP_TOKEN_EOF)) {
      if (node->as.for_stmt.inc_count >= inc_capacity) {
        inc_capacity *= 2;
        node->as.for_stmt.increment =
            (SlpASTNode **)parser->allocator->reallocate(
                node->as.for_stmt.increment,
                sizeof(SlpASTNode *) * inc_capacity,
                parser->allocator->user_data);
      }
      node->as.for_stmt.increment[node->as.for_stmt.inc_count++] =
          expression(parser);
      if (!match(parser, SLP_TOKEN_COMMA))
        break;
    }
  }
  consume(parser, SLP_TOKEN_RIGHT_PAREN,
          "Expect ')' after for loop increment.");

  node->as.for_stmt.body = statement(parser);
  return node;
}

static SlpASTNode *foreach_statement(SlpParser *parser) {
  SlpASTNode *key_node = NULL, *val_node = NULL;
  if (match(parser, SLP_TOKEN_SCALAR)) {
    val_node = scalar(parser, NULL, false);
    if (match(parser, SLP_TOKEN_ARROW)) {
      key_node = val_node;
      consume(parser, SLP_TOKEN_SCALAR, "Expect value variable.");
      val_node = scalar(parser, NULL, false);
    }
  }
  consume(parser, SLP_TOKEN_LEFT_PAREN,
          "Expect '(' after foreach variable.");
  SlpASTNode *source = expression(parser);
  consume(parser, SLP_TOKEN_RIGHT_PAREN, "Expect ')' after collection.");
  SlpASTNode *body = block(parser);
  SlpASTNode *node = allocate_node(parser, SLP_AST_FOREACH);
  node->as.foreach.index = (key_node) ? key_node->as.string_val : NULL;
  
  if (val_node) {
      node->as.foreach.value = val_node->as.string_val;
  } else {
      char *stub = (char *)parser->allocator->reallocate(NULL, 9, parser->allocator->user_data);
      if (stub) slp_utils_strcpy(stub, "stub_var");
      node->as.foreach.value = stub;
  }
  
  if (key_node) SLP_FREE(parser->allocator, key_node);
  if (val_node) SLP_FREE(parser->allocator, val_node);

  node->as.foreach.generator = source;
  node->as.foreach.body = body;
  return node;
}

static SlpASTNode *try_statement(SlpParser *parser) {
  SlpASTNode *node = allocate_node(parser, SLP_AST_TRY_CATCH);
  node->as.try_catch.body = block(parser);
  if (match(parser, SLP_TOKEN_CATCH)) {
    // Advance to the scalar token
    advance(parser);
    SlpASTNode *var_node = scalar(parser, NULL, false);
    node->as.try_catch.value = var_node->as.string_val;
    SLP_FREE(parser->allocator, var_node);
    node->as.try_catch.handler = block(parser);
  } else
    node->as.try_catch.handler = NULL;
  return node;
}

static SlpASTNode *statement(SlpParser *parser) {
  if (match(parser, SLP_TOKEN_SEMICOLON))
    return allocate_node(parser, SLP_AST_NOP);
  if (match(parser, SLP_TOKEN_IF))
    return if_statement(parser);
  if (match(parser, SLP_TOKEN_WHILE))
    return while_statement(parser);
  if (match(parser, SLP_TOKEN_FOR))
    return for_statement(parser);
  if (match(parser, SLP_TOKEN_FOREACH))
    return foreach_statement(parser);
  if (match(parser, SLP_TOKEN_TRY))
    return try_statement(parser);
  if (check(parser, SLP_TOKEN_LEFT_BRACE))
    return block(parser);
  return expression_statement(parser);
}

static SlpASTNode *import_statement(SlpParser *parser) {
  SlpASTNode *node = allocate_node(parser, SLP_AST_IMPORT);
  node->as.import_stmt.target = NULL;
  node->as.import_stmt.path = NULL;

  // Parse import target (e.g., java.util.*)
  // Build up the target string
  char target_buffer[512];
  size_t target_len = 0;
  target_buffer[0] = '\0';

  if (check(parser, SLP_TOKEN_ID) || check(parser, SLP_TOKEN_STRING)) {
    if (match(parser, SLP_TOKEN_ID) || match(parser, SLP_TOKEN_STRING)) {
      const char *part =
          slp_lexer_copy_lexeme(&parser->lexer, &parser->previous);
      size_t part_len = slp_utils_strlen(part);
      if (target_len + part_len < sizeof(target_buffer) - 1) {
        slp_utils_strcpy(target_buffer + target_len, part);
        target_len += part_len;
      }
      SLP_FREE(parser->allocator, (void *)part);

      // Check for dotted names (e.g., java.util.*)
      while (match(parser, SLP_TOKEN_DOT)) {
        if (target_len + 1 < sizeof(target_buffer) - 1) {
          target_buffer[target_len++] = '.';
          target_buffer[target_len] = '\0';
        }
        if (match(parser, SLP_TOKEN_ID) ||
            match(parser, SLP_TOKEN_STRING) ||
            match(parser, SLP_TOKEN_STAR)) {
          const char *next_part =
              slp_lexer_copy_lexeme(&parser->lexer, &parser->previous);
          size_t next_len = slp_utils_strlen(next_part);
          if (target_len + next_len < sizeof(target_buffer) - 1) {
            slp_utils_strcpy(target_buffer + target_len, next_part);
            target_len += next_len;
          }
          SLP_FREE(parser->allocator, (void *)next_part);
        }
      }
    }
  }

  // Store the target string
  if (target_len > 0) {
    char *stored_target = (char *)parser->allocator->reallocate(
        NULL, target_len + 1, parser->allocator->user_data);
    slp_utils_strcpy(stored_target, target_buffer);
    node->as.import_stmt.target = stored_target;
  }

  // Check for optional FROM ':' path
  if (match(parser, SLP_TOKEN_FROM)) {
    consume(parser, SLP_TOKEN_COLON, "Expect ':' after FROM.");
    if (match(parser, SLP_TOKEN_STRING) ||
        match(parser, SLP_TOKEN_IMPORT_PATH) ||
        match(parser, SLP_TOKEN_LITERAL)) {
      node->as.import_stmt.path =
          slp_lexer_copy_lexeme(&parser->lexer, &parser->previous);
    }
  }

  // Consume the semicolon at the end
  match(parser, SLP_TOKEN_SEMICOLON);

  return node;
}

static SlpASTNode *declaration(SlpParser *parser) {
  if (match(parser, SLP_TOKEN_SUB) || match(parser, SLP_TOKEN_INLINE)) {
    return bridge(parser, NULL, false);
  }
  if (match(parser, SLP_TOKEN_IMPORT)) {
    return import_statement(parser);
  }

  // Generic Bridge Detection: ID [ID|STRING|LITERAL]? {
  if (check(parser, SLP_TOKEN_ID)) {
    const char *saved_current = parser->lexer.current;
    int saved_line = parser->lexer.line;
    SlpToken next = slp_lexer_scan_token(&parser->lexer);
    parser->lexer.current = saved_current;
    parser->lexer.line = saved_line;

    if (next.type == SLP_TOKEN_ID || next.type == SLP_TOKEN_STRING ||
        next.type == SLP_TOKEN_LITERAL ||
        next.type == SLP_TOKEN_LEFT_BRACE) {
      advance(parser); // Consume the keyword identifier
      return bridge(parser, NULL, false);
    }
  }

  return statement(parser);
}

static SlpASTNode *block(SlpParser *parser) {
  consume(parser, SLP_TOKEN_LEFT_BRACE, "Expect '{' to start block.");
  SlpASTNode *node = allocate_node(parser, SLP_AST_BLOCK);
  node->as.block.capacity = 8;
  node->as.block.count = 0;
  node->as.block.statements = (SlpASTNode **)parser->allocator->reallocate(
      NULL, sizeof(SlpASTNode *) * node->as.block.capacity,
      parser->allocator->user_data);
  while (!check(parser, SLP_TOKEN_RIGHT_BRACE) &&
         !check(parser, SLP_TOKEN_EOF) && !parser->had_error) {
    SlpASTNode *stmt = declaration(parser);
    if (stmt) {
      if (node->as.block.count >= node->as.block.capacity) {
        node->as.block.capacity *= 2;
        node->as.block.statements =
            (SlpASTNode **)parser->allocator->reallocate(
                node->as.block.statements,
                sizeof(SlpASTNode *) * node->as.block.capacity,
                parser->allocator->user_data);
      }
      node->as.block.statements[node->as.block.count++] = stmt;
    }
  }
  consume(parser, SLP_TOKEN_RIGHT_BRACE, "Expect '}' after block.");
  return node;
}

SlpASTNode **parse_args(SlpParser *parser, size_t *count,
                           SlpTokenType endToken) {
  size_t capacity = 4;
  *count = 0;
  SlpASTNode **args = (SlpASTNode **)parser->allocator->reallocate(
      NULL, sizeof(SlpASTNode *) * capacity, parser->allocator->user_data);
  if (!check(parser, endToken)) {
    do {
      if (check(parser, endToken))
        break;
      if (*count >= capacity) {
        capacity *= 2;
        args = (SlpASTNode **)parser->allocator->reallocate(
            args, sizeof(SlpASTNode *) * capacity,
            parser->allocator->user_data);
      }
      SlpASTNode *expr = NULL;

      // Special case: 'x' (as LOWER_X or ID) followed by '=>' should be treated
      // as an identifier This is used in hash literals like: hash(x => "value")
      if ((check(parser, SLP_TOKEN_LOWER_X) ||
           check(parser, SLP_TOKEN_ID)) &&
          parser->current.length == 1 && parser->current.start[0] == 'x') {
        // Check if the next token is =>
        const char *saved_current = parser->lexer.current;
        SlpToken next_token = slp_lexer_scan_token(&parser->lexer);
        parser->lexer.current = saved_current; // Restore position

        if (next_token.type == SLP_TOKEN_ARROW) {
          advance(parser);
          expr = allocate_node(parser, SLP_AST_IDENTIFIER);
          expr->as.string_val =
              slp_lexer_copy_lexeme(&parser->lexer, &parser->previous);
        } else {
          expr = expression(parser);
        }
      } else {
        expr = expression(parser);
      }

      SlpASTNode *arg_node = allocate_node(parser, SLP_AST_ARG);
      if (match(parser, SLP_TOKEN_ARROW)) {
        arg_node->as.arg.name = expr;
        arg_node->as.arg.value = expression(parser);
      } else {
        arg_node->as.arg.name = NULL;
        arg_node->as.arg.value = expr;
      }
      args[(*count)++] = arg_node;
      if (check(parser, endToken))
        break;
    } while ((match(parser, SLP_TOKEN_COMMA) ||
              (!check(parser, endToken) && !check(parser, SLP_TOKEN_EOF))) &&
             !parser->had_error);
  }
  return args;
}

SlpASTNode *slp_parser_parse(SlpParser *parser) {
  advance(parser);
  if (match(parser, SLP_TOKEN_EOF))
    return NULL;
  SlpASTNode *script = allocate_node(parser, SLP_AST_SCRIPT);
  script->as.block.capacity = 8;
  script->as.block.count = 0;
  script->as.block.statements = (SlpASTNode **)parser->allocator->reallocate(
      NULL, sizeof(SlpASTNode *) * script->as.block.capacity,
      parser->allocator->user_data);
  while (!match(parser, SLP_TOKEN_EOF) && !parser->had_error) {
    SlpASTNode *stmt = declaration(parser);
    if (stmt) {
      if (script->as.block.count >= script->as.block.capacity) {
        script->as.block.capacity *= 2;
        script->as.block.statements =
            (SlpASTNode **)parser->allocator->reallocate(
                script->as.block.statements,
                sizeof(SlpASTNode *) * script->as.block.capacity,
                parser->allocator->user_data);
      }
      script->as.block.statements[script->as.block.count++] = stmt;
    }
  }
  return script;
}
