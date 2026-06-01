#ifndef SLP_COMMON_H
#define SLP_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ----------------------------------------------------------------------------
// COMMON TYPES
// ----------------------------------------------------------------------------

// Length-prefixed string slice. Borrowed (does not own its bytes), so it is
// cheap to pass by value. Used in places where the lexer/parser need to refer
// to a substring of source text without copying.
typedef struct {
  const char *chars;
  uint32_t length;
} SlpStringView;

// Construct a SlpStringView from a C string literal. The literal's trailing
// NUL is excluded from the length.
#define SLP_SV(s) ((SlpStringView){(s), (uint32_t)(sizeof(s) - 1)})

// ----------------------------------------------------------------------------
// COMMON MACROS
// ----------------------------------------------------------------------------

// Assertion macro. With sanitizers (UBSan) enabled, a failed condition becomes
// an unreachable hint that traps under -fsanitize=undefined; without them, the
// optimizer prunes the impossible branch. Pairs with `make debug`.
#if defined(__GNUC__) || defined(__clang__)
#define SLP_ASSERT(c) do { if (!(c)) __builtin_unreachable(); } while (0)
#else
#define SLP_ASSERT(c) ((void)0)
#endif

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
