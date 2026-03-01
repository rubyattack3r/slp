#include "sleepy/sleepy_ast.h"
#include "sleepy/sleepy_utils.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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
// --- AST Builder APIs for FFI ---

SleepyASTNode *sleepy_ast_build_node(SleepyASTNodeType type, int line,
                                     SleepyAllocator *allocator) {
  SleepyASTNode *node =
      (SleepyASTNode *)allocator->reallocate(NULL, sizeof(SleepyASTNode), NULL);
  if (!node)
    return NULL;

  node->type = type;
  node->line = line;
  return node;
}

void sleepy_ast_set_string_val(SleepyASTNode *node, const char *str,
                               SleepyAllocator *allocator) {
  if (!node || !str || !allocator)
    return;

  // Duplicate the string using the allocator
  size_t len = 0;
  while (str[len] != '\0')
    len++;
  char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
  if (dup) {
    for (size_t i = 0; i <= len; i++) {
      dup[i] = str[i];
    }

    // Assign based on node type
    switch (node->type) {
    case SLEEPY_AST_STRING:
    case SLEEPY_AST_LITERAL:
    case SLEEPY_AST_SCALAR:
    case SLEEPY_AST_ARRAY:
    case SLEEPY_AST_HASHTABLE:
    case SLEEPY_AST_IDENTIFIER:
    case SLEEPY_AST_CLASS_LITERAL:
      node->as.string_val = dup;
      break;
    default:
      allocator->reallocate(dup, 0, NULL); // Free if not supported
      break;
    }
  }
}

void sleepy_ast_set_long_val(SleepyASTNode *node, long val) {
  if (node && node->type == SLEEPY_AST_LONG) {
    node->as.long_val = val;
  }
}

void sleepy_ast_set_double_val(SleepyASTNode *node, double val) {
  if (node && node->type == SLEEPY_AST_NUMBER) {
    node->as.double_val = val;
  }
}

void sleepy_ast_set_bool_val(SleepyASTNode *node, bool val) {
  if (node && node->type == SLEEPY_AST_BOOLEAN) {
    node->as.boolean = val;
  }
}

void sleepy_ast_set_children(SleepyASTNode *node, SleepyASTNode **children,
                             size_t count, SleepyAllocator *allocator) {
  if (!node)
    return;

  // Allocate array for children pointers
  SleepyASTNode **child_array = NULL;
  if (count > 0 && children) {
    child_array = (SleepyASTNode **)allocator->reallocate(
        NULL, count * sizeof(SleepyASTNode *), NULL);
    if (!child_array)
      return;
    for (size_t i = 0; i < count; i++) {
      child_array[i] = children[i];
    }
  }

  switch (node->type) {
  case SLEEPY_AST_SCRIPT:
  case SLEEPY_AST_BLOCK:
    node->as.block.statements = child_array;
    node->as.block.count = count;
    node->as.block.capacity = count;
    break;
  case SLEEPY_AST_CALL:
    node->as.call.args = child_array;
    node->as.call.arg_count = count;
    break;
  default:
    if (child_array) {
      allocator->reallocate(child_array, 0, NULL);
    }
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
  if (node && node->type == SLEEPY_AST_ARG) {
    node->as.arg.value = value;
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

void sleepy_ast_set_env_bridge(SleepyASTNode *node, const char *id,
                               const char *str, SleepyASTNode **children,
                               size_t count, SleepyAllocator *allocator) {
  if (!node || node->type != SLEEPY_AST_ENV_BRIDGE || !allocator)
    return;

  if (id) {
    // We unfortunately cannot cleanly re-create string fields for the env
    // bridge Since the struct just expects `const char*` directly, we just
    // allocate strings directly.
    size_t len = strlen(id);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, id);
      node->as.env_bridge.identifier = dup;
    }
  }

  if (str) {
    size_t len = strlen(str);
    char *dup = (char *)allocator->reallocate(NULL, len + 1, NULL);
    if (dup) {
      strcpy(dup, str);
      node->as.env_bridge.string = dup;
    }
  }

  // A bridge's body is a block node generally, but we can wrap it if needed.
  if (count > 0 && children) {
    SleepyASTNode *body_block =
        sleepy_ast_build_node(SLEEPY_AST_BLOCK, node->line, allocator);
    sleepy_ast_set_children(body_block, children, count, allocator);
    node->as.env_bridge.body = body_block;
  }
}

void sleepy_ast_set_call_target(SleepyASTNode *node, const char *target,
                                SleepyAllocator *allocator) {
  if (!node || node->type != SLEEPY_AST_CALL || !target || !allocator)
    return;

  // A call target is technically an expression. In Sleepy, standard calls
  // resolve around identifiers. We'll construct an identifier node as the
  // callee.
  SleepyASTNode *callee =
      sleepy_ast_build_node(SLEEPY_AST_IDENTIFIER, node->line, allocator);
  sleepy_ast_set_string_val(callee, target, allocator);

  node->as.call.target = callee;
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
    if (node->as.string_val)
      allocator->reallocate((void *)node->as.string_val, 0, NULL);
    break;
  default:
    break;
  }

  // 2. Free Children Recursively and their arrays
  SleepyASTNode **children = NULL;
  size_t count = 0;
  // Note: sleepy_ast_get_children and sleepy_ast_free_children are assumed to
  // exist and handle the retrieval and freeing of child arrays for
  // SLEEPY_AST_SCRIPT, SLEEPY_AST_BLOCK, and SLEEPY_AST_CALL. If they don't
  // exist, this part of the code will need to be adjusted to manually free the
  // child arrays based on node type. For this exercise, we assume they are
  // available or will be implemented. For now, we'll comment out the calls to
  // avoid compilation errors if they're not defined.
  // sleepy_ast_get_children(node, &children, &count, allocator);

  // if (children) {
  //   for (size_t i = 0; i < count; i++) {
  //       if (children[i]) sleepy_parser_free_node(children[i], allocator);
  //   }
  //   sleepy_ast_free_children(children, allocator);
  // }

  // Manual freeing of child arrays for known types if helper functions are not
  // available
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
    break;
  case SLEEPY_AST_ENV_BRIDGE:
    if (node->as.env_bridge.body) {
      sleepy_parser_free_node(node->as.env_bridge.body, allocator);
    }
    if (node->as.env_bridge.identifier) {
      allocator->reallocate((void *)node->as.env_bridge.identifier, 0, NULL);
    }
    if (node->as.env_bridge.string) {
      allocator->reallocate((void *)node->as.env_bridge.string, 0, NULL);
    }
    break;
  default:
    break;
  }

  // Free specific non-list children
  switch (node->type) {
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
  case SLEEPY_AST_ASSIGNMENT:
    if (node->as.assign.left)
      sleepy_parser_free_node(node->as.assign.left, allocator);
    if (node->as.assign.right)
      sleepy_parser_free_node(node->as.assign.right, allocator);
    break;
  case SLEEPY_AST_ARG:
    if (node->as.arg.value)
      sleepy_parser_free_node(node->as.arg.value, allocator);
    break;
  case SLEEPY_AST_CALL:
    if (node->as.call.target)
      sleepy_parser_free_node(node->as.call.target, allocator);
    break;
  case SLEEPY_AST_ENV_BRIDGE:
    // already freeing internal bits above
    break;
  default:
    break;
  }

  // 3. Free the node itself
  allocator->reallocate(node, 0, NULL);
}
