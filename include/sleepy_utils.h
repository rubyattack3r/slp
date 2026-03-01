#ifndef SLEEPY_UTILS_H
#define SLEEPY_UTILS_H

#include "sleepy_common.h"
#include "sleepy_core.h"

// Reusable dynamic array of bytes/strings (like Wren's StringBuffer)
typedef struct {
  char *data;
  size_t length;
  size_t capacity;
  SleepyAllocator *allocator;
} SleepyStringBuffer;

void sleepy_string_buffer_init(SleepyStringBuffer *buffer,
                               SleepyAllocator *allocator);
void sleepy_string_buffer_clear(SleepyStringBuffer *buffer);
void sleepy_string_buffer_free(SleepyStringBuffer *buffer);
void sleepy_string_buffer_append_char(SleepyStringBuffer *buffer, char c);
void sleepy_string_buffer_append_string(SleepyStringBuffer *buffer,
                                        const char *str, size_t length);

// Memory and string utilities to avoid libc dependencies
void *sleepy_utils_memcpy(void *dest, const void *src, size_t n);
size_t sleepy_utils_strlen(const char *s);
char *sleepy_utils_strcpy(char *dest, const char *src);

#endif // SLEEPY_UTILS_H
