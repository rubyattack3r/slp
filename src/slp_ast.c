#include "slp_ast.h"
#include "slp_lexer.h"
#include "slp_utils.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Forward declaration of internal recursive formatter
static void format_node(SlpASTNode *node, SlpStringBuffer *buffer,
                        int depth);

// Helper for operator token types
static SlpTokenType get_token_type_from_op(const char *op) {
  if (!op)
    return SLP_TOKEN_ERROR;
  if (strcmp(op, "+") == 0)
    return SLP_TOKEN_PLUS;
  if (strcmp(op, "-") == 0)
    return SLP_TOKEN_MINUS;
  if (strcmp(op, "*") == 0)
    return SLP_TOKEN_STAR;
  if (strcmp(op, "/") == 0)
    return SLP_TOKEN_SLASH;
  if (strcmp(op, "%") == 0)
    return SLP_TOKEN_PERCENT;
  if (strcmp(op, "==") == 0)
    return SLP_TOKEN_EQ;
  if (strcmp(op, "!=") == 0)
    return SLP_TOKEN_NE;
  if (strcmp(op, "<") == 0)
    return SLP_TOKEN_LESS;
  if (strcmp(op, "<=") == 0)
    return SLP_TOKEN_LE;
  if (strcmp(op, ">") == 0)
    return SLP_TOKEN_GREATER;
  if (strcmp(op, ">=") == 0)
    return SLP_TOKEN_GE;
  if (strcmp(op, "&&") == 0)
    return SLP_TOKEN_LAND;
  if (strcmp(op, "||") == 0)
    return SLP_TOKEN_LOR;
  if (strcmp(op, "=") == 0)
    return SLP_TOKEN_EQUAL;
  return SLP_TOKEN_ERROR;
}

char *slp_ast_format(SlpASTNode *node, SlpAllocator *allocator) {
  if (!node || !allocator)
    return NULL;

  SlpStringBuffer buffer;
  slp_string_buffer_init(&buffer, allocator);

  format_node(node, &buffer, 0);

  return buffer.data;
}

// --- Bindings Helpers ---

void slp_ast_get_children(SlpASTNode *node, SlpASTNode ***out_children,
                             size_t *out_count, SlpAllocator *allocator) {
  *out_children = NULL;
  *out_count = 0;
  if (!node || !allocator)
    return;

  switch (node->type) {
  case SLP_AST_SCRIPT:
  case SLP_AST_BLOCK: {
    if (node->as.block.count == 0)
      break;
    SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
        NULL, node->as.block.count * sizeof(SlpASTNode *),
        allocator->user_data);
    for (size_t i = 0; i < node->as.block.count; i++) {
      children[i] = node->as.block.statements[i];
    }
    *out_children = children;
    *out_count = node->as.block.count;
    break;
  }
  case SLP_AST_CALL: {
    if (node->as.call.arg_count == 0)
      break;
    SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
        NULL, node->as.call.arg_count * sizeof(SlpASTNode *),
        allocator->user_data);
    for (size_t i = 0; i < node->as.call.arg_count; i++) {
      children[i] = node->as.call.args[i];
    }
    *out_children = children;
    *out_count = node->as.call.arg_count;
    break;
  }
  case SLP_AST_BINOP: {
    SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SlpASTNode *), allocator->user_data);
    children[0] = node->as.binop.left;
    children[1] = node->as.binop.right;
    *out_children = children;
    *out_count = 2;
    break;
  }
  case SLP_AST_ASSIGNMENT: {
    SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SlpASTNode *), allocator->user_data);
    children[0] = node->as.assign.left;
    children[1] = node->as.assign.right;
    *out_children = children;
    *out_count = 2;
    break;
  }
  case SLP_AST_ARG:
  case SLP_AST_KV_PAIR: {
    size_t cnt =
        (node->type == SLP_AST_KV_PAIR && node->as.arg.name) ? 2 : 1;
    SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
        NULL, cnt * sizeof(SlpASTNode *), allocator->user_data);
    if (cnt == 2) {
      children[0] = node->as.arg.name;
      children[1] = node->as.arg.value;
    } else {
      children[0] = node->as.arg.value;
    }
    *out_children = children;
    *out_count = cnt;
    break;
  }
  case SLP_AST_ENV_BRIDGE: {
    if (node->as.env_bridge.body) {
      SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
          NULL, 1 * sizeof(SlpASTNode *), allocator->user_data);
      children[0] = node->as.env_bridge.body;
      *out_children = children;
      *out_count = 1;
    }
    break;
  }
  case SLP_AST_IF: {
    SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
        NULL, 3 * sizeof(SlpASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.if_stmt.condition)
      children[count++] = node->as.if_stmt.condition;
    if (node->as.if_stmt.then_branch)
      children[count++] = node->as.if_stmt.then_branch;
    if (node->as.if_stmt.else_branch)
      children[count++] = node->as.if_stmt.else_branch;
    if (count > 0) {
      *out_children = children;
      *out_count = count;
    } else {
      allocator->reallocate(children, 0, allocator->user_data);
    }
    break;
  }
  case SLP_AST_WHILE: {
    SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SlpASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.while_stmt.condition)
      children[count++] = node->as.while_stmt.condition;
    if (node->as.while_stmt.body)
      children[count++] = node->as.while_stmt.body;
    if (count > 0) {
      *out_children = children;
      *out_count = count;
    } else {
      allocator->reallocate(children, 0, allocator->user_data);
    }
    break;
  }
  case SLP_AST_FOR: {
    size_t total_count =
        node->as.for_stmt.init_count + 1 + node->as.for_stmt.inc_count + 1;
    SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
        NULL, total_count * sizeof(SlpASTNode *), allocator->user_data);
    size_t count = 0;
    for (size_t i = 0; i < node->as.for_stmt.init_count; i++) {
      children[count++] = node->as.for_stmt.initializer[i];
    }
    if (node->as.for_stmt.condition)
      children[count++] = node->as.for_stmt.condition;
    for (size_t i = 0; i < node->as.for_stmt.inc_count; i++) {
      children[count++] = node->as.for_stmt.increment[i];
    }
    if (node->as.for_stmt.body)
      children[count++] = node->as.for_stmt.body;
    if (count > 0) {
      *out_children = children;
      *out_count = count;
    } else {
      allocator->reallocate(children, 0, allocator->user_data);
    }
    break;
  }
  case SLP_AST_FOREACH:
  case SLP_AST_ASSIGN_LOOP: {
    SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SlpASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->type == SLP_AST_FOREACH) {
      if (node->as.foreach.generator)
        children[count++] = node->as.foreach.generator;
      if (node->as.foreach.body)
        children[count++] = node->as.foreach.body;
    } else {
      if (node->as.assign_loop.generator)
        children[count++] = node->as.assign_loop.generator;
      if (node->as.assign_loop.body)
        children[count++] = node->as.assign_loop.body;
    }
    if (count > 0) {
      *out_children = children;
      *out_count = count;
    } else {
      allocator->reallocate(children, 0, allocator->user_data);
    }
    break;
  }
  case SLP_AST_RETURN:
  case SLP_AST_THROW:
  case SLP_AST_ASSERT:
  case SLP_AST_YIELD:
  case SLP_AST_LOCAL:
  case SLP_AST_THIS: {
    if (node->as.control.value) {
      SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
          NULL, 1 * sizeof(SlpASTNode *), allocator->user_data);
      children[0] = node->as.control.value;
      *out_children = children;
      *out_count = 1;
    }
    break;
  }
  case SLP_AST_UNARYOP: {
    SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
        NULL, 1 * sizeof(SlpASTNode *), allocator->user_data);
    children[0] = node->as.unaryop.operand;
    *out_children = children;
    *out_count = 1;
    break;
  }
  case SLP_AST_INDEX: {
    SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SlpASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.index.container)
      children[count++] = node->as.index.container;
    if (node->as.index.element)
      children[count++] = node->as.index.element;
    *out_children = children;
    *out_count = count;
    break;
  }
  case SLP_AST_OBJ_EXPR: {
    size_t total = 2 + node->as.obj_expr.arg_count;
    SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
        NULL, total * sizeof(SlpASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.obj_expr.target)
      children[count++] = node->as.obj_expr.target;
    if (node->as.obj_expr.message)
      children[count++] = node->as.obj_expr.message;
    for (size_t i = 0; i < node->as.obj_expr.arg_count; i++) {
      children[count++] = node->as.obj_expr.args[i];
    }
    *out_children = children;
    *out_count = count;
    break;
  }
  case SLP_AST_TRY_CATCH: {
    SlpASTNode **children = (SlpASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SlpASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.try_catch.body)
      children[count++] = node->as.try_catch.body;
    if (node->as.try_catch.handler)
      children[count++] = node->as.try_catch.handler;
    *out_children = children;
    *out_count = count;
    break;
  }
  default:
    break;
  }
}

