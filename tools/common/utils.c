#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* utils_read_file(const char* path, size_t* out_size) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }

    fseek(file, 0L, SEEK_END);
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);

    char* buffer = (char*)malloc(size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, size, file);
    buffer[read_bytes] = '\0';
    fclose(file);

    if (out_size) {
        *out_size = read_bytes;
    }
    return buffer;
}

char* utils_get_directory(const char* path) {
    const char* last_slash = strrchr(path, '/');
    const char* last_backslash = strrchr(path, '\\');
    if (last_backslash > last_slash) {
        last_slash = last_backslash;
    }

    if (!last_slash) {
        char* dir = (char*)malloc(2);
        if (dir) {
            dir[0] = '.';
            dir[1] = '\0';
        }
        return dir;
    }
    
    size_t len = last_slash - path;
    if (len == 0) {
        char* dir = (char*)malloc(2);
        if (dir) {
            dir[0] = *last_slash; // Keeps the '/' or '\'
            dir[1] = '\0';
        }
        return dir;
    }
    
    char* dir = (char*)malloc(len + 1);
    if (dir) {
        strncpy(dir, path, len);
        dir[len] = '\0';
    }
    return dir;
}