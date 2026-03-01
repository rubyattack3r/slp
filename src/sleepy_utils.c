#include "sleepy_utils.h"

void sleepy_string_buffer_init(SleepyStringBuffer *buffer,
                               SleepyAllocator *allocator) {
  buffer->data = NULL;
  buffer->length = 0;
  buffer->capacity = 0;
  buffer->allocator = allocator;
}

void sleepy_string_buffer_clear(SleepyStringBuffer *buffer) {
  buffer->length = 0;
}

void sleepy_string_buffer_free(SleepyStringBuffer *buffer) {
  SLEEPY_FREE_ARRAY(buffer->allocator, char, buffer->data, buffer->capacity);
  sleepy_string_buffer_init(buffer, buffer->allocator);
}

void sleepy_string_buffer_append_char(SleepyStringBuffer *buffer, char c) {
  if (buffer->capacity < buffer->length + 1) {
    size_t new_capacity = buffer->capacity == 0 ? 8 : buffer->capacity * 2;
    buffer->data =
        (char *)SLEEPY_REALLOC(buffer->allocator, buffer->data, new_capacity);
    buffer->capacity = new_capacity;
  }

  buffer->data[buffer->length++] = c;
}

void sleepy_string_buffer_append_string(SleepyStringBuffer *buffer,
                                        const char *str, size_t length) {
  if (buffer->capacity < buffer->length + length) {
    size_t new_capacity = buffer->capacity == 0 ? 8 : buffer->capacity * 2;
    while (new_capacity < buffer->length + length) {
      new_capacity *= 2;
    }
    buffer->data =
        (char *)SLEEPY_REALLOC(buffer->allocator, buffer->data, new_capacity);
    buffer->capacity = new_capacity;
  }

  for (size_t i = 0; i < length; i++) {
    buffer->data[buffer->length + i] = str[i];
  }
  buffer->length += length;
}

void *sleepy_utils_memcpy(void *dest, const void *src, size_t n) {
  char *d = (char *)dest;
  const char *s = (const char *)src;
  while (n--) {
    *d++ = *s++;
  }
  return dest;
}

size_t sleepy_utils_strlen(const char *s) {
  size_t len = 0;
  while (s[len] != '\0') {
    len++;
  }
  return len;
}