void slp_ast_free_children(SlpASTNode **children,
                              SlpAllocator *allocator) {
  if (children && allocator) {
    allocator->reallocate(children, 0, allocator->user_data);
  }
}

const char *slp_ast_get_string(SlpASTNode *node) {
  if (!node)
    return NULL;
  switch (node->type) {
  case SLP_AST_STRING:
  case SLP_AST_LITERAL:
  case SLP_AST_SCALAR:
  case SLP_AST_ARRAY:
  case SLP_AST_HASHTABLE:
  case SLP_AST_IDENTIFIER:
  case SLP_AST_CLASS_LITERAL:
  case SLP_AST_BACKTICK:
  case SLP_AST_ADDRESS:
    return node->as.string_val;
  case SLP_AST_ENV_BRIDGE:
    return node->as.env_bridge.keyword;
  case SLP_AST_CALL:
    if (node->as.call.target &&
        (node->as.call.target->type == SLP_AST_IDENTIFIER ||
         node->as.call.target->type == SLP_AST_HASHTABLE ||
         node->as.call.target->type == SLP_AST_ARRAY)) {
      return node->as.call.target->as.string_val;
    }
    return NULL;
  default:
    return NULL;
  }
}

const char *slp_ast_get_op(SlpASTNode *node) {
  if (!node)
    return NULL;
  switch (node->type) {
  case SLP_AST_BINOP:
    return node->as.binop.op.start;
  case SLP_AST_UNARYOP:
    return node->as.unaryop.op.start;
  case SLP_AST_ASSIGNMENT:
    return node->as.assign.op.start;
  default:
    return NULL;
  }
}

size_t slp_ast_get_op_length(SlpASTNode *node) {
  if (!node)
    return 0;
  switch (node->type) {
  case SLP_AST_BINOP:
    return node->as.binop.op.length;
  case SLP_AST_UNARYOP:
    return node->as.unaryop.op.length;
  case SLP_AST_ASSIGNMENT:
    return node->as.assign.op.length;
  default:
    return 0;
  }
}

long slp_ast_get_long(SlpASTNode *node) {
  return (node && node->type == SLP_AST_LONG) ? node->as.long_val : 0;
}

double slp_ast_get_double(SlpASTNode *node) {
  return (node && node->type == SLP_AST_NUMBER) ? node->as.double_val : 0.0;
}

bool slp_ast_get_bool(SlpASTNode *node) {
  return (node && node->type == SLP_AST_BOOLEAN) ? node->as.boolean : false;
}

const char *slp_ast_get_env_bridge_keyword(SlpASTNode *node) {
  if (node && node->type == SLP_AST_ENV_BRIDGE) {
    return node->as.env_bridge.keyword;
  }
  return NULL;
}

const char *slp_ast_get_env_bridge_id(SlpASTNode *node) {
  return (node && node->type == SLP_AST_ENV_BRIDGE)
             ? node->as.env_bridge.identifier
             : NULL;
}

