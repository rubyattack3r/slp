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
  // We need space for the char PLUS the null terminator
  if (buffer->capacity < buffer->length + 2) {
    size_t new_capacity = buffer->capacity == 0 ? 16 : buffer->capacity * 2;
    while (new_capacity < buffer->length + 2) {
      new_capacity *= 2;
    }
    buffer->data =
        (char *)SLEEPY_REALLOC(buffer->allocator, buffer->data, new_capacity);
    buffer->capacity = new_capacity;
  }

  buffer->data[buffer->length++] = c;
  buffer->data[buffer->length] = '\0';
}

void sleepy_string_buffer_append_string(SleepyStringBuffer *buffer,
                                        const char *str, size_t length) {
  if (length == 0)
    return;
  // We need space for the string PLUS the null terminator
  if (buffer->capacity < buffer->length + length + 1) {
    size_t new_capacity = buffer->capacity == 0 ? 16 : buffer->capacity * 2;
    while (new_capacity < buffer->length + length + 1) {
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
  buffer->data[buffer->length] = '\0';
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

char *sleepy_utils_strcpy(char *dest, const char *src) {
  char *d = dest;
  while ((*d++ = *src++) != '\0') {
    // nothing
  }
  return dest;
}
