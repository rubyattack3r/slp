#include "slp_chunk.h"
#include "slp_utils.h"

/*
 * Runtime numeric equality is deliberately type-coercing, but the constant
 * pool must preserve Sleep's distinct Int, Long, and Double representations.
 * Otherwise, literals such as 7, 7L, and 7.0 can all resolve to whichever
 * typed constant was emitted first.
 */
static bool constants_equal(SlpValue a, SlpValue b) {
    if (SLP_IS_NUM(a) || SLP_IS_NUM(b)) {
        return SLP_IS_NUM(a) && SLP_IS_NUM(b) &&
               SLP_AS_NUM(a) == SLP_AS_NUM(b);
    }

    if (SLP_IS_OBJ(a) || SLP_IS_OBJ(b)) {
        if (!SLP_IS_OBJ(a) || !SLP_IS_OBJ(b) ||
            SLP_OBJ_TYPE(a) != SLP_OBJ_TYPE(b)) {
            return false;
        }

        switch (SLP_OBJ_TYPE(a)) {
            case SLP_OBJ_LONG:
                return SLP_AS_LONG(a)->value == SLP_AS_LONG(b)->value;
            case SLP_OBJ_DOUBLE:
                return SLP_AS_DOUBLE(a)->value == SLP_AS_DOUBLE(b)->value;
            default:
                return SLP_AS_OBJ(a) == SLP_AS_OBJ(b);
        }
    }

    return slp_value_equals(a, b);
}

void slp_chunk_init(SlpChunk *chunk, SlpAllocator *allocator) {
    chunk->allocator = allocator;
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->constants = NULL;
    chunk->constant_count = 0;
    chunk->constant_capacity = 0;
}

void slp_chunk_free(SlpChunk *chunk) {
    if (chunk->code) SLP_FREE(chunk->allocator, chunk->code);
    if (chunk->lines) SLP_FREE(chunk->allocator, chunk->lines);
    if (chunk->constants) SLP_FREE(chunk->allocator, chunk->constants);
    slp_chunk_init(chunk, chunk->allocator);
}

int slp_chunk_write(SlpChunk *chunk, uint8_t byte, int line) {
    if (chunk->count + 1 > chunk->capacity) {
        int new_cap = chunk->capacity == 0 ? 64 : chunk->capacity * 2;
        chunk->code = (uint8_t*)SLP_REALLOC(chunk->allocator, chunk->code, new_cap);
        chunk->lines = (int*)SLP_REALLOC(chunk->allocator, chunk->lines, sizeof(int) * new_cap);
        chunk->capacity = new_cap;
    }
    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    return chunk->count++;
}

int slp_chunk_add_constant(SlpChunk *chunk, SlpValue value) {
    for (int i = 0; i < chunk->constant_count; i++) {
        if (constants_equal(chunk->constants[i], value))
            return i;
    }
    if (chunk->constant_count + 1 > chunk->constant_capacity) {
        int new_cap = chunk->constant_capacity == 0 ? 16 : chunk->constant_capacity * 2;
        chunk->constants = (SlpValue*)SLP_REALLOC(chunk->allocator, chunk->constants, sizeof(SlpValue) * new_cap);
        chunk->constant_capacity = new_cap;
    }
    chunk->constants[chunk->constant_count] = value;
    return chunk->constant_count++;
}

uint16_t slp_chunk_read_short(SlpChunk *chunk, int offset) {
    return (uint16_t)((chunk->code[offset] << 8) | chunk->code[offset + 1]);
}

void slp_chunk_write_short(SlpChunk *chunk, uint16_t val, int line) {
    slp_chunk_write(chunk, (uint8_t)((val >> 8) & 0xFF), line);
    slp_chunk_write(chunk, (uint8_t)(val & 0xFF), line);
}
