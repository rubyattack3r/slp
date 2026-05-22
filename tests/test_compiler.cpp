#include "doctest.h"
#include <string.h>
#include <stdlib.h>

extern "C" {
#include "slp_common.h"
#include "slp_core.h"
#include "slp_value.h"
#include "slp_vm.h"
#include "slp_compiler.h"
#include "slp_parser.h"
#include "slp_ast.h"
#include "slp_opcodes.h"
#include "slp_bytecode.h"

static void *tc_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static SlpAllocator tc_allocator = {tc_alloc, NULL};
}

static SlpObjFunction *compile_source(const char *source, SlpVM *vm) {
    SlpParser parser;
    slp_parser_init(&parser, source, &tc_allocator);
    SlpASTNode *ast = slp_parser_parse(&parser);
    if (!ast) return nullptr;
    SlpObjFunction *fn = slp_compile(vm, ast, &tc_allocator);
    slp_parser_free_node(ast, &tc_allocator);
    return fn;
}

TEST_CASE("Compiler: null literal") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("$null;", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    CHECK(c->count >= 3);
    CHECK(c->code[0] == OP_PUSH_NULL);
    CHECK(c->code[c->count - 1] == OP_RETURN);
    slp_vm_free(vm);
}

TEST_CASE("Compiler: boolean literals") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("true; false;", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_TRUE);
    CHECK(c->code[1] == OP_POP);
    CHECK(c->code[2] == OP_PUSH_FALSE);
    slp_vm_free(vm);
}

TEST_CASE("Compiler: number literal") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("42;", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_CONST);
    CHECK(c->constant_count == 1);
    CHECK(SLP_IS_NUM(c->constants[0]));
    CHECK(SLP_AS_NUM(c->constants[0]) == doctest::Approx(42));
    slp_vm_free(vm);
}

TEST_CASE("Compiler: string literal") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("\"hello\";", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_CONST);
    CHECK(c->constant_count >= 1);
    CHECK(SLP_IS_OBJ(c->constants[0]));
    CHECK(SLP_OBJ_TYPE(c->constants[0]) == SLP_OBJ_STRING);
    SlpObjString *s = SLP_AS_STRING(c->constants[0]);
    CHECK(s->length == 5);
    CHECK(strcmp(s->chars, "hello") == 0);
    slp_vm_free(vm);
}

TEST_CASE("Compiler: addition") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("1 + 2;", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_CONST);
    CHECK(c->code[3] == OP_PUSH_CONST);
    CHECK(c->code[6] == OP_ADD);
    slp_vm_free(vm);
}

TEST_CASE("Compiler: variable assignment and access") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("$x = 10; $x;", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_CONST);
    CHECK(c->code[3] == OP_STORE_GLOBAL);
    SlpObjString *name = SLP_AS_STRING(c->constants[slp_chunk_read_short(c, 4)]);
    CHECK(strcmp(name->chars, "x") == 0);
    slp_vm_free(vm);
}

TEST_CASE("Compiler: if statement") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("if (true) { 1; }", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_TRUE);
    CHECK(c->code[1] == OP_JUMP_IF_FALSE);
    slp_vm_free(vm);
}

TEST_CASE("Compiler: if-else statement") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("if (true) { 1; } else { 2; }", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_TRUE);
    CHECK(c->code[1] == OP_JUMP_IF_FALSE);
    slp_vm_free(vm);
}

TEST_CASE("Compiler: while loop") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("while (true) { break; }", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_TRUE);
    CHECK(c->code[1] == OP_JUMP_IF_FALSE);
    slp_vm_free(vm);
}

TEST_CASE("Compiler: function call") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("foo(1, 2);", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    bool found_call = false;
    for (int i = 0; i < c->count; i++) {
        if (c->code[i] == OP_CALL) { found_call = true; break; }
    }
    CHECK(found_call);
    slp_vm_free(vm);
}

TEST_CASE("Compiler: negation") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("-42;", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_CONST);
    CHECK(c->code[3] == OP_NEGATE);
    slp_vm_free(vm);
}

TEST_CASE("Compiler: not operator") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("!true;", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_TRUE);
    CHECK(c->code[1] == OP_NOT);
    slp_vm_free(vm);
}

