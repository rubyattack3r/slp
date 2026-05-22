#include "sleepy_ast.h"
#include "sleepy_lexer.h"
#include "sleepy_utils.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Forward declaration of internal recursive formatter
static void format_node(SleepyASTNode *node, SleepyStringBuffer *buffer,
                        int depth);

// Helper for operator token types
static SleepyTokenType get_token_type_from_op(const char *op) {
  if (!op)
    return SLEEPY_TOKEN_ERROR;
  if (strcmp(op, "+") == 0)
    return SLEEPY_TOKEN_PLUS;
  if (strcmp(op, "-") == 0)
    return SLEEPY_TOKEN_MINUS;
  if (strcmp(op, "*") == 0)
    return SLEEPY_TOKEN_STAR;
  if (strcmp(op, "/") == 0)
    return SLEEPY_TOKEN_SLASH;
  if (strcmp(op, "%") == 0)
    return SLEEPY_TOKEN_PERCENT;
  if (strcmp(op, "==") == 0)
    return SLEEPY_TOKEN_EQ;
  if (strcmp(op, "!=") == 0)
    return SLEEPY_TOKEN_NE;
  if (strcmp(op, "<") == 0)
    return SLEEPY_TOKEN_LESS;
  if (strcmp(op, "<=") == 0)
    return SLEEPY_TOKEN_LE;
  if (strcmp(op, ">") == 0)
    return SLEEPY_TOKEN_GREATER;
  if (strcmp(op, ">=") == 0)
    return SLEEPY_TOKEN_GE;
  if (strcmp(op, "&&") == 0)
    return SLEEPY_TOKEN_LAND;
  if (strcmp(op, "||") == 0)
    return SLEEPY_TOKEN_LOR;
  if (strcmp(op, "=") == 0)
    return SLEEPY_TOKEN_EQUAL;
  return SLEEPY_TOKEN_ERROR;
}

char *sleepy_ast_format(SleepyASTNode *node, SleepyAllocator *allocator) {
  if (!node || !allocator)
    return NULL;

  SleepyStringBuffer buffer;
  sleepy_string_buffer_init(&buffer, allocator);

  format_node(node, &buffer, 0);

  return buffer.data;
}

// --- Bindings Helpers ---

void sleepy_ast_get_children(SleepyASTNode *node, SleepyASTNode ***out_children,
                             size_t *out_count, SleepyAllocator *allocator) {
  *out_children = NULL;
  *out_count = 0;
  if (!node || !allocator)
    return;

  switch (node->type) {
  case SLEEPY_AST_SCRIPT:
  case SLEEPY_AST_BLOCK: {
    if (node->as.block.count == 0)
      break;
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, node->as.block.count * sizeof(SleepyASTNode *),
        allocator->user_data);
    for (size_t i = 0; i < node->as.block.count; i++) {
      children[i] = node->as.block.statements[i];
    }
    *out_children = children;
    *out_count = node->as.block.count;
    break;
  }
  case SLEEPY_AST_CALL: {
    if (node->as.call.arg_count == 0)
      break;
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, node->as.call.arg_count * sizeof(SleepyASTNode *),
        allocator->user_data);
    for (size_t i = 0; i < node->as.call.arg_count; i++) {
      children[i] = node->as.call.args[i];
    }
    *out_children = children;
    *out_count = node->as.call.arg_count;
    break;
  }
  case SLEEPY_AST_BINOP: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    children[0] = node->as.binop.left;
    children[1] = node->as.binop.right;
    *out_children = children;
    *out_count = 2;
    break;
  }
  case SLEEPY_AST_ASSIGNMENT: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    children[0] = node->as.assign.left;
    children[1] = node->as.assign.right;
    *out_children = children;
    *out_count = 2;
    break;
  }
  case SLEEPY_AST_ARG:
  case SLEEPY_AST_KV_PAIR: {
    size_t cnt =
        (node->type == SLEEPY_AST_KV_PAIR && node->as.arg.name) ? 2 : 1;
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, cnt * sizeof(SleepyASTNode *), allocator->user_data);
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
  case SLEEPY_AST_ENV_BRIDGE: {
    if (node->as.env_bridge.body) {
      SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
          NULL, 1 * sizeof(SleepyASTNode *), allocator->user_data);
      children[0] = node->as.env_bridge.body;
      *out_children = children;
      *out_count = 1;
    }
    break;
  }
  case SLEEPY_AST_IF: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 3 * sizeof(SleepyASTNode *), allocator->user_data);
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
  case SLEEPY_AST_WHILE: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
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
  case SLEEPY_AST_FOR: {
    size_t total_count =
        node->as.for_stmt.init_count + 1 + node->as.for_stmt.inc_count + 1;
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, total_count * sizeof(SleepyASTNode *), allocator->user_data);
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
  case SLEEPY_AST_FOREACH:
  case SLEEPY_AST_ASSIGN_LOOP: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->type == SLEEPY_AST_FOREACH) {
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
  case SLEEPY_AST_RETURN:
  case SLEEPY_AST_THROW:
  case SLEEPY_AST_ASSERT:
  case SLEEPY_AST_YIELD:
  case SLEEPY_AST_LOCAL:
  case SLEEPY_AST_THIS: {
    if (node->as.control.value) {
      SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
          NULL, 1 * sizeof(SleepyASTNode *), allocator->user_data);
      children[0] = node->as.control.value;
      *out_children = children;
      *out_count = 1;
    }
    break;
  }
  case SLEEPY_AST_UNARYOP: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 1 * sizeof(SleepyASTNode *), allocator->user_data);
    children[0] = node->as.unaryop.operand;
    *out_children = children;
    *out_count = 1;
    break;
  }
  case SLEEPY_AST_INDEX: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.index.container)
      children[count++] = node->as.index.container;
    if (node->as.index.element)
      children[count++] = node->as.index.element;
    *out_children = children;
    *out_count = count;
    break;
  }
  case SLEEPY_AST_OBJ_EXPR: {
    size_t total = 2 + node->as.obj_expr.arg_count;
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, total * sizeof(SleepyASTNode *), allocator->user_data);
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
  case SLEEPY_AST_TRY_CATCH: {
    SleepyASTNode **children = (SleepyASTNode **)allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
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

void sleepy_ast_free_children(SleepyASTNode **children,
                              SleepyAllocator *allocator) {
  if (children && allocator) {
    allocator->reallocate(children, 0, allocator->user_data);
  }
}

const char *sleepy_ast_get_string(SleepyASTNode *node) {
  if (!node)
    return NULL;
  switch (node->type) {
  case SLEEPY_AST_STRING:
  case SLEEPY_AST_LITERAL:
  case SLEEPY_AST_SCALAR:
  case SLEEPY_AST_ARRAY:
  case SLEEPY_AST_HASHTABLE:
  case SLEEPY_AST_IDENTIFIER:
  case SLEEPY_AST_CLASS_LITERAL:
  case SLEEPY_AST_BACKTICK:
  case SLEEPY_AST_ADDRESS:
    return node->as.string_val;
  case SLEEPY_AST_ENV_BRIDGE:
    return node->as.env_bridge.keyword;
  case SLEEPY_AST_CALL:
    if (node->as.call.target &&
        (node->as.call.target->type == SLEEPY_AST_IDENTIFIER ||
         node->as.call.target->type == SLEEPY_AST_HASHTABLE ||
         node->as.call.target->type == SLEEPY_AST_ARRAY)) {
      return node->as.call.target->as.string_val;
    }
    return NULL;
  default:
    return NULL;
  }
}

const char *sleepy_ast_get_op(SleepyASTNode *node) {
  if (!node)
    return NULL;
  switch (node->type) {
  case SLEEPY_AST_BINOP:
    return node->as.binop.op.start;
  case SLEEPY_AST_UNARYOP:
    return node->as.unaryop.op.start;
  case SLEEPY_AST_ASSIGNMENT:
    return node->as.assign.op.start;
  default:
    return NULL;
  }
}

size_t sleepy_ast_get_op_length(SleepyASTNode *node) {
  if (!node)
    return 0;
  switch (node->type) {
  case SLEEPY_AST_BINOP:
    return node->as.binop.op.length;
  case SLEEPY_AST_UNARYOP:
    return node->as.unaryop.op.length;
  case SLEEPY_AST_ASSIGNMENT:
    return node->as.assign.op.length;
  default:
    return 0;
  }
}

long sleepy_ast_get_long(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_LONG) ? node->as.long_val : 0;
}

