#include "sleepy_ast.h"
#include "sleepy_utils.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

// Forward declaration of internal recursive formatter
static void format_node(SleepyASTNode *node, SleepyStringBuffer *buffer,
                        int depth);

char *sleepy_ast_format(SleepyASTNode *node, SleepyAllocator *allocator) {
  if (!node || !allocator)
    return NULL;

  SleepyStringBuffer buffer;
  sleepy_string_buffer_init(&buffer, allocator);

  format_node(node, &buffer, 0);

  // Return the formatted string; caller assumes ownership.
  return buffer.data;
}

// --- Bindings Helpers ---

void sleepy_ast_get_children(SleepyASTNode *node, SleepyASTNode ***out_children,
                             size_t *out_count, SleepyAllocator *allocator) {
  *out_children = NULL;
  *out_count = 0;
  if (!node)
    return;

  switch (node->type) {
  case SLEEPY_AST_SCRIPT:
  case SLEEPY_AST_BLOCK: {
    if (node->as.block.count == 0)
      break;
    SleepyASTNode **children = allocator->reallocate(
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
    SleepyASTNode **children = allocator->reallocate(
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
    SleepyASTNode **children = allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    children[0] = node->as.binop.left;
    children[1] = node->as.binop.right;
    *out_children = children;
    *out_count = 2;
    break;
  }
  case SLEEPY_AST_ASSIGNMENT: {
    SleepyASTNode **children = allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    children[0] = node->as.assign.left;
    children[1] = node->as.assign.right;
    *out_children = children;
    *out_count = 2;
    break;
  }
  case SLEEPY_AST_ARG: {
    SleepyASTNode **children = allocator->reallocate(
        NULL, 1 * sizeof(SleepyASTNode *), allocator->user_data);
    children[0] = node->as.arg.value;
    *out_children = children;
    *out_count = 1;
    break;
  }
  case SLEEPY_AST_ENV_BRIDGE: {
    if (node->as.env_bridge.body) {
      SleepyASTNode **children = allocator->reallocate(
          NULL, 1 * sizeof(SleepyASTNode *), allocator->user_data);
      children[0] = node->as.env_bridge.body;
      *out_children = children;
      *out_count = 1;
    }
    break;
  }
  case SLEEPY_AST_IF: {
    SleepyASTNode **children = allocator->reallocate(
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
    SleepyASTNode **children = allocator->reallocate(
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
    if (total_count == 0)
      break;

    // Use the allocator to allocate a temporary array of children
    SleepyASTNode **children = allocator->reallocate(
        NULL, total_count * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;

    for (size_t i = 0; i < node->as.for_stmt.init_count; i++) {
      children[count++] = node->as.for_stmt.initializer[i];
    }
    if (node->as.for_stmt.condition) {
      children[count++] = node->as.for_stmt.condition;
    }
    for (size_t i = 0; i < node->as.for_stmt.inc_count; i++) {
      children[count++] = node->as.for_stmt.increment[i];
    }
    if (node->as.for_stmt.body) {
      children[count++] = node->as.for_stmt.body;
    }

    if (count > 0) {
      *out_children = children;
      *out_count = count;
    } else {
      allocator->reallocate(children, 0, allocator->user_data);
    }
    break;
  }
  case SLEEPY_AST_FOREACH: {
    SleepyASTNode **children = allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.foreach.generator)
      children[count++] = node->as.foreach.generator;
    if (node->as.foreach.body)
      children[count++] = node->as.foreach.body;
    if (count > 0) {
      *out_children = children;
      *out_count = count;
    } else {
      allocator->reallocate(children, 0, allocator->user_data);
    }
    break;
  }
  case SLEEPY_AST_ASSIGN_LOOP: {
    SleepyASTNode **children = allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.assign_loop.generator)
      children[count++] = node->as.assign_loop.generator;
    if (node->as.assign_loop.body)
      children[count++] = node->as.assign_loop.body;
    if (count > 0) {
      *out_children = children;
      *out_count = count;
    } else {
      allocator->reallocate(children, 0, allocator->user_data);
    }
    break;
  }
  case SLEEPY_AST_INDEX: {
    SleepyASTNode **children = allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.index.container)
      children[count++] = node->as.index.container;
    if (node->as.index.element)
      children[count++] = node->as.index.element;
    if (count > 0) {
      *out_children = children;
      *out_count = count;
    } else {
      allocator->reallocate(children, 0, allocator->user_data);
    }
    break;
  }
  case SLEEPY_AST_OBJ_EXPR: {
    size_t total_count = 2 + node->as.obj_expr.arg_count;
    SleepyASTNode **children = allocator->reallocate(
        NULL, total_count * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;

    if (node->as.obj_expr.target)
      children[count++] = node->as.obj_expr.target;
    if (node->as.obj_expr.message)
      children[count++] = node->as.obj_expr.message;
    for (size_t i = 0; i < node->as.obj_expr.arg_count; i++) {
      children[count++] = node->as.obj_expr.args[i];
    }

    if (count > 0) {
      *out_children = children;
      *out_count = count;
    } else {
      allocator->reallocate(children, 0, allocator->user_data);
    }
    break;
  }
  case SLEEPY_AST_TRY_CATCH: {
    SleepyASTNode **children = allocator->reallocate(
        NULL, 2 * sizeof(SleepyASTNode *), allocator->user_data);
    size_t count = 0;
    if (node->as.try_catch.body)
      children[count++] = node->as.try_catch.body;
    if (node->as.try_catch.handler)
      children[count++] = node->as.try_catch.handler;
    if (count > 0) {
      *out_children = children;
      *out_count = count;
    } else {
      allocator->reallocate(children, 0, allocator->user_data);
    }
    break;
  }
  case SLEEPY_AST_RETURN:
  case SLEEPY_AST_YIELD:
  case SLEEPY_AST_THROW:
  case SLEEPY_AST_ASSERT: {
    if (node->as.control.value) {
      SleepyASTNode **children = allocator->reallocate(
          NULL, 1 * sizeof(SleepyASTNode *), allocator->user_data);
      children[0] = node->as.control.value;
      *out_children = children;
      *out_count = 1;
    }
    break;
  }
  case SLEEPY_AST_UNARYOP: {
    if (node->as.unaryop.operand) {
      SleepyASTNode **children = allocator->reallocate(
          NULL, 1 * sizeof(SleepyASTNode *), allocator->user_data);
      children[0] = node->as.unaryop.operand;
      *out_children = children;
      *out_count = 1;
    }
    break;
  }
  default:
    break;
  }
}

void sleepy_ast_free_children(SleepyASTNode **children,
                              SleepyAllocator *allocator) {
  if (allocator && allocator->reallocate && children) {
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
    return node->as.string_val;
  case SLEEPY_AST_ENV_BRIDGE:
    return node->as.env_bridge.keyword;
  case SLEEPY_AST_CALL:
    if (node->as.call.target &&
        node->as.call.target->type == SLEEPY_AST_IDENTIFIER) {
      return node->as.call.target->as.string_val;
    }
    return NULL;
  default:
    return NULL;
  }
}

long sleepy_ast_get_long(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_LONG)
    return node->as.long_val;
  return 0;
}

double sleepy_ast_get_double(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_NUMBER)
    return node->as.double_val;
  return 0.0;
}

bool sleepy_ast_get_bool(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_BOOLEAN)
    return node->as.boolean;
  return false;
}