TEST_CASE("Compiler: comparison operators") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    const char *ops[] = {"1 < 2;", "1 > 2;", "1 <= 2;", "1 >= 2;", "1 == 2;", "1 != 2;"};
    SlpOpcode expected[] = {OP_LESS, OP_GREATER, OP_LESS_EQUAL, OP_GREATER_EQUAL, OP_EQUAL, OP_NOT_EQUAL};
    for (int i = 0; i < 6; i++) {
        SlpObjFunction *fn = compile_source(ops[i], vm);
        REQUIRE(fn != nullptr);
        bool found = false;
        for (int j = 0; j < fn->chunk->count; j++) {
            if (fn->chunk->code[j] == expected[i]) { found = true; break; }
        }
        CHECK(found);
    }
    slp_vm_free(vm);
}

TEST_CASE("Compiler: arithmetic operators") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    const char *ops[] = {"1 + 2;", "1 - 2;", "1 * 2;", "1 / 2;", "1 % 2;"};
    SlpOpcode expected[] = {OP_ADD, OP_SUBTRACT, OP_MULTIPLY, OP_DIVIDE, OP_MODULO};
    for (int i = 0; i < 5; i++) {
        SlpObjFunction *fn = compile_source(ops[i], vm);
        REQUIRE(fn != nullptr);
        bool found = false;
        for (int j = 0; j < fn->chunk->count; j++) {
            if (fn->chunk->code[j] == expected[i]) { found = true; break; }
        }
        CHECK(found);
    }
    slp_vm_free(vm);
}

TEST_CASE("Compiler: return statement") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("return 42;", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    bool found_return = false;
    for (int i = 0; i < c->count; i++) {
        if (c->code[i] == OP_RETURN) { found_return = true; break; }
    }
    CHECK(found_return);
    slp_vm_free(vm);
}

TEST_CASE("Compiler: empty source") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source(" ", vm);
    if (fn) {
        SlpChunk *c = fn->chunk;
        CHECK(c->count >= 2);
        CHECK(c->code[c->count - 1] == OP_RETURN);
    }
    slp_vm_free(vm);
}

TEST_CASE("Compiler: multiple statements") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("1; 2; 3;", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    int push_count = 0;
    for (int i = 0; i < c->count; i++) {
        if (c->code[i] == OP_PUSH_CONST) push_count++;
    }
    CHECK(push_count == 3);
    slp_vm_free(vm);
}

TEST_CASE("Compiler: try/catch") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("try { 1; } catch $e { 2; }", vm);
    REQUIRE(fn != nullptr);
    SlpChunk *c = fn->chunk;
    bool found_handler = false, found_pop_handler = false;
    for (int i = 0; i < c->count; i++) {
        if (c->code[i] == OP_PUSH_HANDLER) found_handler = true;
        if (c->code[i] == OP_POP_HANDLER) found_pop_handler = true;
    }
    CHECK(found_handler);
    CHECK(found_pop_handler);
    slp_vm_free(vm);
}

TEST_CASE("Bytecode: serialize/deserialize chunk") {
    SlpAllocator *alloc = &tc_allocator;
    SlpChunk chunk;
    slp_chunk_init(&chunk, alloc);
    slp_chunk_write(&chunk, OP_PUSH_CONST, 1);
    slp_chunk_write_short(&chunk, 0, 1);
    slp_chunk_write(&chunk, OP_PUSH_CONST, 1);
    slp_chunk_write_short(&chunk, 1, 1);
    slp_chunk_write(&chunk, OP_ADD, 1);
    slp_chunk_write(&chunk, OP_RETURN, 1);
    slp_chunk_add_constant(&chunk, SLP_NUM_VAL(1.0));
    slp_chunk_add_constant(&chunk, SLP_NUM_VAL(2.0));

    SlpBytecodeWriter writer;
    slp_bytecode_writer_init(&writer, alloc);
    bool ok = slp_bytecode_serialize_chunk(&writer, &chunk);
    CHECK(ok);
    CHECK(writer.count > 0);

    SlpBytecodeReader reader = slp_bytecode_reader_init(writer.buffer, writer.count);
    SlpChunk *chunk2 = slp_bytecode_deserialize_chunk(&reader, alloc);
    REQUIRE(chunk2 != nullptr);
    CHECK(chunk2->count == chunk.count);
    for (int i = 0; i < chunk.count; i++)
        CHECK(chunk2->code[i] == chunk.code[i]);
    CHECK(chunk2->constant_count == 2);
    CHECK(SLP_AS_NUM(chunk2->constants[0]) == doctest::Approx(1.0));
    CHECK(SLP_AS_NUM(chunk2->constants[1]) == doctest::Approx(2.0));

    slp_chunk_free(chunk2);
    slp_bytecode_writer_free(&writer);
    slp_chunk_free(&chunk);
}

