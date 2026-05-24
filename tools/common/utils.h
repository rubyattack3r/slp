#ifndef TOOLS_UTILS_H
#define TOOLS_UTILS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read the entire file into a heap-allocated buffer. 
 * Null-terminates the string and sets *out_size to the length if not NULL.
 * Returns NULL on error. The caller must free() the returned buffer.
 */
char* utils_read_file(const char* path, size_t* out_size);

/* Extracts the directory component of a path, handling both '/' and '\'.
 * Returns a heap-allocated string that must be freed with free().
 * If no directory is found, returns a heap-allocated ".".
 */
char* utils_get_directory(const char* path);

#ifdef __cplusplus
}
#endif

#endif /* TOOLS_UTILS_H */