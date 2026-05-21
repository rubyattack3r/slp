#ifndef SLEEPY_BYTECODE_H
#define SLEEPY_BYTECODE_H

#include "sleepy_value.h"
#include "sleepy_chunk.h"

#define SLEEPY_BYTECODE_MAGIC   0x534C5950
#define SLEEPY_BYTECODE_VERSION 1

typedef struct SleepyBytecodeWriter {
    uint8_t *buffer;
    size_t capacity;
    size_t count;
    SleepyAllocator *allocator;
} SleepyBytecodeWriter;

typedef struct SleepyBytecodeReader {
    const uint8_t *data;
    size_t size;
    size_t offset;
} SleepyBytecodeReader;

void sleepy_bytecode_writer_init(SleepyBytecodeWriter *writer, SleepyAllocator *allocator);
void sleepy_bytecode_writer_free(SleepyBytecodeWriter *writer);
void sleepy_bytecode_writer_write(SleepyBytecodeWriter *writer, const uint8_t *data, size_t len);

SleepyBytecodeReader sleepy_bytecode_reader_init(const uint8_t *data, size_t size);
bool sleepy_bytecode_reader_read(SleepyBytecodeReader *reader, uint8_t *out, size_t len);

bool sleepy_bytecode_serialize_chunk(SleepyBytecodeWriter *writer, SleepyChunk *chunk);
SleepyChunk *sleepy_bytecode_deserialize_chunk(SleepyBytecodeReader *reader, SleepyAllocator *allocator);

bool sleepy_bytecode_serialize_function(SleepyBytecodeWriter *writer, SleepyObjFunction *fn);
SleepyObjFunction *sleepy_bytecode_deserialize_function(SleepyBytecodeReader *reader, SleepyAllocator *allocator);

#endif // SLEEPY_H
