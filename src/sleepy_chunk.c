#include "sleepy_chunk.h"
#include "sleepy_utils.h"

void sleepy_chunk_init(SleepyChunk *chunk, SleepyAllocator *allocator) {
    chunk->allocator = allocator;
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->constants = NULL;
    chunk->constant_count = 0;
    chunk->constant_capacity = 0;
}

void sleepy_chunk_free(SleepyChunk *chunk) {
    if (chunk->code) SLEEPY_FREE(chunk->allocator, chunk->code);
    if (chunk->lines) SLEEPY_FREE(chunk->allocator, chunk->lines);
    if (chunk->constants) SLEEPY_FREE(chunk->allocator, chunk->constants);
    sleepy_chunk_init(chunk, chunk->allocator);
}

int sleepy_chunk_write(SleepyChunk *chunk, uint8_t byte, int line) {
    if (chunk->count + 1 > chunk->capacity) {
        int new_cap = chunk->capacity == 0 ? 64 : chunk->capacity * 2;
        chunk->code = (uint8_t*)SLEEPY_REALLOC(chunk->allocator, chunk->code, new_cap);
        chunk->lines = (int*)SLEEPY_REALLOC(chunk->allocator, chunk->lines, sizeof(int) * new_cap);
        chunk->capacity = new_cap;
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    return chunk->count++;
}

int sleepy_chunk_add_constant(SleepyChunk *chunk, SleepyValue value) {
    for (int i = 0; i < chunk->constant_count; i++) {
        if (sleepy_value_equals(chunk->constants[i], value))
            return i;
    }
    if (chunk->constant_count + 1 > chunk->constant_capacity) {
        int new_cap = chunk->constant_capacity == 0 ? 16 : chunk->constant_capacity * 2;
        chunk->constants = (SleepyValue*)SLEEPY_REALLOC(chunk->allocator, chunk->constants, sizeof(SleepyValue) * new_cap);
        chunk->constant_capacity = new_cap;
    }
    chunk->constants[chunk->constant_count] = value;
    return chunk->constant_count++;
}

uint16_t sleepy_chunk_read_short(SleepyChunk *chunk, int offset) {
    return (uint16_t)((chunk->code[offset] << 8) | chunk->code[offset + 1]);
}

void sleepy_chunk_write_short(SleepyChunk *chunk, uint16_t val, int line) {
    sleepy_chunk_write(chunk, (uint8_t)((val >> 8) & 0xFF), line);
    sleepy_chunk_write(chunk, (uint8_t)(val & 0xFF), line);
}
