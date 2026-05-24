#ifndef BOF_H
#define BOF_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load and execute a Beacon Object File (BOF).
 *
 * bof_data:   Pointer to the raw COFF file data.
 * bof_size:   Size of the COFF file.
 * entrypoint: Name of the function to execute (typically "go").
 * args:       Packed arguments from bof_pack (or NULL).
 * args_size:  Size of the packed arguments.
 */
int bof_run(const uint8_t *bof_data, size_t bof_size, const char *entrypoint, const uint8_t *args, size_t args_size);

#ifdef __cplusplus
}
#endif

#endif /* BOF_H */
