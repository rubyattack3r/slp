#ifndef SLEEPY_CORE_H
#define SLEEPY_CORE_H

#include "sleepy_common.h"

// A generic allocation function. It should act like `realloc`.
// If `ptr` is NULL and `new_size` > 0, it behaves like `malloc`.
// If `new_size` is 0, it behaves like `free` and should return NULL.
// Otherwise, it resizes the memory block to `new_size`.
typedef void *(*SleepyReallocateFn)(void *ptr, size_t new_size,
                                    void *user_data);

typedef struct {
  SleepyReallocateFn reallocate;
  void *user_data;
} SleepyAllocator;

#endif // SLEEPY_CORE_H