const char *slp_ast_get_env_bridge_string(SlpASTNode *node) {
  return (node && node->type == SLP_AST_ENV_BRIDGE)
             ? node->as.env_bridge.string
             : NULL;
}

const char *slp_ast_get_foreach_index(SlpASTNode *node) {
  return (node && node->type == SLP_AST_FOREACH) ? node->as.foreach.index
                                                    : NULL;
}

const char *slp_ast_get_foreach_value(SlpASTNode *node) {
  return (node && node->type == SLP_AST_FOREACH) ? node->as.foreach.value
                                                    : NULL;
}

const char *slp_ast_get_import_path(SlpASTNode *node) {
  return (node && node->type == SLP_AST_IMPORT) ? node->as.import_stmt.path
                                                   : NULL;
}

const char *slp_ast_get_try_catch_var(SlpASTNode *node) {
  return (node && node->type == SLP_AST_TRY_CATCH) ? node->as.try_catch.value
                                                      : NULL;
}

size_t slp_ast_get_for_init_count(SlpASTNode *node) {
  return (node && node->type == SLP_AST_FOR) ? node->as.for_stmt.init_count
                                                : 0;
}

size_t slp_ast_get_for_inc_count(SlpASTNode *node) {
  return (node && node->type == SLP_AST_FOR) ? node->as.for_stmt.inc_count
                                                : 0;
}

// --- AST Builder APIs for FFI ---

SlpASTNode *slp_ast_build_node(SlpASTNodeType type, int line,
                                     SlpAllocator *allocator) {
  SlpASTNode *node =
      (SlpASTNode *)allocator->reallocate(NULL, sizeof(SlpASTNode), NULL);
  if (!node)
    return NULL;
  memset(node, 0, sizeof(SlpASTNode));
  node->type = type;
  node->line = line;
  return node;
}

void slp_ast_set_string_val(SlpASTNode *node, const char *str,
                               SlpAllocator *allocator) {
  if (!node || !str || !allocator)
    return;
  size_t len = strlen(str);
  char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
  if (dup) {
    strcpy(dup, str);
    switch (node->type) {
    case SLP_AST_STRING:
    case SLP_AST_LITERAL:
    case SLP_AST_SCALAR:
    case SLP_AST_ARRAY:
    case SLP_AST_HASHTABLE:
    case SLP_AST_IDENTIFIER:
    case SLP_AST_CLASS_LITERAL:
    case SLP_AST_BACKTICK:
    case SLP_AST_ADDRESS:
      node->as.string_val = dup;
      break;
    default:
      allocator->reallocate(dup, 0, NULL);
      break;
    }
  }
}

void slp_ast_set_long_val(SlpASTNode *node, long val) {
  if (node && node->type == SLP_AST_LONG)
    node->as.long_val = val;
}

void slp_ast_set_double_val(SlpASTNode *node, double val) {
  if (node && node->type == SLP_AST_NUMBER)
    node->as.double_val = val;
}

void slp_ast_set_bool_val(SlpASTNode *node, bool val) {
  if (node && node->type == SLP_AST_BOOLEAN)
    node->as.boolean = val;
}

SlpASTNode **slp_ast_allocate_children(size_t count,
                                             SlpAllocator *allocator) {
  if (count == 0)
    return NULL;
  return (SlpASTNode **)allocator->reallocate(
      NULL, count * sizeof(SlpASTNode *), NULL);
}

void slp_ast_set_children(SlpASTNode *node, SlpASTNode **children,
                             size_t count, SlpAllocator *allocator) {
  if (!node)
    return;
  SlpASTNode **arr = NULL;
  if (count > 0 && children) {
    arr = (SlpASTNode **)allocator->reallocate(
        NULL, count * sizeof(SlpASTNode *), NULL);
    for (size_t i = 0; i < count; i++)
      arr[i] = children[i];
  }
  switch (node->type) {
  case SLP_AST_SCRIPT:
  case SLP_AST_BLOCK:
    node->as.block.statements = arr;
    node->as.block.count = count;
    node->as.block.capacity = count;
    break;
  case SLP_AST_CALL:
    node->as.call.args = arr;
    node->as.call.arg_count = count;
    break;
  default:
    if (arr)
      allocator->reallocate(arr, 0, NULL);
    break;
  }
}

void slp_ast_set_binop(SlpASTNode *node, SlpASTNode *left,
                          SlpASTNode *right) {
  if (node && node->type == SLP_AST_BINOP) {
    node->as.binop.left = left;
    node->as.binop.right = right;
  }
}

void slp_ast_set_binop_with_op(SlpASTNode *node, SlpASTNode *left,
                                  SlpASTNode *right, const char *op,
                                  SlpAllocator *allocator) {
  if (node && node->type == SLP_AST_BINOP && allocator) {
    node->as.binop.left = left;
    node->as.binop.right = right;
    node->as.binop.op.type = get_token_type_from_op(op);
    size_t len = strlen(op);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, op);
      node->as.binop.op.start = dup;
      node->as.binop.op.length = len;
    }
  }
}

void slp_ast_set_unaryop_with_op(SlpASTNode *node, SlpASTNode *operand,
                                    const char *op,
                                    SlpAllocator *allocator) {
  if (node && node->type == SLP_AST_UNARYOP && allocator) {
    node->as.unaryop.operand = operand;
    node->as.unaryop.op.type = get_token_type_from_op(op);
    size_t len = strlen(op);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, op);
      node->as.unaryop.op.start = dup;
      node->as.unaryop.op.length = len;
    }
  }
}

void slp_ast_set_if(SlpASTNode *node, SlpASTNode *condition,
                       SlpASTNode *then_branch, SlpASTNode *else_branch) {
  if (node && node->type == SLP_AST_IF) {
    node->as.if_stmt.condition = condition;
    node->as.if_stmt.then_branch = then_branch;
    node->as.if_stmt.else_branch = else_branch;
  }
}