double sleepy_ast_get_double(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_NUMBER) ? node->as.double_val : 0.0;
}

bool sleepy_ast_get_bool(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_BOOLEAN) ? node->as.boolean : false;
}

const char *sleepy_ast_get_env_bridge_keyword(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_ENV_BRIDGE) {
    return node->as.env_bridge.keyword;
  }
  return NULL;
}

const char *sleepy_ast_get_env_bridge_id(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_ENV_BRIDGE)
             ? node->as.env_bridge.identifier
             : NULL;
}

const char *sleepy_ast_get_env_bridge_string(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_ENV_BRIDGE)
             ? node->as.env_bridge.string
             : NULL;
}

const char *sleepy_ast_get_foreach_index(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_FOREACH) ? node->as.foreach.index
                                                    : NULL;
}

const char *sleepy_ast_get_foreach_value(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_FOREACH) ? node->as.foreach.value
                                                    : NULL;
}

const char *sleepy_ast_get_import_path(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_IMPORT) ? node->as.import_stmt.path
                                                   : NULL;
}

const char *sleepy_ast_get_try_catch_var(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_TRY_CATCH) ? node->as.try_catch.value
                                                      : NULL;
}

size_t sleepy_ast_get_for_init_count(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_FOR) ? node->as.for_stmt.init_count
                                                : 0;
}

size_t sleepy_ast_get_for_inc_count(SleepyASTNode *node) {
  return (node && node->type == SLEEPY_AST_FOR) ? node->as.for_stmt.inc_count
                                                : 0;
}

// --- AST Builder APIs for FFI ---

SleepyASTNode *sleepy_ast_build_node(SleepyASTNodeType type, int line,
                                     SleepyAllocator *allocator) {
  SleepyASTNode *node =
      (SleepyASTNode *)allocator->reallocate(NULL, sizeof(SleepyASTNode), NULL);
  if (!node)
    return NULL;
  memset(node, 0, sizeof(SleepyASTNode));
  node->type = type;
  node->line = line;
  return node;
}

