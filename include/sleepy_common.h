#ifndef SLEEPY_COMMON_H
#define SLEEPY_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Includes for common types used across the VM
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ----------------------------------------------------------------------------
// COMMON MACROS
// ----------------------------------------------------------------------------

#define SLEEPY_MALLOC(allocator, size)                                         \
  ((allocator)->reallocate(NULL, (size), (allocator)->user_data))

#define SLEEPY_REALLOC(allocator, ptr, new_size)                               \
  ((allocator)->reallocate((ptr), (new_size), (allocator)->user_data))

#define SLEEPY_FREE(allocator, ptr)                                            \
  ((allocator)->reallocate((ptr), 0, (allocator)->user_data))

#define SLEEPY_ALLOCATE_ARRAY(allocator, type, count)                          \
  ((type *)SLEEPY_MALLOC(allocator, sizeof(type) * (count)))

#define SLEEPY_ALLOCATE(allocator, type)                                       \
  ((type *)SLEEPY_MALLOC(allocator, sizeof(type)))

#define SLEEPY_FREE_ARRAY(allocator, type, pointer, count)                     \
  SLEEPY_FREE(allocator, pointer)

#endif // SLEEPY_COMMON_H
