#include "slp_bytecode.h"
#include "slp_common.h"
#include "slp_vm.h"
#include <string.h>

static void ensure_capacity(SlpBytecodeWriter *w, size_t needed) {
    if (w->count + needed <= w->capacity) return;
    size_t new_cap = w->capacity * 2;
    if (new_cap < w->capacity + needed) new_cap = w->capacity + needed;
    if (new_cap < 64) new_cap = 64;
    w->buffer = (uint8_t*)w->allocator->reallocate(w->buffer, new_cap, w->allocator->user_data);
    w->capacity = new_cap;
}

void slp_bytecode_writer_init(SlpBytecodeWriter *writer, SlpAllocator *allocator) {
    writer->buffer = NULL;
    writer->capacity = 0;
    writer->count = 0;
    writer->allocator = allocator;
}

void slp_bytecode_writer_free(SlpBytecodeWriter *writer) {
    if (writer->buffer)
        writer->allocator->reallocate(writer->buffer, 0, writer->allocator->user_data);
    writer->buffer = NULL;
    writer->capacity = 0;
    writer->count = 0;
}

void slp_bytecode_writer_write(SlpBytecodeWriter *writer, const uint8_t *data, size_t len) {
    ensure_capacity(writer, len);
    memcpy(writer->buffer + writer->count, data, len);
    writer->count += len;
}

SlpBytecodeReader slp_bytecode_reader_init(const uint8_t *data, size_t size) {
    SlpBytecodeReader r;
    r.data = data;
    r.size = size;
    r.offset = 0;
    return r;
}

bool slp_bytecode_reader_read(SlpBytecodeReader *reader, uint8_t *out, size_t len) {
    if (reader->offset + len > reader->size) return false;
    memcpy(out, reader->data + reader->offset, len);
    reader->offset += len;
    return true;
}

static void write_u8(SlpBytecodeWriter *w, uint8_t val) {
    slp_bytecode_writer_write(w, &val, 1);
}

static void __attribute__((unused)) write_u16(SlpBytecodeWriter *w, uint16_t val) {
    uint8_t buf[2];
    buf[0] = (uint8_t)((val >> 8) & 0xFF);
    buf[1] = (uint8_t)(val & 0xFF);
    slp_bytecode_writer_write(w, buf, 2);
}

static void write_u32(SlpBytecodeWriter *w, uint32_t val) {
    uint8_t buf[4];
    buf[0] = (uint8_t)((val >> 24) & 0xFF);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >> 8) & 0xFF);
    buf[3] = (uint8_t)(val & 0xFF);
    slp_bytecode_writer_write(w, buf, 4);
}

static void write_u64(SlpBytecodeWriter *w, uint64_t val) {
    uint8_t buf[8];
    for (int i = 0; i < 8; i++)
        buf[i] = (uint8_t)((val >> (56 - i * 8)) & 0xFF);
    slp_bytecode_writer_write(w, buf, 8);
}

static void write_f64(SlpBytecodeWriter *w, double val) {
    uint64_t bits;
    memcpy(&bits, &val, sizeof(bits));
    write_u64(w, bits);
}

static bool read_u8(SlpBytecodeReader *r, uint8_t *out) {
    return slp_bytecode_reader_read(r, out, 1);
}

static bool __attribute__((unused)) read_u16(SlpBytecodeReader *r, uint16_t *out) {
    uint8_t buf[2];
    if (!slp_bytecode_reader_read(r, buf, 2)) return false;
    *out = (uint16_t)((buf[0] << 8) | buf[1]);
    return true;
}

static bool read_u32(SlpBytecodeReader *r, uint32_t *out) {
    uint8_t buf[4];
    if (!slp_bytecode_reader_read(r, buf, 4)) return false;
    *out = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    return true;
}

static bool read_u64(SlpBytecodeReader *r, uint64_t *out) {
    uint8_t buf[8];
    if (!slp_bytecode_reader_read(r, buf, 8)) return false;
    *out = 0;
    for (int i = 0; i < 8; i++)
        *out = (*out << 8) | buf[i];
    return true;
}

