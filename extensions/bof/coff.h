#ifndef COFF_H
#define COFF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* (*coff_symbol_resolver_t)(const char *sym_name, void *userdata);

typedef struct coff_module {
    uint8_t *allocated_memory;
    size_t allocated_size;
    void *entrypoint;
} coff_module_t;

int coff_load(coff_module_t *module, 
              const uint8_t *data, size_t data_len, 
              const char *entrypoint_name,
              coff_symbol_resolver_t resolver,
              void *userdata);

void coff_execute(coff_module_t *module, const uint8_t *args, size_t args_size);

void coff_free(coff_module_t *module);

#ifdef __cplusplus
}
#endif

#endif