void sleepy_ast_set_string_val(SleepyASTNode *node, const char *str,
                               SleepyAllocator *allocator) {
  if (!node || !str || !allocator)
    return;
  size_t len = strlen(str);
  char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
  if (dup) {
    strcpy(dup, str);
    switch (node->type) {
    case SLEEPY_AST_STRING:
    case SLEEPY_AST_LITERAL:
    case SLEEPY_AST_SCALAR:
    case SLEEPY_AST_ARRAY:
    case SLEEPY_AST_HASHTABLE:
    case SLEEPY_AST_IDENTIFIER:
    case SLEEPY_AST_CLASS_LITERAL:
    case SLEEPY_AST_BACKTICK:
    case SLEEPY_AST_ADDRESS:
      node->as.string_val = dup;
      break;
    default:
      allocator->reallocate(dup, 0, NULL);
      break;
    }
  }
}

void sleepy_ast_set_long_val(SleepyASTNode *node, long val) {
  if (node && node->type == SLEEPY_AST_LONG)
    node->as.long_val = val;
}

void sleepy_ast_set_double_val(SleepyASTNode *node, double val) {
  if (node && node->type == SLEEPY_AST_NUMBER)
    node->as.double_val = val;
}

void sleepy_ast_set_bool_val(SleepyASTNode *node, bool val) {
  if (node && node->type == SLEEPY_AST_BOOLEAN)
    node->as.boolean = val;
}

SleepyASTNode **sleepy_ast_allocate_children(size_t count,
                                             SleepyAllocator *allocator) {
  if (count == 0)
    return NULL;
  return (SleepyASTNode **)allocator->reallocate(
      NULL, count * sizeof(SleepyASTNode *), NULL);
}

void sleepy_ast_set_children(SleepyASTNode *node, SleepyASTNode **children,
                             size_t count, SleepyAllocator *allocator) {
  if (!node)
    return;
  SleepyASTNode **arr = NULL;
  if (count > 0 && children) {
    arr = (SleepyASTNode **)allocator->reallocate(
        NULL, count * sizeof(SleepyASTNode *), NULL);
    for (size_t i = 0; i < count; i++)
      arr[i] = children[i];
  }
  switch (node->type) {
  case SLEEPY_AST_SCRIPT:
  case SLEEPY_AST_BLOCK:
    node->as.block.statements = arr;
    node->as.block.count = count;
    node->as.block.capacity = count;
    break;
  case SLEEPY_AST_CALL:
    node->as.call.args = arr;
    node->as.call.arg_count = count;
    break;
  default:
    if (arr)
      allocator->reallocate(arr, 0, NULL);
    break;
  }
}

void sleepy_ast_set_binop(SleepyASTNode *node, SleepyASTNode *left,
                          SleepyASTNode *right) {
  if (node && node->type == SLEEPY_AST_BINOP) {
    node->as.binop.left = left;
    node->as.binop.right = right;
  }
}

void sleepy_ast_set_binop_with_op(SleepyASTNode *node, SleepyASTNode *left,
                                  SleepyASTNode *right, const char *op,
                                  SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_BINOP && allocator) {
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

void sleepy_ast_set_unaryop_with_op(SleepyASTNode *node, SleepyASTNode *operand,
                                    const char *op,
                                    SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_UNARYOP && allocator) {
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

void sleepy_ast_set_if(SleepyASTNode *node, SleepyASTNode *condition,
                       SleepyASTNode *then_branch, SleepyASTNode *else_branch) {
  if (node && node->type == SLEEPY_AST_IF) {
    node->as.if_stmt.condition = condition;
    node->as.if_stmt.then_branch = then_branch;
    node->as.if_stmt.else_branch = else_branch;
  }
}

void sleepy_ast_set_while(SleepyASTNode *node, SleepyASTNode *condition,
                          SleepyASTNode *body) {
  if (node && node->type == SLEEPY_AST_WHILE) {
    node->as.while_stmt.condition = condition;
    node->as.while_stmt.body = body;
  }
}

void sleepy_ast_set_arg(SleepyASTNode *node, SleepyASTNode *value) {
  if (node && node->type == SLEEPY_AST_ARG)
    node->as.arg.value = value;
}

void sleepy_ast_set_arg_with_name(SleepyASTNode *node, SleepyASTNode *name, SleepyASTNode *value) {
  if (node && node->type == SLEEPY_AST_ARG) {
    node->as.arg.name = name;
    node->as.arg.value = value;
  }
}

void sleepy_ast_set_obj_expr(SleepyASTNode *node, SleepyASTNode *target,
                             SleepyASTNode *message, SleepyASTNode **args,
                             size_t arg_count, SleepyAllocator *allocator) {
  (void)allocator;
  if (node && node->type == SLEEPY_AST_OBJ_EXPR) {
    node->as.obj_expr.target = target;
    node->as.obj_expr.message = message;
    node->as.obj_expr.args = args;
    node->as.obj_expr.arg_count = arg_count;
  }
}

void sleepy_ast_set_assignment(SleepyASTNode *node, SleepyASTNode *target,
                               SleepyASTNode *value) {
  if (node && node->type == SLEEPY_AST_ASSIGNMENT) {
    node->as.assign.left = target;
    node->as.assign.right = value;
    node->as.assign.op.type = SLEEPY_TOKEN_EQUAL;
    node->as.assign.op.start = "=";
    node->as.assign.op.length = 1;
  }
}

void sleepy_ast_set_env_bridge_keyword(SleepyASTNode *node, const char *keyword,
                                       SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_ENV_BRIDGE && keyword) {
    size_t len = strlen(keyword);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, keyword);
      node->as.env_bridge.keyword = dup;
    }
  }
}

