#ifndef SLEEPY_AST_H
#define SLEEPY_AST_H

#include "sleepy_parser.h"

// Formats a Sleepy AST Node back into Sleepy source code natively.
// Returns a dynamically allocated string containing the code.
// The caller is responsible for freeing the resulting string using
// allocator->reallocate(ptr, 0).
char *sleepy_ast_format(SleepyASTNode *node, SleepyAllocator *allocator);

#endif // SLEEPY_AST_H
