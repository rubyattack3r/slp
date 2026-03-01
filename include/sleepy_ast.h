#ifndef SLEEPY_AST_H
#define SLEEPY_AST_H

#include "sleepy_parser.h"

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
long sleepy_ast_get_long(SleepyASTNode *node);
double sleepy_ast_get_double(SleepyASTNode *node);
bool sleepy_ast_get_bool(SleepyASTNode *node);

// Bridge specific getters
const char *sleepy_ast_get_env_bridge_id(SleepyASTNode *node);
const char *sleepy_ast_get_env_bridge_string(SleepyASTNode *node);

// AST Builder APIs for FFI
SleepyASTNode *sleepy_ast_build_node(SleepyASTNodeType type, int line,
                                     SleepyAllocator *allocator);
void sleepy_ast_set_string_val(SleepyASTNode *node, const char *str,
                               SleepyAllocator *allocator);
void sleepy_ast_set_long_val(SleepyASTNode *node, long val);
void sleepy_ast_set_double_val(SleepyASTNode *node, double val);
void sleepy_ast_set_bool_val(SleepyASTNode *node, bool val);

// List/Array Setters
void sleepy_ast_set_children(SleepyASTNode *node, SleepyASTNode **children,
                             size_t count, SleepyAllocator *allocator);

// Complex Setters
void sleepy_ast_set_binop(SleepyASTNode *node, SleepyASTNode *left,
                          SleepyASTNode *right);
void sleepy_ast_set_if(SleepyASTNode *node, SleepyASTNode *condition,
                       SleepyASTNode *then_branch, SleepyASTNode *else_branch);
void sleepy_ast_set_while(SleepyASTNode *node, SleepyASTNode *condition,
                          SleepyASTNode *body);
void sleepy_ast_set_arg(SleepyASTNode *node, SleepyASTNode *value);
void sleepy_ast_set_assignment(SleepyASTNode *node, SleepyASTNode *target,
                               SleepyASTNode *value);
void sleepy_ast_set_env_bridge(SleepyASTNode *node, const char *id,
                               const char *str, SleepyASTNode **children,
                               size_t count, SleepyAllocator *allocator);
void sleepy_ast_set_call_target(SleepyASTNode *node, const char *target,
                                SleepyAllocator *allocator);

// Memory Management
void sleepy_parser_free_node(SleepyASTNode *node, SleepyAllocator *allocator);

#endif // SLEEPY_AST_H