void slp_ast_set_while(SlpASTNode *node, SlpASTNode *condition,
                          SlpASTNode *body) {
  if (node && node->type == SLP_AST_WHILE) {
    node->as.while_stmt.condition = condition;
    node->as.while_stmt.body = body;
  }
}

void slp_ast_set_arg(SlpASTNode *node, SlpASTNode *value) {
  if (node && node->type == SLP_AST_ARG)
    node->as.arg.value = value;
}

void slp_ast_set_arg_with_name(SlpASTNode *node, SlpASTNode *name, SlpASTNode *value) {
  if (node && node->type == SLP_AST_ARG) {
    node->as.arg.name = name;
    node->as.arg.value = value;
  }
}

void slp_ast_set_obj_expr(SlpASTNode *node, SlpASTNode *target,
                             SlpASTNode *message, SlpASTNode **args,
                             size_t arg_count, SlpAllocator *allocator) {
  (void)allocator;
  if (node && node->type == SLP_AST_OBJ_EXPR) {
    node->as.obj_expr.target = target;
    node->as.obj_expr.message = message;
    node->as.obj_expr.args = args;
    node->as.obj_expr.arg_count = arg_count;
  }
}

void slp_ast_set_assignment(SlpASTNode *node, SlpASTNode *target,
                               SlpASTNode *value) {
  if (node && node->type == SLP_AST_ASSIGNMENT) {
    node->as.assign.left = target;
    node->as.assign.right = value;
    node->as.assign.op.type = SLP_TOKEN_EQUAL;
    node->as.assign.op.start = "=";
    node->as.assign.op.length = 1;
  }
}

void slp_ast_set_env_bridge_keyword(SlpASTNode *node, const char *keyword,
                                       SlpAllocator *allocator) {
  if (node && node->type == SLP_AST_ENV_BRIDGE && keyword) {
    size_t len = strlen(keyword);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, keyword);
      node->as.env_bridge.keyword = dup;
    }
  }
}

void slp_ast_set_env_bridge_id(SlpASTNode *node, const char *id,
                                  SlpAllocator *allocator) {
  if (node && node->type == SLP_AST_ENV_BRIDGE && id) {
    size_t len = strlen(id);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, id);
      node->as.env_bridge.identifier = dup;
    }
  }
}

void slp_ast_set_env_bridge_string(SlpASTNode *node, const char *str,
                                      SlpAllocator *allocator) {
  if (node && node->type == SLP_AST_ENV_BRIDGE && str) {
    size_t len = strlen(str);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, str);
      node->as.env_bridge.string = dup;
    }
  }
}

void slp_ast_set_env_bridge_body(SlpASTNode *node, SlpASTNode *body) {
  if (node && node->type == SLP_AST_ENV_BRIDGE)
    node->as.env_bridge.body = body;
}

void slp_ast_set_foreach(SlpASTNode *node, const char *index,
                            const char *value, SlpASTNode *generator,
                            SlpASTNode *body, SlpAllocator *allocator) {
  if (node && node->type == SLP_AST_FOREACH) {
    if (index) {
      size_t len = strlen(index);
      char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
      if (dup) {
        strcpy(dup, index);
        node->as.foreach.index = dup;
      }
    }
    if (value) {
      size_t len = strlen(value);
      char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
      if (dup) {
        strcpy(dup, value);
        node->as.foreach.value = dup;
      }
    }
    node->as.foreach.generator = generator;
    node->as.foreach.body = body;
  }
}

void slp_ast_set_for(SlpASTNode *node, SlpASTNode **init,
                        size_t init_cnt, SlpASTNode *cond,
                        SlpASTNode **inc, size_t inc_cnt,
                        SlpASTNode *body, SlpAllocator *allocator) {
  (void)allocator;
  if (node && node->type == SLP_AST_FOR) {
    node->as.for_stmt.initializer = init;
    node->as.for_stmt.init_count = init_cnt;
    node->as.for_stmt.condition = cond;
    node->as.for_stmt.increment = inc;
    node->as.for_stmt.inc_count = inc_cnt;
    node->as.for_stmt.body = body;
  }
}

void slp_ast_set_return(SlpASTNode *node, SlpASTNode *value) {
  if (node && node->type == SLP_AST_RETURN)
    node->as.control.value = value;
}

void slp_ast_set_break(SlpASTNode *node) {
  if (node && node->type == SLP_AST_BREAK) {
  }
}

void slp_ast_set_continue(SlpASTNode *node) {
  if (node && node->type == SLP_AST_CONTINUE) {
  }
}

void slp_ast_set_yield(SlpASTNode *node, SlpASTNode *value) {
  if (node && node->type == SLP_AST_YIELD)
    node->as.control.value = value;
}

void slp_ast_set_throw(SlpASTNode *node, SlpASTNode *value) {
  if (node && node->type == SLP_AST_THROW)
    node->as.control.value = value;
}

void slp_ast_set_assert(SlpASTNode *node, SlpASTNode *value) {
  if (node && node->type == SLP_AST_ASSERT)
    node->as.control.value = value;
}

void slp_ast_set_try_catch(SlpASTNode *node, SlpASTNode *body,
                              const char *var, SlpASTNode *handler,
                              SlpAllocator *allocator) {
  if (node && node->type == SLP_AST_TRY_CATCH) {
    node->as.try_catch.body = body;
    if (var) {
      size_t len = strlen(var);
      char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
      if (dup) {
        strcpy(dup, var);
        node->as.try_catch.value = dup;
      }
    }
    node->as.try_catch.handler = handler;
  }
}

void slp_ast_set_import(SlpASTNode *node, const char *path,
                           SlpAllocator *allocator) {
  if (node && node->type == SLP_AST_IMPORT && path) {
    size_t len = strlen(path);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, path);
      node->as.import_stmt.path = dup;
    }
  }
}