void sleepy_ast_set_env_bridge_id(SleepyASTNode *node, const char *id,
                                  SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_ENV_BRIDGE && id) {
    size_t len = strlen(id);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, id);
      node->as.env_bridge.identifier = dup;
    }
  }
}

void sleepy_ast_set_env_bridge_string(SleepyASTNode *node, const char *str,
                                      SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_ENV_BRIDGE && str) {
    size_t len = strlen(str);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, str);
      node->as.env_bridge.string = dup;
    }
  }
}

void sleepy_ast_set_env_bridge_body(SleepyASTNode *node, SleepyASTNode *body) {
  if (node && node->type == SLEEPY_AST_ENV_BRIDGE)
    node->as.env_bridge.body = body;
}

void sleepy_ast_set_foreach(SleepyASTNode *node, const char *index,
                            const char *value, SleepyASTNode *generator,
                            SleepyASTNode *body, SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_FOREACH) {
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

void sleepy_ast_set_for(SleepyASTNode *node, SleepyASTNode **init,
                        size_t init_cnt, SleepyASTNode *cond,
                        SleepyASTNode **inc, size_t inc_cnt,
                        SleepyASTNode *body, SleepyAllocator *allocator) {
  (void)allocator;
  if (node && node->type == SLEEPY_AST_FOR) {
    node->as.for_stmt.initializer = init;
    node->as.for_stmt.init_count = init_cnt;
    node->as.for_stmt.condition = cond;
    node->as.for_stmt.increment = inc;
    node->as.for_stmt.inc_count = inc_cnt;
    node->as.for_stmt.body = body;
  }
}

void sleepy_ast_set_return(SleepyASTNode *node, SleepyASTNode *value) {
  if (node && node->type == SLEEPY_AST_RETURN)
    node->as.control.value = value;
}

void sleepy_ast_set_break(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_BREAK) {
  }
}

void sleepy_ast_set_continue(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_CONTINUE) {
  }
}

void sleepy_ast_set_yield(SleepyASTNode *node, SleepyASTNode *value) {
  if (node && node->type == SLEEPY_AST_YIELD)
    node->as.control.value = value;
}

void sleepy_ast_set_throw(SleepyASTNode *node, SleepyASTNode *value) {
  if (node && node->type == SLEEPY_AST_THROW)
    node->as.control.value = value;
}

void sleepy_ast_set_assert(SleepyASTNode *node, SleepyASTNode *value) {
  if (node && node->type == SLEEPY_AST_ASSERT)
    node->as.control.value = value;
}

void sleepy_ast_set_try_catch(SleepyASTNode *node, SleepyASTNode *body,
                              const char *var, SleepyASTNode *handler,
                              SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_TRY_CATCH) {
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

void sleepy_ast_set_import(SleepyASTNode *node, const char *path,
                           SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_IMPORT && path) {
    size_t len = strlen(path);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, path);
      node->as.import_stmt.path = dup;
    }
  }
}

void sleepy_ast_set_backtick(SleepyASTNode *node, const char *cmd,
                             SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_BACKTICK && cmd) {
    size_t len = strlen(cmd);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, cmd);
      node->as.string_val = dup;
    }
  }
}

void sleepy_ast_set_call_target(SleepyASTNode *node, const char *target,
                                SleepyAllocator *allocator) {
  if (node && node->type == SLEEPY_AST_CALL && target) {
    SleepyASTNode *id =
        sleepy_ast_build_node(SLEEPY_AST_IDENTIFIER, node->line, allocator);
    sleepy_ast_set_string_val(id, target, allocator);
    node->as.call.target = id;
  }
}

