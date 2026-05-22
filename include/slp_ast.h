#ifndef SLP_AST_H
#define SLP_AST_H

#include "slp_parser.h"

// Formats a Slp AST Node back into Slp source code natively.
// Returns a dynamically allocated string containing the code.
// The caller is responsible for freeing the resulting string using
// allocator->reallocate(ptr, 0).
char *slp_ast_format(SlpASTNode *node, SlpAllocator *allocator);

// AST Helper Functions for Bindings
void slp_ast_get_children(SlpASTNode *node, SlpASTNode ***out_children,
                             size_t *out_count, SlpAllocator *allocator);
void slp_ast_free_children(SlpASTNode **children,
                              SlpAllocator *allocator);
const char *slp_ast_get_string(SlpASTNode *node);
const char *slp_ast_get_op(SlpASTNode *node);
size_t slp_ast_get_op_length(SlpASTNode *node);
long slp_ast_get_long(SlpASTNode *node);
double slp_ast_get_double(SlpASTNode *node);
bool slp_ast_get_bool(SlpASTNode *node);

// Bridge specific getters
const char *slp_ast_get_env_bridge_keyword(SlpASTNode *node);
const char *slp_ast_get_env_bridge_id(SlpASTNode *node);
const char *slp_ast_get_env_bridge_string(SlpASTNode *node);
const char *slp_ast_get_foreach_index(SlpASTNode *node);
const char *slp_ast_get_foreach_value(SlpASTNode *node);
const char *slp_ast_get_import_path(SlpASTNode *node);
const char *slp_ast_get_try_catch_var(SlpASTNode *node);
size_t slp_ast_get_for_init_count(SlpASTNode *node);
size_t slp_ast_get_for_inc_count(SlpASTNode *node);

// AST Builder APIs for FFI
SlpASTNode *slp_ast_build_node(SlpASTNodeType type, int line,
                                     SlpAllocator *allocator);
void slp_ast_set_string_val(SlpASTNode *node, const char *str,
                               SlpAllocator *allocator);
void slp_ast_set_long_val(SlpASTNode *node, long val);
void slp_ast_set_double_val(SlpASTNode *node, double val);
void slp_ast_set_bool_val(SlpASTNode *node, bool val);

// List/Array Setters
SlpASTNode **slp_ast_allocate_children(size_t count,
                                             SlpAllocator *allocator);
void slp_ast_set_children(SlpASTNode *node, SlpASTNode **children,
                             size_t count, SlpAllocator *allocator);

// Complex Setters
void slp_ast_set_binop(SlpASTNode *node, SlpASTNode *left,
                          SlpASTNode *right);
void slp_ast_set_binop_with_op(SlpASTNode *node, SlpASTNode *left,
                                  SlpASTNode *right, const char *op,
                                  SlpAllocator *allocator);
void slp_ast_set_unaryop_with_op(SlpASTNode *node, SlpASTNode *operand,
                                    const char *op, SlpAllocator *allocator);
void slp_ast_set_if(SlpASTNode *node, SlpASTNode *condition,
                       SlpASTNode *then_branch, SlpASTNode *else_branch);
void slp_ast_set_while(SlpASTNode *node, SlpASTNode *condition,
                          SlpASTNode *body);
void slp_ast_set_arg(SlpASTNode *node, SlpASTNode *value);
void slp_ast_set_arg_with_name(SlpASTNode *node, SlpASTNode *name, SlpASTNode *value);
void slp_ast_set_assignment(SlpASTNode *node, SlpASTNode *target,
                               SlpASTNode *value);
void slp_ast_set_obj_expr(SlpASTNode *node, SlpASTNode *target,
                             SlpASTNode *message, SlpASTNode **args,
                             size_t arg_count, SlpAllocator *allocator);
void slp_ast_set_env_bridge(SlpASTNode *node, const char *id,
                               const char *str, SlpASTNode **children,
                               size_t count, SlpAllocator *allocator);
void slp_ast_set_env_bridge_keyword(SlpASTNode *node, const char *keyword,
                                       SlpAllocator *allocator);
void slp_ast_set_env_bridge_id(SlpASTNode *node, const char *id,
                                  SlpAllocator *allocator);
void slp_ast_set_env_bridge_string(SlpASTNode *node, const char *str,
                                      SlpAllocator *allocator);
void slp_ast_set_env_bridge_body(SlpASTNode *node, SlpASTNode *body);
void slp_ast_set_call_target(SlpASTNode *node, const char *target,
                                SlpAllocator *allocator);
void slp_ast_set_foreach(SlpASTNode *node, const char *index,
                            const char *value, SlpASTNode *generator,
                            SlpASTNode *body, SlpAllocator *allocator);
void slp_ast_set_for(SlpASTNode *node, SlpASTNode **init,
                        size_t init_cnt, SlpASTNode *cond,
                        SlpASTNode **inc, size_t inc_cnt,
                        SlpASTNode *body, SlpAllocator *allocator);
void slp_ast_set_return(SlpASTNode *node, SlpASTNode *value);
void slp_ast_set_break(SlpASTNode *node);
void slp_ast_set_continue(SlpASTNode *node);
void slp_ast_set_yield(SlpASTNode *node, SlpASTNode *value);
void slp_ast_set_throw(SlpASTNode *node, SlpASTNode *value);
void slp_ast_set_assert(SlpASTNode *node, SlpASTNode *value);
void slp_ast_set_try_catch(SlpASTNode *node, SlpASTNode *body,
                              const char *var, SlpASTNode *handler,
                              SlpAllocator *allocator);
void slp_ast_set_import(SlpASTNode *node, const char *path,
                           SlpAllocator *allocator);
void slp_ast_set_backtick(SlpASTNode *node, const char *cmd,
                             SlpAllocator *allocator);

// Memory Management
void slp_parser_free_node(SlpASTNode *node, SlpAllocator *allocator);

void slp_ast_set_index(SlpASTNode *node, SlpASTNode *container, SlpASTNode *element);
void slp_ast_set_kv_pair(SlpASTNode *node, SlpASTNode *name, SlpASTNode *value);

#endif // SLP_AST_H