void slp_ast_set_backtick(SlpASTNode *node, const char *cmd,
                             SlpAllocator *allocator) {
  if (node && node->type == SLP_AST_BACKTICK && cmd) {
    size_t len = strlen(cmd);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, cmd);
      node->as.string_val = dup;
    }
  }
}

void slp_ast_set_call_target(SlpASTNode *node, const char *target,
                                SlpAllocator *allocator) {
  if (node && node->type == SLP_AST_CALL && target) {
    SlpASTNode *id =
        slp_ast_build_node(SLP_AST_IDENTIFIER, node->line, allocator);
    slp_ast_set_string_val(id, target, allocator);
    node->as.call.target = id;
  }
}

// Memory Management for FFI-built nodes
void slp_parser_free_node(SlpASTNode *node, SlpAllocator *allocator) {
  if (!node || !allocator)
    return;

  // 1. Free Value Strings
  switch (node->type) {
  case SLP_AST_STRING:
  case SLP_AST_LITERAL:
  case SLP_AST_SCALAR:
  case SLP_AST_ARRAY:
  case SLP_AST_HASHTABLE:
  case SLP_AST_IDENTIFIER:
  case SLP_AST_CLASS_LITERAL:
  case SLP_AST_BACKTICK:
  case SLP_AST_ADDRESS:
    if (node->as.string_val)
      allocator->reallocate((void *)node->as.string_val, 0, NULL);
    break;
  default:
    break;
  }

  // 2. Free recursive children and specific non-list children
  switch (node->type) {
  case SLP_AST_SCRIPT:
  case SLP_AST_BLOCK:
    if (node->as.block.statements) {
      for (size_t i = 0; i < node->as.block.count; i++) {
        if (node->as.block.statements[i])
          slp_parser_free_node(node->as.block.statements[i], allocator);
      }
      allocator->reallocate(node->as.block.statements, 0, NULL);
    }
    break;
  case SLP_AST_CALL:
    if (node->as.call.args) {
      for (size_t i = 0; i < node->as.call.arg_count; i++) {
        if (node->as.call.args[i])
          slp_parser_free_node(node->as.call.args[i], allocator);
      }
      allocator->reallocate(node->as.call.args, 0, NULL);
    }
    if (node->as.call.target)
      slp_parser_free_node(node->as.call.target, allocator);
    break;
  case SLP_AST_ENV_BRIDGE:
    if (node->as.env_bridge.body) {
      slp_parser_free_node(node->as.env_bridge.body, allocator);
    }
    if (node->as.env_bridge.keyword) {
      allocator->reallocate((void *)node->as.env_bridge.keyword, 0, NULL);
    }
    if (node->as.env_bridge.identifier) {
      allocator->reallocate((void *)node->as.env_bridge.identifier, 0, NULL);
    }
    if (node->as.env_bridge.string) {
      allocator->reallocate((void *)node->as.env_bridge.string, 0, NULL);
    }
    break;
  case SLP_AST_BINOP:
    if (node->as.binop.left)
      slp_parser_free_node(node->as.binop.left, allocator);
    if (node->as.binop.right)
      slp_parser_free_node(node->as.binop.right, allocator);
    break;
  case SLP_AST_UNARYOP:
    if (node->as.unaryop.operand)
      slp_parser_free_node(node->as.unaryop.operand, allocator);
    break;
  case SLP_AST_IF:
    if (node->as.if_stmt.condition)
      slp_parser_free_node(node->as.if_stmt.condition, allocator);
    if (node->as.if_stmt.then_branch)
      slp_parser_free_node(node->as.if_stmt.then_branch, allocator);
    if (node->as.if_stmt.else_branch)
      slp_parser_free_node(node->as.if_stmt.else_branch, allocator);
    break;
  case SLP_AST_WHILE:
    if (node->as.while_stmt.condition)
      slp_parser_free_node(node->as.while_stmt.condition, allocator);
    if (node->as.while_stmt.body)
      slp_parser_free_node(node->as.while_stmt.body, allocator);
    break;
  case SLP_AST_FOR:
    if (node->as.for_stmt.initializer) {
      for (size_t i = 0; i < node->as.for_stmt.init_count; i++)
        if (node->as.for_stmt.initializer[i])
          slp_parser_free_node(node->as.for_stmt.initializer[i], allocator);
      allocator->reallocate(node->as.for_stmt.initializer, 0, NULL);
    }
    if (node->as.for_stmt.condition)
      slp_parser_free_node(node->as.for_stmt.condition, allocator);
    if (node->as.for_stmt.increment) {
      for (size_t i = 0; i < node->as.for_stmt.inc_count; i++)
        if (node->as.for_stmt.increment[i])
          slp_parser_free_node(node->as.for_stmt.increment[i], allocator);
      allocator->reallocate(node->as.for_stmt.increment, 0, NULL);
    }
    if (node->as.for_stmt.body)
      slp_parser_free_node(node->as.for_stmt.body, allocator);
    break;
  case SLP_AST_FOREACH:
    if (node->as.foreach.generator)
      slp_parser_free_node(node->as.foreach.generator, allocator);
    if (node->as.foreach.body)
      slp_parser_free_node(node->as.foreach.body, allocator);
    if (node->as.foreach.index)
      allocator->reallocate((void *)node->as.foreach.index, 0, NULL);
    if (node->as.foreach.value)
      allocator->reallocate((void *)node->as.foreach.value, 0, NULL);
    break;
  case SLP_AST_ASSIGNMENT:
    if (node->as.assign.left)
      slp_parser_free_node(node->as.assign.left, allocator);
    if (node->as.assign.right)
      slp_parser_free_node(node->as.assign.right, allocator);
    break;
  case SLP_AST_ARG:
  case SLP_AST_KV_PAIR:
    if (node->as.arg.value)
      slp_parser_free_node(node->as.arg.value, allocator);
    if (node->type == SLP_AST_KV_PAIR && node->as.arg.name)
      slp_parser_free_node(node->as.arg.name, allocator);
    break;
  case SLP_AST_TRY_CATCH:
    if (node->as.try_catch.body)
      slp_parser_free_node(node->as.try_catch.body, allocator);
    if (node->as.try_catch.handler)
      slp_parser_free_node(node->as.try_catch.handler, allocator);
    if (node->as.try_catch.value)
      allocator->reallocate((void *)node->as.try_catch.value, 0, NULL);
    break;
  case SLP_AST_RETURN:
  case SLP_AST_THROW:
  case SLP_AST_ASSERT:
  case SLP_AST_YIELD:
  case SLP_AST_LOCAL:
  case SLP_AST_THIS:
    if (node->as.control.value)
      slp_parser_free_node(node->as.control.value, allocator);
    break;
  case SLP_AST_IMPORT:
    if (node->as.import_stmt.path)
      allocator->reallocate((void *)node->as.import_stmt.path, 0, NULL);
    break;
  case SLP_AST_INDEX:
    if (node->as.index.container)
      slp_parser_free_node(node->as.index.container, allocator);
    if (node->as.index.element)
      slp_parser_free_node(node->as.index.element, allocator);
    break;
  case SLP_AST_OBJ_EXPR:
    if (node->as.obj_expr.target)
      slp_parser_free_node(node->as.obj_expr.target, allocator);
    if (node->as.obj_expr.message)
      slp_parser_free_node(node->as.obj_expr.message, allocator);
    if (node->as.obj_expr.args) {
      for (size_t i = 0; i < node->as.obj_expr.arg_count; i++)
        if (node->as.obj_expr.args[i])
          slp_parser_free_node(node->as.obj_expr.args[i], allocator);
      allocator->reallocate(node->as.obj_expr.args, 0, NULL);
    }
    break;
  default:
    break;
  }

  // 3. Free the node itself
  allocator->reallocate(node, 0, NULL);
}

