#ifndef SLP_COMMON_H
#define SLP_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Includes for common types used across the VM
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ----------------------------------------------------------------------------
// COMMON MACROS
// ----------------------------------------------------------------------------

#define SLP_MALLOC(allocator, size)                                         \
  ((allocator)->reallocate(NULL, (size), (allocator)->user_data))

#define SLP_REALLOC(allocator, ptr, new_size)                               \
  ((allocator)->reallocate((ptr), (new_size), (allocator)->user_data))

#define SLP_FREE(allocator, ptr)                                            \
  ((allocator)->reallocate((ptr), 0, (allocator)->user_data))

#define SLP_ALLOCATE_ARRAY(allocator, type, count)                          \
  ((type *)SLP_MALLOC(allocator, sizeof(type) * (count)))

#define SLP_ALLOCATE(allocator, type)                                       \
  ((type *)SLP_MALLOC(allocator, sizeof(type)))

#define SLP_FREE_ARRAY(allocator, type, pointer, count)                     \
  SLP_FREE(allocator, pointer)

#endif // SLP_COMMON_H