static bool read_f64(SlpBytecodeReader *r, double *out) {
    uint64_t bits;
    if (!read_u64(r, &bits)) return false;
    memcpy(out, &bits, sizeof(bits));
    return true;
}

#define CONST_TAG_NULL    0
#define CONST_TAG_BOOL    1
#define CONST_TAG_NUMBER  2
#define CONST_TAG_STRING  3
#define CONST_TAG_LONG    4

static bool serialize_value(SlpBytecodeWriter *w, SlpValue val) {
    if (SLP_IS_NULL(val)) {
        write_u8(w, CONST_TAG_NULL);
    } else if (SLP_IS_BOOL(val)) {
        write_u8(w, CONST_TAG_BOOL);
        write_u8(w, SLP_AS_BOOL(val) ? 1 : 0);
    } else if (SLP_IS_NUM(val)) {
        write_u8(w, CONST_TAG_NUMBER);
        write_f64(w, SLP_AS_NUM(val));
    } else if (SLP_IS_OBJ(val)) {
        SlpObj *obj = SLP_AS_OBJ(val);
        if (obj->type == SLP_OBJ_STRING) {
            SlpObjString *s = (SlpObjString*)obj;
            write_u8(w, CONST_TAG_STRING);
            write_u32(w, s->length);
            slp_bytecode_writer_write(w, (const uint8_t*)s->chars, s->length);
        } else if (obj->type == SLP_OBJ_LONG) {
            SlpObjLong *l = (SlpObjLong*)obj;
            write_u8(w, CONST_TAG_LONG);
            write_u64(w, (uint64_t)l->value);
        } else {
            return false;
        }
    } else {
        return false;
    }
    return true;
}

static SlpValue deserialize_value(SlpBytecodeReader *r, SlpAllocator *allocator) {
    uint8_t tag;
    if (!read_u8(r, &tag)) return SLP_NULL_VAL;
    switch (tag) {
    case CONST_TAG_NULL:
        return SLP_NULL_VAL;
    case CONST_TAG_BOOL: {
        uint8_t b;
        if (!read_u8(r, &b)) return SLP_NULL_VAL;
        return SLP_BOOL_VAL(b != 0);
    }
    case CONST_TAG_NUMBER: {
        double d;
        if (!read_f64(r, &d)) return SLP_NULL_VAL;
        return SLP_NUM_VAL(d);
    }
    case CONST_TAG_STRING: {
        uint32_t len;
        if (!read_u32(r, &len)) return SLP_NULL_VAL;
        // Reject a length that cannot fit in the remaining input before
        // allocating. This also prevents the len+1 size from overflowing.
        if (len > r->size - r->offset) return SLP_NULL_VAL;
        char *buf = (char*)allocator->reallocate(NULL, len + 1, allocator->user_data);
        if (!buf) return SLP_NULL_VAL;
        if (!slp_bytecode_reader_read(r, (uint8_t*)buf, len)) {
            allocator->reallocate(buf, 0, allocator->user_data);
            return SLP_NULL_VAL;
        }
        buf[len] = '\0';
        SlpObjString *s = slp_obj_string_new(allocator, buf, len);
        allocator->reallocate(buf, 0, allocator->user_data);
        if (s) s->hash = slp_hash_string(s->chars, s->length);
        return s ? SLP_OBJ_VAL(s) : SLP_NULL_VAL;
    }
    case CONST_TAG_LONG: {
        uint64_t v;
        if (!read_u64(r, &v)) return SLP_NULL_VAL;
        SlpObjLong *l = slp_obj_long_new(allocator, (int64_t)v);
        return l ? SLP_OBJ_VAL(l) : SLP_NULL_VAL;
    }
    default:
        return SLP_NULL_VAL;
    }
}

bool slp_bytecode_serialize_chunk(SlpBytecodeWriter *writer, SlpChunk *chunk) {
    write_u32(writer, (uint32_t)chunk->count);
    slp_bytecode_writer_write(writer, chunk->code, chunk->count);
    write_u32(writer, (uint32_t)chunk->constant_count);
    for (int i = 0; i < chunk->constant_count; i++) {
        if (!serialize_value(writer, chunk->constants[i]))
            return false;
    }
    return true;
}