static void append_indent(SlpStringBuffer *buffer, int depth) {
  for (int i = 0; i < depth; i++) {
    slp_string_buffer_append_string(buffer, "    ", 4);
  }
}

static void format_node(SlpASTNode *node, SlpStringBuffer *buffer,
                        int depth) {
  if (!node)
    return;

  switch (node->type) {
  case SLP_AST_SCRIPT: {
    for (size_t i = 0; i < node->as.block.count; i++) {
      format_node(node->as.block.statements[i], buffer, depth);
      if (i < node->as.block.count - 1) {
        if (node->as.block.statements[i]->type != SLP_AST_ENV_BRIDGE) {
          slp_string_buffer_append_char(buffer, ';');
        }
        slp_string_buffer_append_char(buffer, '\n');
      }
    }
    break;
  }

  case SLP_AST_BLOCK: {
    slp_string_buffer_append_string(buffer, "{\n", 2);
    for (size_t i = 0; i < node->as.block.count; i++) {
      append_indent(buffer, depth + 1);
      format_node(node->as.block.statements[i], buffer, depth + 1);
      if (node->as.block.statements[i]->type != SLP_AST_ENV_BRIDGE &&
          node->as.block.statements[i]->type != SLP_AST_IF &&
          node->as.block.statements[i]->type != SLP_AST_WHILE &&
          node->as.block.statements[i]->type != SLP_AST_FOR &&
          node->as.block.statements[i]->type != SLP_AST_FOREACH) {
        slp_string_buffer_append_char(buffer, ';');
      }
      slp_string_buffer_append_char(buffer, '\n');
    }
    append_indent(buffer, depth);
    slp_string_buffer_append_char(buffer, '}');
    break;
  }

  case SLP_AST_BOOLEAN: {
    if (node->as.boolean) {
      slp_string_buffer_append_string(buffer, "true", 4);
    } else {
      slp_string_buffer_append_string(buffer, "false", 5);
    }
    break;
  }

  case SLP_AST_NULL: {
    slp_string_buffer_append_string(buffer, "$null", 5);
    break;
  }

  case SLP_AST_LONG: {
    char buf[64];
    snprintf(buf, sizeof(buf), "%ldL", node->as.long_val);
    slp_string_buffer_append_string(buffer, buf, slp_utils_strlen(buf));
    break;
  }

  case SLP_AST_NUMBER: {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", node->as.double_val);
    slp_string_buffer_append_string(buffer, buf, slp_utils_strlen(buf));
    break;
  }

  case SLP_AST_LITERAL:
  case SLP_AST_STRING: {
    slp_string_buffer_append_char(buffer, '"');
    const char *str = node->as.string_val;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == '"' || str[i] == '\\') {
            slp_string_buffer_append_char(buffer, '\\');
        }
        slp_string_buffer_append_char(buffer, str[i]);
    }
    slp_string_buffer_append_char(buffer, '"');
    break;
  }

  case SLP_AST_SCALAR: {
    slp_string_buffer_append_char(buffer, '$');
    slp_string_buffer_append_string(
        buffer, node->as.string_val, slp_utils_strlen(node->as.string_val));
    break;
  }

  case SLP_AST_IDENTIFIER:
  case SLP_AST_CLASS_LITERAL: {
    slp_string_buffer_append_string(
        buffer, node->as.string_val, slp_utils_strlen(node->as.string_val));
    break;
  }

  case SLP_AST_ARRAY: {
    if (node->as.string_val && node->as.string_val[0] != '@') {
      slp_string_buffer_append_char(buffer, '@');
    }
    slp_string_buffer_append_string(
        buffer, node->as.string_val, slp_utils_strlen(node->as.string_val));
    break;
  }

  case SLP_AST_HASHTABLE: {
    if (node->as.string_val && node->as.string_val[0] != '%') {
      slp_string_buffer_append_char(buffer, '%');
    }
    slp_string_buffer_append_string(
        buffer, node->as.string_val, slp_utils_strlen(node->as.string_val));
    break;
  }

  case SLP_AST_BACKTICK: {
    slp_string_buffer_append_char(buffer, '`');
    slp_string_buffer_append_string(
        buffer, node->as.string_val, slp_utils_strlen(node->as.string_val));
    slp_string_buffer_append_char(buffer, '`');
    break;
  }

  case SLP_AST_CALL: {
    format_node(node->as.call.target, buffer, depth);
    slp_string_buffer_append_char(buffer, '(');
    for (size_t i = 0; i < node->as.call.arg_count; i++) {
      format_node(node->as.call.args[i], buffer, depth);
      if (i < node->as.call.arg_count - 1) {
        slp_string_buffer_append_string(buffer, ", ", 2);
      }
    }
    slp_string_buffer_append_char(buffer, ')');
    break;
  }

  case SLP_AST_BINOP: {
    format_node(node->as.binop.left, buffer, depth);
    slp_string_buffer_append_char(buffer, ' ');
    if (node->as.binop.negate) {
      slp_string_buffer_append_char(buffer, '!');
    }
    if (node->as.binop.op.start) {
      slp_string_buffer_append_string(buffer, node->as.binop.op.start,
                                         node->as.binop.op.length);
    } else {
      slp_string_buffer_append_string(buffer, "/* missing op */", 16);
    }
    slp_string_buffer_append_char(buffer, ' ');
    format_node(node->as.binop.right, buffer, depth);
    break;
  }

  case SLP_AST_LVALUE_TUPLE: {
    slp_string_buffer_append_char(buffer, '(');
    for (size_t i = 0; i < node->as.block.count; i++) {
      if (i > 0) {
        slp_string_buffer_append_string(buffer, ", ", 2);
      }
      format_node(node->as.block.statements[i], buffer, depth);
    }
    slp_string_buffer_append_char(buffer, ')');
    break;
  }

  case SLP_AST_ASSIGNMENT: {
    format_node(node->as.assign.left, buffer, depth);
    slp_string_buffer_append_char(buffer, ' ');
    if (node->as.assign.op.start) {
      slp_string_buffer_append_string(buffer, node->as.assign.op.start,
                                         node->as.assign.op.length);
    } else {
      slp_string_buffer_append_char(buffer, '=');
    }
    slp_string_buffer_append_char(buffer, ' ');
    format_node(node->as.assign.right, buffer, depth);
    break;
  }

  case SLP_AST_UNARYOP: {
    if (!node->as.unaryop.op.start) {
      format_node(node->as.unaryop.operand, buffer, depth);
      break;
    }
    bool is_post = (node->as.unaryop.op.start[0] == '+' ||
                    node->as.unaryop.op.start[0] == '-') &&
                   (node->as.unaryop.op.length == 2);
    if (!is_post) {
      slp_string_buffer_append_string(buffer, node->as.unaryop.op.start,
                                         node->as.unaryop.op.length);
    }
    format_node(node->as.unaryop.operand, buffer, depth);
    if (is_post) {
      slp_string_buffer_append_string(buffer, node->as.unaryop.op.start,
                                         node->as.unaryop.op.length);
    }
    break;
  }

  case SLP_AST_IF: {
    slp_string_buffer_append_string(buffer, "if (", 4);
    format_node(node->as.if_stmt.condition, buffer, depth);
    slp_string_buffer_append_string(buffer, ") ", 2);
    format_node(node->as.if_stmt.then_branch, buffer, depth);
    if (node->as.if_stmt.else_branch) {
      slp_string_buffer_append_string(buffer, " else ", 6);
      format_node(node->as.if_stmt.else_branch, buffer, depth);
    }
    break;
  }

  case SLP_AST_WHILE: {
    slp_string_buffer_append_string(buffer, "while (", 7);
    format_node(node->as.while_stmt.condition, buffer, depth);
    slp_string_buffer_append_string(buffer, ") ", 2);
    format_node(node->as.while_stmt.body, buffer, depth);
    break;
  }

  case SLP_AST_FOR: {
    slp_string_buffer_append_string(buffer, "for (", 5);
    for (size_t i = 0; i < node->as.for_stmt.init_count; i++) {
      format_node(node->as.for_stmt.initializer[i], buffer, depth);
      if (i < node->as.for_stmt.init_count - 1) {
        slp_string_buffer_append_string(buffer, ", ", 2);
      }
    }
    slp_string_buffer_append_string(buffer, "; ", 2);
    format_node(node->as.for_stmt.condition, buffer, depth);
    slp_string_buffer_append_string(buffer, "; ", 2);
    for (size_t i = 0; i < node->as.for_stmt.inc_count; i++) {
      format_node(node->as.for_stmt.increment[i], buffer, depth);
      if (i < node->as.for_stmt.inc_count - 1) {
        slp_string_buffer_append_string(buffer, ", ", 2);
      }
    }
    slp_string_buffer_append_string(buffer, ") ", 2);
    format_node(node->as.for_stmt.body, buffer, depth);
    break;
  }

  case SLP_AST_FOREACH: {
    slp_string_buffer_append_string(buffer, "foreach ", 8);
    if (node->as.foreach.index) {
      if (node->as.foreach.index[0] != '$') {
        slp_string_buffer_append_char(buffer, '$');
      }
      slp_string_buffer_append_string(
          buffer, node->as.foreach.index,
          slp_utils_strlen(node->as.foreach.index));
      slp_string_buffer_append_string(buffer, " => ", 4);
    }
    if (node->as.foreach.value) {
      if (node->as.foreach.value[0] != '$') {
        slp_string_buffer_append_char(buffer, '$');
      }
      slp_string_buffer_append_string(
          buffer, node->as.foreach.value,
          slp_utils_strlen(node->as.foreach.value));
    } else {
      slp_string_buffer_append_string(buffer, "$_", 2);
    }
    slp_string_buffer_append_string(buffer, " (", 2);
    format_node(node->as.foreach.generator, buffer, depth);
    slp_string_buffer_append_string(buffer, ") ", 2);
    format_node(node->as.foreach.body, buffer, depth);
    break;
  }

  case SLP_AST_RETURN: {
    slp_string_buffer_append_string(buffer, "return", 6);
    if (node->as.control.value) {
      slp_string_buffer_append_char(buffer, ' ');
      format_node(node->as.control.value, buffer, depth);
    }
    break;
  }

  case SLP_AST_BREAK: {
    slp_string_buffer_append_string(buffer, "break", 5);
    break;
  }

  case SLP_AST_CONTINUE: {
    slp_string_buffer_append_string(buffer, "continue", 8);
    break;
  }

  case SLP_AST_YIELD: {
    slp_string_buffer_append_string(buffer, "yield", 5);
    if (node->as.control.value) {
      slp_string_buffer_append_char(buffer, ' ');
      format_node(node->as.control.value, buffer, depth);
    }
    break;
  }

  case SLP_AST_INDEX: {
    format_node(node->as.index.container, buffer, depth);
    slp_string_buffer_append_char(buffer, '[');
    format_node(node->as.index.element, buffer, depth);
    slp_string_buffer_append_char(buffer, ']');
    break;
  }

  case SLP_AST_OBJ_EXPR: {
    slp_string_buffer_append_char(buffer, '[');
    format_node(node->as.obj_expr.target, buffer, depth);
    if (node->as.obj_expr.message) {
      slp_string_buffer_append_char(buffer, ' ');
      format_node(node->as.obj_expr.message, buffer, depth);
    }
    if (node->as.obj_expr.arg_count > 0) {
      slp_string_buffer_append_string(buffer, ": ", 2);
      for (size_t i = 0; i < node->as.obj_expr.arg_count; i++) {
        format_node(node->as.obj_expr.args[i], buffer, depth);
        if (i < node->as.obj_expr.arg_count - 1) {
          slp_string_buffer_append_string(buffer, ", ", 2);
        }
      }
    }
    slp_string_buffer_append_char(buffer, ']');
    break;
  }

  case SLP_AST_TRY_CATCH: {
    slp_string_buffer_append_string(buffer, "try ", 4);
    format_node(node->as.try_catch.body, buffer, depth);
    slp_string_buffer_append_string(buffer, " catch ", 7);
    if (node->as.try_catch.value) {
      slp_string_buffer_append_string(
          buffer, node->as.try_catch.value,
          slp_utils_strlen(node->as.try_catch.value));
    }
    slp_string_buffer_append_char(buffer, ' ');
    format_node(node->as.try_catch.handler, buffer, depth);
    break;
  }

  case SLP_AST_THROW: {
    slp_string_buffer_append_string(buffer, "throw ", 6);
    format_node(node->as.control.value, buffer, depth);
    break;
  }

  case SLP_AST_ASSERT: {
    slp_string_buffer_append_string(buffer, "assert ", 7);
    format_node(node->as.control.value, buffer, depth);
    break;
  }

  case SLP_AST_ENV_BRIDGE: {
    if (node->as.env_bridge.keyword) {
      slp_string_buffer_append_string(
          buffer, node->as.env_bridge.keyword,
          slp_utils_strlen(node->as.env_bridge.keyword));
    } else {
      slp_string_buffer_append_string(buffer, "/* missing keyword */", 21);
    }

    if (node->as.env_bridge.identifier) {
      slp_string_buffer_append_char(buffer, ' ');
      slp_string_buffer_append_string(
          buffer, node->as.env_bridge.identifier,
          slp_utils_strlen(node->as.env_bridge.identifier));
    }
    if (node->as.env_bridge.string) {
      slp_string_buffer_append_char(buffer, ' ');
      slp_string_buffer_append_string(
          buffer, node->as.env_bridge.string,
          slp_utils_strlen(node->as.env_bridge.string));
    }
    if (node->as.env_bridge.body) {
      slp_string_buffer_append_char(buffer, ' ');
      format_node(node->as.env_bridge.body, buffer, depth);
    }
    break;
  }

  case SLP_AST_IMPORT: {
    slp_string_buffer_append_string(buffer, "import ", 7);
    if (node->as.import_stmt.path) {
      slp_string_buffer_append_string(
          buffer, node->as.import_stmt.path,
          slp_utils_strlen(node->as.import_stmt.path));
    }
    break;
  }

  case SLP_AST_KV_PAIR:
  case SLP_AST_ARG: {
    if (node->as.arg.name) {
      format_node(node->as.arg.name, buffer, depth);
      slp_string_buffer_append_string(buffer, " => ", 4);
    }
    format_node(node->as.arg.value, buffer, depth);
    break;
  }

  case SLP_AST_DONE: {
    slp_string_buffer_append_string(buffer, "done", 4);
    break;
  }

  case SLP_AST_HALT: {
    slp_string_buffer_append_string(buffer, "halt", 4);
    break;
  }

  case SLP_AST_LOCAL: {
    slp_string_buffer_append_string(buffer, "local ", 6);
    format_node(node->as.control.value, buffer, depth);
    break;
  }

  case SLP_AST_THIS: {
    slp_string_buffer_append_string(buffer, "this ", 5);
    format_node(node->as.control.value, buffer, depth);
    break;
  }

  case SLP_AST_NOP: {
    break;
  }
  case SLP_AST_ADDRESS: {
    slp_string_buffer_append_char(buffer, '&');
    slp_string_buffer_append_string(
        buffer, node->as.string_val, slp_utils_strlen(node->as.string_val));
    break;
  }

  default:
    slp_string_buffer_append_string(buffer, "/* unknown node type */", 23);
    break;
  }
}

void slp_ast_set_index(SlpASTNode *node, SlpASTNode *container, SlpASTNode *element) {
    if (!node) return;
    node->as.index.container = container;
    node->as.index.element = element;
}

void slp_ast_set_kv_pair(SlpASTNode *node, SlpASTNode *name, SlpASTNode *value) {
    if (!node) return;
    node->as.arg.name = name;
    node->as.arg.value = value;
    node->as.arg.trailing_sep = false;
}
