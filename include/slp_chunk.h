#ifndef SLP_CHUNK_H
#define SLP_CHUNK_H

#include "slp_value.h"

struct SlpChunk {
    SlpAllocator *allocator;
    uint8_t *code;
    int *lines;
    int count;
    int capacity;
    SlpValue *constants;
    int constant_count;
    int constant_capacity;
};

void slp_chunk_init(SlpChunk *chunk, SlpAllocator *allocator);
void slp_chunk_free(SlpChunk *chunk);
int slp_chunk_write(SlpChunk *chunk, uint8_t byte, int line);
int slp_chunk_add_constant(SlpChunk *chunk, SlpValue value);
uint16_t slp_chunk_read_short(SlpChunk *chunk, int offset);
void slp_chunk_write_short(SlpChunk *chunk, uint16_t val, int line);

#endif // SLP_H