SlpChunk *slp_bytecode_deserialize_chunk(SlpBytecodeReader *reader, SlpAllocator *allocator) {
    uint32_t code_count;
    if (!read_u32(reader, &code_count)) return NULL;
    SlpChunk *chunk = SLP_ALLOCATE(allocator, SlpChunk);
    if (!chunk) return NULL;
    slp_chunk_init(chunk, allocator);
    for (uint32_t i = 0; i < code_count; i++) {
        uint8_t byte;
        if (!read_u8(reader, &byte)) { slp_chunk_free(chunk); return NULL; }
        slp_chunk_write(chunk, byte, 0);
    }
    uint32_t const_count;
    if (!read_u32(reader, &const_count)) { slp_chunk_free(chunk); return NULL; }
    for (uint32_t i = 0; i < const_count; i++) {
        SlpValue val = deserialize_value(reader, allocator);
        slp_chunk_add_constant(chunk, val);
    }
    return chunk;
}

bool slp_bytecode_serialize_function(SlpBytecodeWriter *writer, SlpObjFunction *fn) {
    write_u32(writer, SLP_BYTECODE_MAGIC);
    write_u32(writer, SLP_BYTECODE_VERSION);
    uint32_t name_len = fn->name ? fn->name->length : 0;
    write_u32(writer, name_len);
    if (name_len > 0 && fn->name)
        slp_bytecode_writer_write(writer, (const uint8_t*)fn->name->chars, name_len);
    write_u8(writer, (uint8_t)fn->arity);
    write_u8(writer, (uint8_t)fn->upvalue_count);
    return slp_bytecode_serialize_chunk(writer, fn->chunk);
}

static void free_deserialized_function(SlpObjFunction *fn, SlpAllocator *allocator) {
    if (fn) {
        if (fn->chunk) {
            slp_chunk_free(fn->chunk);
            SLP_FREE(allocator, fn->chunk);
        }
        SLP_FREE(allocator, fn);
    }
}

SlpObjFunction *slp_bytecode_deserialize_function(SlpBytecodeReader *reader, SlpAllocator *allocator) {
    uint32_t magic, version;
    if (!read_u32(reader, &magic) || magic != SLP_BYTECODE_MAGIC) return NULL;
    if (!read_u32(reader, &version) || version != SLP_BYTECODE_VERSION) return NULL;
    uint32_t name_len;
    if (!read_u32(reader, &name_len)) return NULL;
    SlpObjFunction *fn = slp_obj_function_new(allocator);
    if (!fn) return NULL;
    if (name_len > 0) {
        if (name_len > reader->size - reader->offset) {
            free_deserialized_function(fn, allocator);
            return NULL;
        }
        char *name_buf = (char*)allocator->reallocate(NULL, name_len + 1, allocator->user_data);
        if (!name_buf) {
            free_deserialized_function(fn, allocator);
            return NULL;
        }
        if (!slp_bytecode_reader_read(reader, (uint8_t*)name_buf, name_len)) {
            allocator->reallocate(name_buf, 0, allocator->user_data);
            free_deserialized_function(fn, allocator);
            return NULL;
        }
        name_buf[name_len] = '\0';
        fn->name = slp_obj_string_new(allocator, name_buf, name_len);
        if (fn->name) fn->name->hash = slp_hash_string(fn->name->chars, fn->name->length);
        allocator->reallocate(name_buf, 0, allocator->user_data);
    }
    uint8_t arity, upvalue_count;
    if (!read_u8(reader, &arity) || !read_u8(reader, &upvalue_count)) {
        free_deserialized_function(fn, allocator);
        return NULL;
    }
    fn->arity = arity;
    fn->upvalue_count = upvalue_count;
    slp_chunk_free(fn->chunk);
    allocator->reallocate(fn->chunk, 0, allocator->user_data);
    fn->chunk = slp_bytecode_deserialize_chunk(reader, allocator);
    if (!fn->chunk) {
        free_deserialized_function(fn, allocator);
        return NULL;
    }
    fn->chunk->allocator = allocator;
    return fn;
}
