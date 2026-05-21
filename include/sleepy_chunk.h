#ifndef SLEEPY_CHUNK_H
#define SLEEPY_CHUNK_H

#include "sleepy_value.h"

struct SleepyChunk {
    SleepyAllocator *allocator;
    uint8_t *code;
    int *lines;
    int count;
    int capacity;
    SleepyValue *constants;
    int constant_count;
    int constant_capacity;
};

void sleepy_chunk_init(SleepyChunk *chunk, SleepyAllocator *allocator);
void sleepy_chunk_free(SleepyChunk *chunk);
int sleepy_chunk_write(SleepyChunk *chunk, uint8_t byte, int line);
int sleepy_chunk_add_constant(SleepyChunk *chunk, SleepyValue value);
uint16_t sleepy_chunk_read_short(SleepyChunk *chunk, int offset);
void sleepy_chunk_write_short(SleepyChunk *chunk, uint16_t val, int line);

#endif // SLEEPY_H
