#ifndef SLP_CORE_H
#define SLP_CORE_H

#include "slp_common.h"

// A generic allocation function. It should act like `realloc`.
// If `ptr` is NULL and `new_size` > 0, it behaves like `malloc`.
// If `new_size` is 0, it behaves like `free` and should return NULL.
// Otherwise, it resizes the memory block to `new_size`.
typedef void *(*SlpReallocateFn)(void *ptr, size_t new_size,
                                    void *user_data);

typedef struct {
  SlpReallocateFn reallocate;
  void *user_data;
} SlpAllocator;

#endif // SLP_CORE_H
