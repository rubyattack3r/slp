#ifndef SLP_UTILS_H
#define SLP_UTILS_H

#include "slp_common.h"
#include "slp_core.h"

// Reusable dynamic array of bytes/strings (like Wren's StringBuffer)
typedef struct {
  char *data;
  size_t length;
  size_t capacity;
  SlpAllocator *allocator;
} SlpStringBuffer;

void slp_string_buffer_init(SlpStringBuffer *buffer,
                               SlpAllocator *allocator);
void slp_string_buffer_clear(SlpStringBuffer *buffer);
void slp_string_buffer_free(SlpStringBuffer *buffer);
void slp_string_buffer_append_char(SlpStringBuffer *buffer, char c);
void slp_string_buffer_append_string(SlpStringBuffer *buffer,
                                        const char *str, size_t length);

// Memory and string utilities to avoid libc dependencies
void *slp_utils_memcpy(void *dest, const void *src, size_t n);
size_t slp_utils_strlen(const char *s);
char *slp_utils_strcpy(char *dest, const char *src);

#endif // SLP_UTILS_H