// Memory Management for FFI-built nodes
void sleepy_parser_free_node(SleepyASTNode *node, SleepyAllocator *allocator) {
  if (!node || !allocator)
    return;

  // 1. Free Value Strings
  switch (node->type) {
  case SLEEPY_AST_STRING:
  case SLEEPY_AST_LITERAL:
  case SLEEPY_AST_SCALAR:
  case SLEEPY_AST_ARRAY:
  case SLEEPY_AST_HASHTABLE:
  case SLEEPY_AST_IDENTIFIER:
  case SLEEPY_AST_CLASS_LITERAL:
  case SLEEPY_AST_BACKTICK:
  case SLEEPY_AST_ADDRESS:
    if (node->as.string_val)
      allocator->reallocate((void *)node->as.string_val, 0, NULL);
    break;
  default:
    break;
  }

  // 2. Free recursive children and specific non-list children
  switch (node->type) {
  case SLEEPY_AST_SCRIPT:
  case SLEEPY_AST_BLOCK:
    if (node->as.block.statements) {
      for (size_t i = 0; i < node->as.block.count; i++) {
        if (node->as.block.statements[i])
          sleepy_parser_free_node(node->as.block.statements[i], allocator);
      }
      allocator->reallocate(node->as.block.statements, 0, NULL);
    }
    break;
  case SLEEPY_AST_CALL:
    if (node->as.call.args) {
      for (size_t i = 0; i < node->as.call.arg_count; i++) {
        if (node->as.call.args[i])
          sleepy_parser_free_node(node->as.call.args[i], allocator);
      }
      allocator->reallocate(node->as.call.args, 0, NULL);
    }
    if (node->as.call.target)
      sleepy_parser_free_node(node->as.call.target, allocator);
    break;
  case SLEEPY_AST_ENV_BRIDGE:
    if (node->as.env_bridge.body) {
      sleepy_parser_free_node(node->as.env_bridge.body, allocator);
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
  case SLEEPY_AST_BINOP:
    if (node->as.binop.left)
      sleepy_parser_free_node(node->as.binop.left, allocator);
    if (node->as.binop.right)
      sleepy_parser_free_node(node->as.binop.right, allocator);
    break;
  case SLEEPY_AST_UNARYOP:
    if (node->as.unaryop.operand)
      sleepy_parser_free_node(node->as.unaryop.operand, allocator);
    break;
  case SLEEPY_AST_IF:
    if (node->as.if_stmt.condition)
      sleepy_parser_free_node(node->as.if_stmt.condition, allocator);
    if (node->as.if_stmt.then_branch)
      sleepy_parser_free_node(node->as.if_stmt.then_branch, allocator);
    if (node->as.if_stmt.else_branch)
      sleepy_parser_free_node(node->as.if_stmt.else_branch, allocator);
    break;
  case SLEEPY_AST_WHILE:
    if (node->as.while_stmt.condition)
      sleepy_parser_free_node(node->as.while_stmt.condition, allocator);
    if (node->as.while_stmt.body)
      sleepy_parser_free_node(node->as.while_stmt.body, allocator);
    break;
  case SLEEPY_AST_FOR:
    if (node->as.for_stmt.initializer) {
      for (size_t i = 0; i < node->as.for_stmt.init_count; i++)
        if (node->as.for_stmt.initializer[i])
          sleepy_parser_free_node(node->as.for_stmt.initializer[i], allocator);
      allocator->reallocate(node->as.for_stmt.initializer, 0, NULL);
    }
    if (node->as.for_stmt.condition)
      sleepy_parser_free_node(node->as.for_stmt.condition, allocator);
    if (node->as.for_stmt.increment) {
      for (size_t i = 0; i < node->as.for_stmt.inc_count; i++)
        if (node->as.for_stmt.increment[i])
          sleepy_parser_free_node(node->as.for_stmt.increment[i], allocator);
      allocator->reallocate(node->as.for_stmt.increment, 0, NULL);
    }
    if (node->as.for_stmt.body)
      sleepy_parser_free_node(node->as.for_stmt.body, allocator);
    break;
  case SLEEPY_AST_FOREACH:
    if (node->as.foreach.generator)
      sleepy_parser_free_node(node->as.foreach.generator, allocator);
    if (node->as.foreach.body)
      sleepy_parser_free_node(node->as.foreach.body, allocator);
    if (node->as.foreach.index)
      allocator->reallocate((void *)node->as.foreach.index, 0, NULL);
    if (node->as.foreach.value)
      allocator->reallocate((void *)node->as.foreach.value, 0, NULL);
    break;
  case SLEEPY_AST_ASSIGNMENT:
    if (node->as.assign.left)
      sleepy_parser_free_node(node->as.assign.left, allocator);
    if (node->as.assign.right)
      sleepy_parser_free_node(node->as.assign.right, allocator);
    break;
  case SLEEPY_AST_ARG:
  case SLEEPY_AST_KV_PAIR:
    if (node->as.arg.value)
      sleepy_parser_free_node(node->as.arg.value, allocator);
    if (node->type == SLEEPY_AST_KV_PAIR && node->as.arg.name)
      sleepy_parser_free_node(node->as.arg.name, allocator);
    break;
  case SLEEPY_AST_TRY_CATCH:
    if (node->as.try_catch.body)
      sleepy_parser_free_node(node->as.try_catch.body, allocator);
    if (node->as.try_catch.handler)
      sleepy_parser_free_node(node->as.try_catch.handler, allocator);
    if (node->as.try_catch.value)
      allocator->reallocate((void *)node->as.try_catch.value, 0, NULL);
    break;
  case SLEEPY_AST_RETURN:
  case SLEEPY_AST_THROW:
  case SLEEPY_AST_ASSERT:
  case SLEEPY_AST_YIELD:
  case SLEEPY_AST_LOCAL:
  case SLEEPY_AST_THIS:
    if (node->as.control.value)
      sleepy_parser_free_node(node->as.control.value, allocator);
    break;
  case SLEEPY_AST_IMPORT:
    if (node->as.import_stmt.path)
      allocator->reallocate((void *)node->as.import_stmt.path, 0, NULL);
    break;
  case SLEEPY_AST_INDEX:
    if (node->as.index.container)
      sleepy_parser_free_node(node->as.index.container, allocator);
    if (node->as.index.element)
      sleepy_parser_free_node(node->as.index.element, allocator);
    break;
  case SLEEPY_AST_OBJ_EXPR:
    if (node->as.obj_expr.target)
      sleepy_parser_free_node(node->as.obj_expr.target, allocator);
    if (node->as.obj_expr.message)
      sleepy_parser_free_node(node->as.obj_expr.message, allocator);
    if (node->as.obj_expr.args) {
      for (size_t i = 0; i < node->as.obj_expr.arg_count; i++)
        if (node->as.obj_expr.args[i])
          sleepy_parser_free_node(node->as.obj_expr.args[i], allocator);
      allocator->reallocate(node->as.obj_expr.args, 0, NULL);
    }
    break;
  default:
    break;
  }

  // 3. Free the node itself
  allocator->reallocate(node, 0, NULL);
}

static void append_indent(SleepyStringBuffer *buffer, int depth) {
  for (int i = 0; i < depth; i++) {
    sleepy_string_buffer_append_string(buffer, "    ", 4);
  }
}

static void format_node(SleepyASTNode *node, SleepyStringBuffer *buffer,
                        int depth) {
  if (!node)
    return;

  switch (node->type) {
  case SLEEPY_AST_SCRIPT: {
    for (size_t i = 0; i < node->as.block.count; i++) {
      format_node(node->as.block.statements[i], buffer, depth);
      if (i < node->as.block.count - 1) {
        if (node->as.block.statements[i]->type != SLEEPY_AST_ENV_BRIDGE) {
          sleepy_string_buffer_append_char(buffer, ';');
        }
        sleepy_string_buffer_append_char(buffer, '\n');
      }
    }
    break;
  }

  case SLEEPY_AST_BLOCK: {
    sleepy_string_buffer_append_string(buffer, "{\n", 2);
    for (size_t i = 0; i < node->as.block.count; i++) {
      append_indent(buffer, depth + 1);
      format_node(node->as.block.statements[i], buffer, depth + 1);
      if (node->as.block.statements[i]->type != SLEEPY_AST_ENV_BRIDGE &&
          node->as.block.statements[i]->type != SLEEPY_AST_IF &&
          node->as.block.statements[i]->type != SLEEPY_AST_WHILE &&
          node->as.block.statements[i]->type != SLEEPY_AST_FOR &&
          node->as.block.statements[i]->type != SLEEPY_AST_FOREACH) {
        sleepy_string_buffer_append_char(buffer, ';');
      }
      sleepy_string_buffer_append_char(buffer, '\n');
    }
    append_indent(buffer, depth);
    sleepy_string_buffer_append_char(buffer, '}');
    break;
  }

  case SLEEPY_AST_BOOLEAN: {
    if (node->as.boolean) {
      sleepy_string_buffer_append_string(buffer, "true", 4);
    } else {
      sleepy_string_buffer_append_string(buffer, "false", 5);
    }
    break;
  }

  case SLEEPY_AST_NULL: {
    sleepy_string_buffer_append_string(buffer, "$null", 5);
    break;
  }

  case SLEEPY_AST_LONG: {
    char buf[64];
    snprintf(buf, sizeof(buf), "%ldL", node->as.long_val);
    sleepy_string_buffer_append_string(buffer, buf, sleepy_utils_strlen(buf));
    break;
  }

  case SLEEPY_AST_NUMBER: {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", node->as.double_val);
    sleepy_string_buffer_append_string(buffer, buf, sleepy_utils_strlen(buf));
    break;
  }

  case SLEEPY_AST_LITERAL:
  case SLEEPY_AST_STRING: {
    sleepy_string_buffer_append_char(buffer, '"');
    const char *str = node->as.string_val;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == '"' || str[i] == '\\') {
            sleepy_string_buffer_append_char(buffer, '\\');
        }
        sleepy_string_buffer_append_char(buffer, str[i]);
    }
    sleepy_string_buffer_append_char(buffer, '"');
    break;
  }

  case SLEEPY_AST_SCALAR: {
    sleepy_string_buffer_append_char(buffer, '$');
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    break;
  }

  case SLEEPY_AST_IDENTIFIER:
  case SLEEPY_AST_CLASS_LITERAL: {
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    break;
  }

  case SLEEPY_AST_ARRAY: {
    if (node->as.string_val && node->as.string_val[0] != '@') {
      sleepy_string_buffer_append_char(buffer, '@');
    }
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    break;
  }

  case SLEEPY_AST_HASHTABLE: {
    if (node->as.string_val && node->as.string_val[0] != '%') {
      sleepy_string_buffer_append_char(buffer, '%');
    }
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    break;
  }

  case SLEEPY_AST_BACKTICK: {
    sleepy_string_buffer_append_char(buffer, '`');
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    sleepy_string_buffer_append_char(buffer, '`');
    break;
  }

  case SLEEPY_AST_CALL: {
    format_node(node->as.call.target, buffer, depth);
    sleepy_string_buffer_append_char(buffer, '(');
    for (size_t i = 0; i < node->as.call.arg_count; i++) {
      format_node(node->as.call.args[i], buffer, depth);
      if (i < node->as.call.arg_count - 1) {
        sleepy_string_buffer_append_string(buffer, ", ", 2);
      }
    }
    sleepy_string_buffer_append_char(buffer, ')');
    break;
  }

  case SLEEPY_AST_BINOP: {
    format_node(node->as.binop.left, buffer, depth);
    sleepy_string_buffer_append_char(buffer, ' ');
    if (node->as.binop.negate) {
      sleepy_string_buffer_append_char(buffer, '!');
    }
    if (node->as.binop.op.start) {
      sleepy_string_buffer_append_string(buffer, node->as.binop.op.start,
                                         node->as.binop.op.length);
    } else {
      sleepy_string_buffer_append_string(buffer, "/* missing op */", 16);
    }
    sleepy_string_buffer_append_char(buffer, ' ');
    format_node(node->as.binop.right, buffer, depth);
    break;
  }

  case SLEEPY_AST_LVALUE_TUPLE: {
    sleepy_string_buffer_append_char(buffer, '(');
    for (size_t i = 0; i < node->as.block.count; i++) {
      if (i > 0) {
        sleepy_string_buffer_append_string(buffer, ", ", 2);
      }
      format_node(node->as.block.statements[i], buffer, depth);
    }
    sleepy_string_buffer_append_char(buffer, ')');
    break;
  }

  case SLEEPY_AST_ASSIGNMENT: {
    format_node(node->as.assign.left, buffer, depth);
    sleepy_string_buffer_append_char(buffer, ' ');
    if (node->as.assign.op.start) {
      sleepy_string_buffer_append_string(buffer, node->as.assign.op.start,
                                         node->as.assign.op.length);
    } else {
      sleepy_string_buffer_append_char(buffer, '=');
    }
    sleepy_string_buffer_append_char(buffer, ' ');
    format_node(node->as.assign.right, buffer, depth);
    break;
  }

  case SLEEPY_AST_UNARYOP: {
    if (!node->as.unaryop.op.start) {
      format_node(node->as.unaryop.operand, buffer, depth);
      break;
    }
    bool is_post = (node->as.unaryop.op.start[0] == '+' ||
                    node->as.unaryop.op.start[0] == '-') &&
                   (node->as.unaryop.op.length == 2);
    if (!is_post) {
      sleepy_string_buffer_append_string(buffer, node->as.unaryop.op.start,
                                         node->as.unaryop.op.length);
    }
    format_node(node->as.unaryop.operand, buffer, depth);
    if (is_post) {
      sleepy_string_buffer_append_string(buffer, node->as.unaryop.op.start,
                                         node->as.unaryop.op.length);
    }
    break;
  }

  case SLEEPY_AST_IF: {
    sleepy_string_buffer_append_string(buffer, "if (", 4);
    format_node(node->as.if_stmt.condition, buffer, depth);
    sleepy_string_buffer_append_string(buffer, ") ", 2);
    format_node(node->as.if_stmt.then_branch, buffer, depth);
    if (node->as.if_stmt.else_branch) {
      sleepy_string_buffer_append_string(buffer, " else ", 6);
      format_node(node->as.if_stmt.else_branch, buffer, depth);
    }
    break;
  }

  case SLEEPY_AST_WHILE: {
    sleepy_string_buffer_append_string(buffer, "while (", 7);
    format_node(node->as.while_stmt.condition, buffer, depth);
    sleepy_string_buffer_append_string(buffer, ") ", 2);
    format_node(node->as.while_stmt.body, buffer, depth);
    break;
  }

  case SLEEPY_AST_FOR: {
    sleepy_string_buffer_append_string(buffer, "for (", 5);
    for (size_t i = 0; i < node->as.for_stmt.init_count; i++) {
      format_node(node->as.for_stmt.initializer[i], buffer, depth);
      if (i < node->as.for_stmt.init_count - 1) {
        sleepy_string_buffer_append_string(buffer, ", ", 2);
      }
    }
    sleepy_string_buffer_append_string(buffer, "; ", 2);
    format_node(node->as.for_stmt.condition, buffer, depth);
    sleepy_string_buffer_append_string(buffer, "; ", 2);
    for (size_t i = 0; i < node->as.for_stmt.inc_count; i++) {
      format_node(node->as.for_stmt.increment[i], buffer, depth);
      if (i < node->as.for_stmt.inc_count - 1) {
        sleepy_string_buffer_append_string(buffer, ", ", 2);
      }
    }
    sleepy_string_buffer_append_string(buffer, ") ", 2);
    format_node(node->as.for_stmt.body, buffer, depth);
    break;
  }

  case SLEEPY_AST_FOREACH: {
    sleepy_string_buffer_append_string(buffer, "foreach ", 8);
    if (node->as.foreach.index) {
      if (node->as.foreach.index[0] != '$') {
        sleepy_string_buffer_append_char(buffer, '$');
      }
      sleepy_string_buffer_append_string(
          buffer, node->as.foreach.index,
          sleepy_utils_strlen(node->as.foreach.index));
      sleepy_string_buffer_append_string(buffer, " => ", 4);
    }
    if (node->as.foreach.value) {
      if (node->as.foreach.value[0] != '$') {
        sleepy_string_buffer_append_char(buffer, '$');
      }
      sleepy_string_buffer_append_string(
          buffer, node->as.foreach.value,
          sleepy_utils_strlen(node->as.foreach.value));
    } else {
      sleepy_string_buffer_append_string(buffer, "$_", 2);
    }
    sleepy_string_buffer_append_string(buffer, " (", 2);
    format_node(node->as.foreach.generator, buffer, depth);
    sleepy_string_buffer_append_string(buffer, ") ", 2);
    format_node(node->as.foreach.body, buffer, depth);
    break;
  }

  case SLEEPY_AST_RETURN: {
    sleepy_string_buffer_append_string(buffer, "return", 6);
    if (node->as.control.value) {
      sleepy_string_buffer_append_char(buffer, ' ');
      format_node(node->as.control.value, buffer, depth);
    }
    break;
  }

  case SLEEPY_AST_BREAK: {
    sleepy_string_buffer_append_string(buffer, "break", 5);
    break;
  }

  case SLEEPY_AST_CONTINUE: {
    sleepy_string_buffer_append_string(buffer, "continue", 8);
    break;
  }

  case SLEEPY_AST_YIELD: {
    sleepy_string_buffer_append_string(buffer, "yield", 5);
    if (node->as.control.value) {
      sleepy_string_buffer_append_char(buffer, ' ');
      format_node(node->as.control.value, buffer, depth);
    }
    break;
  }

  case SLEEPY_AST_INDEX: {
    format_node(node->as.index.container, buffer, depth);
    sleepy_string_buffer_append_char(buffer, '[');
    format_node(node->as.index.element, buffer, depth);
    sleepy_string_buffer_append_char(buffer, ']');
    break;
  }

  case SLEEPY_AST_OBJ_EXPR: {
    sleepy_string_buffer_append_char(buffer, '[');
    format_node(node->as.obj_expr.target, buffer, depth);
    if (node->as.obj_expr.message) {
      sleepy_string_buffer_append_char(buffer, ' ');
      format_node(node->as.obj_expr.message, buffer, depth);
    }
    if (node->as.obj_expr.arg_count > 0) {
      sleepy_string_buffer_append_string(buffer, ": ", 2);
      for (size_t i = 0; i < node->as.obj_expr.arg_count; i++) {
        format_node(node->as.obj_expr.args[i], buffer, depth);
        if (i < node->as.obj_expr.arg_count - 1) {
          sleepy_string_buffer_append_string(buffer, ", ", 2);
        }
      }
    }
    sleepy_string_buffer_append_char(buffer, ']');
    break;
  }

  case SLEEPY_AST_TRY_CATCH: {
    sleepy_string_buffer_append_string(buffer, "try ", 4);
    format_node(node->as.try_catch.body, buffer, depth);
    sleepy_string_buffer_append_string(buffer, " catch ", 7);
    if (node->as.try_catch.value) {
      sleepy_string_buffer_append_string(
          buffer, node->as.try_catch.value,
          sleepy_utils_strlen(node->as.try_catch.value));
    }
    sleepy_string_buffer_append_char(buffer, ' ');
    format_node(node->as.try_catch.handler, buffer, depth);
    break;
  }

  case SLEEPY_AST_THROW: {
    sleepy_string_buffer_append_string(buffer, "throw ", 6);
    format_node(node->as.control.value, buffer, depth);
    break;
  }

  case SLEEPY_AST_ASSERT: {
    sleepy_string_buffer_append_string(buffer, "assert ", 7);
    format_node(node->as.control.value, buffer, depth);
    break;
  }

  case SLEEPY_AST_ENV_BRIDGE: {
    if (node->as.env_bridge.keyword) {
      sleepy_string_buffer_append_string(
          buffer, node->as.env_bridge.keyword,
          sleepy_utils_strlen(node->as.env_bridge.keyword));
    } else {
      sleepy_string_buffer_append_string(buffer, "/* missing keyword */", 21);
    }

    if (node->as.env_bridge.identifier) {
      sleepy_string_buffer_append_char(buffer, ' ');
      sleepy_string_buffer_append_string(
          buffer, node->as.env_bridge.identifier,
          sleepy_utils_strlen(node->as.env_bridge.identifier));
    }
    if (node->as.env_bridge.string) {
      sleepy_string_buffer_append_char(buffer, ' ');
      sleepy_string_buffer_append_string(
          buffer, node->as.env_bridge.string,
          sleepy_utils_strlen(node->as.env_bridge.string));
    }
    if (node->as.env_bridge.body) {
      sleepy_string_buffer_append_char(buffer, ' ');
      format_node(node->as.env_bridge.body, buffer, depth);
    }
    break;
  }

  case SLEEPY_AST_IMPORT: {
    sleepy_string_buffer_append_string(buffer, "import ", 7);
    if (node->as.import_stmt.path) {
      sleepy_string_buffer_append_string(
          buffer, node->as.import_stmt.path,
          sleepy_utils_strlen(node->as.import_stmt.path));
    }
    break;
  }

  case SLEEPY_AST_KV_PAIR:
  case SLEEPY_AST_ARG: {
    if (node->as.arg.name) {
      format_node(node->as.arg.name, buffer, depth);
      sleepy_string_buffer_append_string(buffer, " => ", 4);
    }
    format_node(node->as.arg.value, buffer, depth);
    break;
  }

  case SLEEPY_AST_DONE: {
    sleepy_string_buffer_append_string(buffer, "done", 4);
    break;
  }

  case SLEEPY_AST_HALT: {
    sleepy_string_buffer_append_string(buffer, "halt", 4);
    break;
  }

  case SLEEPY_AST_LOCAL: {
    sleepy_string_buffer_append_string(buffer, "local ", 6);
    format_node(node->as.control.value, buffer, depth);
    break;
  }

  case SLEEPY_AST_THIS: {
    sleepy_string_buffer_append_string(buffer, "this ", 5);
    format_node(node->as.control.value, buffer, depth);
    break;
  }

  case SLEEPY_AST_NOP: {
    break;
  }
  case SLEEPY_AST_ADDRESS: {
    sleepy_string_buffer_append_char(buffer, '&');
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    break;
  }

  default:
    sleepy_string_buffer_append_string(buffer, "/* unknown node type */", 23);
    break;
  }
}

void sleepy_ast_set_index(SleepyASTNode *node, SleepyASTNode *container, SleepyASTNode *element) {
    if (!node) return;
    node->as.index.container = container;
    node->as.index.element = element;
}

void sleepy_ast_set_kv_pair(SleepyASTNode *node, SleepyASTNode *name, SleepyASTNode *value) {
    if (!node) return;
    node->as.arg.name = name;
    node->as.arg.value = value;
    node->as.arg.trailing_sep = false;
}