const char *sleepy_ast_get_env_bridge_id(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_ENV_BRIDGE)
    return node->as.env_bridge.identifier;
  return NULL;
}

const char *sleepy_ast_get_env_bridge_string(SleepyASTNode *node) {
  if (node && node->type == SLEEPY_AST_ENV_BRIDGE)
    return node->as.env_bridge.string;
  return NULL;
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
    // Use %g for nice double formatting
    snprintf(buf, sizeof(buf), "%g", node->as.double_val);
    sleepy_string_buffer_append_string(buffer, buf, sleepy_utils_strlen(buf));
    break;
  }

  case SLEEPY_AST_LITERAL:
  case SLEEPY_AST_STRING: {
    // Parser already includes quotes
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    break;
  }

  case SLEEPY_AST_SCALAR: {
    sleepy_string_buffer_append_char(buffer, '$');
    sleepy_string_buffer_append_string(
        buffer, node->as.string_val, sleepy_utils_strlen(node->as.string_val));
    break;
  }

  case SLEEPY_AST_IDENTIFIER:
  case SLEEPY_AST_ARRAY:
  case SLEEPY_AST_HASHTABLE:
  case SLEEPY_AST_CLASS_LITERAL: {
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
    sleepy_string_buffer_append_string(buffer, node->as.binop.op.start,
                                       node->as.binop.op.length);
    sleepy_string_buffer_append_char(buffer, ' ');
    format_node(node->as.binop.right, buffer, depth);
    break;
  }

  case SLEEPY_AST_ASSIGNMENT: {
    format_node(node->as.assign.left, buffer, depth);
    sleepy_string_buffer_append_char(buffer, ' ');
    sleepy_string_buffer_append_string(buffer, node->as.assign.op.start,
                                       node->as.assign.op.length);
    sleepy_string_buffer_append_char(buffer, ' ');
    format_node(node->as.assign.right, buffer, depth);
    break;
  }

  case SLEEPY_AST_UNARYOP: {
    // Very basic check for post ops
    bool is_post = (node->as.unaryop.op.start[0] == '+' ||
                    node->as.unaryop.op.start[0] == '-') &&
                   (node->as.unaryop.op.length == 2) && (depth > 0);
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
      sleepy_string_buffer_append_string(
          buffer, node->as.foreach.index,
          sleepy_utils_strlen(node->as.foreach.index));
      sleepy_string_buffer_append_string(buffer, " => ", 4);
    }
    sleepy_string_buffer_append_string(
        buffer, node->as.foreach.value,
        sleepy_utils_strlen(node->as.foreach.value));
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
    sleepy_string_buffer_append_string(
        buffer, node->as.try_catch.value,
        sleepy_utils_strlen(node->as.try_catch.value));
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
    sleepy_string_buffer_append_string(
        buffer, node->as.env_bridge.keyword,
        sleepy_utils_strlen(node->as.env_bridge.keyword));
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
    sleepy_string_buffer_append_string(
        buffer, node->as.import_stmt.target,
        sleepy_utils_strlen(node->as.import_stmt.target));
    if (node->as.import_stmt.path) {
      sleepy_string_buffer_append_string(buffer, " from: ", 7);
      sleepy_string_buffer_append_string(
          buffer, node->as.import_stmt.path,
          sleepy_utils_strlen(node->as.import_stmt.path));
    }
    break;
  }

  case SLEEPY_AST_KV_PAIR:
  case SLEEPY_AST_ARG: {
    if (node->type == SLEEPY_AST_KV_PAIR && node->as.arg.name) {
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

  default:
    sleepy_string_buffer_append_string(buffer, "/* unknown node type */", 23);
    break;
  }
}
