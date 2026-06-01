#ifndef SLP_BYTECODE_H
#define SLP_BYTECODE_H

#include "slp_value.h"
#include "slp_chunk.h"

#define SLP_BYTECODE_MAGIC   0x534C5950
#define SLP_BYTECODE_VERSION 1

typedef struct SlpBytecodeWriter {
    uint8_t *buffer;
    size_t capacity;
    size_t count;
    SlpAllocator *allocator;
} SlpBytecodeWriter;

typedef struct SlpBytecodeReader {
    const uint8_t *data;
    size_t size;
    size_t offset;
} SlpBytecodeReader;

void slp_bytecode_writer_init(SlpBytecodeWriter *writer, SlpAllocator *allocator);
void slp_bytecode_writer_free(SlpBytecodeWriter *writer);
void slp_bytecode_writer_write(SlpBytecodeWriter *writer, const uint8_t *data, size_t len);

SlpBytecodeReader slp_bytecode_reader_init(const uint8_t *data, size_t size);
bool slp_bytecode_reader_read(SlpBytecodeReader *reader, uint8_t *out, size_t len);

bool slp_bytecode_serialize_chunk(SlpBytecodeWriter *writer, SlpChunk *chunk);
SlpChunk *slp_bytecode_deserialize_chunk(SlpBytecodeReader *reader, SlpAllocator *allocator);

bool slp_bytecode_serialize_function(SlpBytecodeWriter *writer, SlpObjFunction *fn);
SlpObjFunction *slp_bytecode_deserialize_function(SlpBytecodeReader *reader, SlpAllocator *allocator);

#endif // SLP_BYTECODE_H