TEST_CASE("Bytecode: serialize/deserialize function") {
    SlpVM *vm = slp_vm_new(&tc_allocator);
    SlpObjFunction *fn = compile_source("1 + 2;", vm);
    REQUIRE(fn != nullptr);

    SlpBytecodeWriter writer;
    slp_bytecode_writer_init(&writer, &tc_allocator);
    bool ok = slp_bytecode_serialize_function(&writer, fn);
    CHECK(ok);
    CHECK(writer.count > 0);

    SlpBytecodeReader reader = slp_bytecode_reader_init(writer.buffer, writer.count);
    SlpObjFunction *fn2 = slp_bytecode_deserialize_function(&reader, &tc_allocator);
    REQUIRE(fn2 != nullptr);
    CHECK(fn2->chunk != nullptr);
    CHECK(fn2->chunk->count == fn->chunk->count);
    for (int i = 0; i < fn->chunk->count; i++)
        CHECK(fn2->chunk->code[i] == fn->chunk->code[i]);
    CHECK(fn2->chunk->constant_count == fn->chunk->constant_count);

    slp_bytecode_writer_free(&writer);
    slp_vm_free(vm);
}

TEST_CASE("Bytecode: serialize string constants") {
    SlpAllocator *alloc = &tc_allocator;
    SlpChunk chunk;
    slp_chunk_init(&chunk, alloc);
    slp_chunk_write(&chunk, OP_PUSH_CONST, 1);
    slp_chunk_write_short(&chunk, 0, 1);
    slp_chunk_add_constant(&chunk, SLP_OBJ_VAL(slp_obj_string_new(alloc, "hello", 5)));

    SlpBytecodeWriter writer;
    slp_bytecode_writer_init(&writer, alloc);
    bool ok = slp_bytecode_serialize_chunk(&writer, &chunk);
    CHECK(ok);

    SlpBytecodeReader reader = slp_bytecode_reader_init(writer.buffer, writer.count);
    SlpChunk *chunk2 = slp_bytecode_deserialize_chunk(&reader, alloc);
    REQUIRE(chunk2 != nullptr);
    CHECK(chunk2->constant_count == 1);
    CHECK(SLP_IS_OBJ(chunk2->constants[0]));
    CHECK(SLP_OBJ_TYPE(chunk2->constants[0]) == SLP_OBJ_STRING);
    SlpObjString *s = SLP_AS_STRING(chunk2->constants[0]);
    CHECK(s->length == 5);
    CHECK(memcmp(s->chars, "hello", 5) == 0);

    slp_chunk_free(chunk2);
    slp_bytecode_writer_free(&writer);
    slp_chunk_free(&chunk);
}

TEST_CASE("Bytecode: bad magic returns null") {
    uint8_t bad_data[] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01};
    SlpBytecodeReader reader = slp_bytecode_reader_init(bad_data, sizeof(bad_data));
    SlpObjFunction *fn = slp_bytecode_deserialize_function(&reader, &tc_allocator);
    CHECK(fn == nullptr);
}

TEST_CASE("Bytecode: truncated data returns null") {
    uint8_t data[] = {0x53, 0x4C, 0x59, 0x50};
    SlpBytecodeReader reader = slp_bytecode_reader_init(data, sizeof(data));
    SlpObjFunction *fn = slp_bytecode_deserialize_function(&reader, &tc_allocator);
    CHECK(fn == nullptr);
}
