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
                             size_t *out_count);
const char *sleepy_ast_get_string(SleepyASTNode *node);
long sleepy_ast_get_long(SleepyASTNode *node);
double sleepy_ast_get_double(SleepyASTNode *node);
bool sleepy_ast_get_bool(SleepyASTNode *node);

#endif // SLEEPY_AST_H
