#include "doctest.h"
#include <string.h>
#include <stdlib.h>

extern "C" {
#include "sleepy_common.h"
#include "sleepy_core.h"
#include "sleepy_value.h"
#include "sleepy_vm.h"
#include "sleepy_compiler.h"
#include "sleepy_parser.h"
#include "sleepy_ast.h"
#include "sleepy_opcodes.h"
#include "sleepy_bytecode.h"

static void *tc_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static SleepyAllocator tc_allocator = {tc_alloc, NULL};
}

static SleepyObjFunction *compile_source(const char *source, SleepyVM *vm) {
    SleepyParser parser;
    sleepy_parser_init(&parser, source, &tc_allocator);
    SleepyASTNode *ast = sleepy_parser_parse(&parser);
    if (!ast) return nullptr;
    SleepyObjFunction *fn = sleepy_compile(vm, ast, &tc_allocator);
    sleepy_parser_free_node(ast, &tc_allocator);
    return fn;
}

TEST_CASE("Compiler: null literal") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("$null;", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    CHECK(c->count >= 3);
    CHECK(c->code[0] == OP_PUSH_NULL);
    CHECK(c->code[c->count - 1] == OP_RETURN);
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: boolean literals") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("true; false;", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_TRUE);
    CHECK(c->code[1] == OP_POP);
    CHECK(c->code[2] == OP_PUSH_FALSE);
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: number literal") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("42;", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_CONST);
    CHECK(c->constant_count == 1);
    CHECK(SLEEPY_IS_NUM(c->constants[0]));
    CHECK(SLEEPY_AS_NUM(c->constants[0]) == doctest::Approx(42));
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: string literal") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("\"hello\";", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_CONST);
    CHECK(c->constant_count >= 1);
    CHECK(SLEEPY_IS_OBJ(c->constants[0]));
    CHECK(SLEEPY_OBJ_TYPE(c->constants[0]) == SLEEPY_OBJ_STRING);
    SleepyObjString *s = SLEEPY_AS_STRING(c->constants[0]);
    CHECK(s->length == 5);
    CHECK(strcmp(s->chars, "hello") == 0);
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: addition") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("1 + 2;", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_CONST);
    CHECK(c->code[3] == OP_PUSH_CONST);
    CHECK(c->code[6] == OP_ADD);
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: variable assignment and access") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("$x = 10; $x;", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_CONST);
    CHECK(c->code[3] == OP_STORE_GLOBAL);
    SleepyObjString *name = SLEEPY_AS_STRING(c->constants[sleepy_chunk_read_short(c, 4)]);
    CHECK(strcmp(name->chars, "x") == 0);
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: if statement") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("if (true) { 1; }", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_TRUE);
    CHECK(c->code[1] == OP_JUMP_IF_FALSE);
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: if-else statement") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("if (true) { 1; } else { 2; }", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_TRUE);
    CHECK(c->code[1] == OP_JUMP_IF_FALSE);
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: while loop") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("while (true) { break; }", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_TRUE);
    CHECK(c->code[1] == OP_JUMP_IF_FALSE);
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: function call") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("foo(1, 2);", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    bool found_call = false;
    for (int i = 0; i < c->count; i++) {
        if (c->code[i] == OP_CALL) { found_call = true; break; }
    }
    CHECK(found_call);
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: negation") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("-42;", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_CONST);
    CHECK(c->code[3] == OP_NEGATE);
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: not operator") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("!true;", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    CHECK(c->code[0] == OP_PUSH_TRUE);
    CHECK(c->code[1] == OP_NOT);
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: comparison operators") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    const char *ops[] = {"1 < 2;", "1 > 2;", "1 <= 2;", "1 >= 2;", "1 == 2;", "1 != 2;"};
    SleepyOpcode expected[] = {OP_LESS, OP_GREATER, OP_LESS_EQUAL, OP_GREATER_EQUAL, OP_EQUAL, OP_NOT_EQUAL};
    for (int i = 0; i < 6; i++) {
        SleepyObjFunction *fn = compile_source(ops[i], vm);
        REQUIRE(fn != nullptr);
        bool found = false;
        for (int j = 0; j < fn->chunk->count; j++) {
            if (fn->chunk->code[j] == expected[i]) { found = true; break; }
        }
        CHECK(found);
    }
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: arithmetic operators") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    const char *ops[] = {"1 + 2;", "1 - 2;", "1 * 2;", "1 / 2;", "1 % 2;"};
    SleepyOpcode expected[] = {OP_ADD, OP_SUBTRACT, OP_MULTIPLY, OP_DIVIDE, OP_MODULO};
    for (int i = 0; i < 5; i++) {
        SleepyObjFunction *fn = compile_source(ops[i], vm);
        REQUIRE(fn != nullptr);
        bool found = false;
        for (int j = 0; j < fn->chunk->count; j++) {
            if (fn->chunk->code[j] == expected[i]) { found = true; break; }
        }
        CHECK(found);
    }
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: return statement") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("return 42;", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    bool found_return = false;
    for (int i = 0; i < c->count; i++) {
        if (c->code[i] == OP_RETURN) { found_return = true; break; }
    }
    CHECK(found_return);
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: empty source") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source(" ", vm);
    if (fn) {
        SleepyChunk *c = fn->chunk;
        CHECK(c->count >= 2);
        CHECK(c->code[c->count - 1] == OP_RETURN);
    }
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: multiple statements") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("1; 2; 3;", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    int push_count = 0;
    for (int i = 0; i < c->count; i++) {
        if (c->code[i] == OP_PUSH_CONST) push_count++;
    }
    CHECK(push_count == 3);
    sleepy_vm_free(vm);
}

TEST_CASE("Compiler: try/catch") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("try { 1; } catch $e { 2; }", vm);
    REQUIRE(fn != nullptr);
    SleepyChunk *c = fn->chunk;
    bool found_handler = false, found_pop_handler = false;
    for (int i = 0; i < c->count; i++) {
        if (c->code[i] == OP_PUSH_HANDLER) found_handler = true;
        if (c->code[i] == OP_POP_HANDLER) found_pop_handler = true;
    }
    CHECK(found_handler);
    CHECK(found_pop_handler);
    sleepy_vm_free(vm);
}

TEST_CASE("Bytecode: serialize/deserialize chunk") {
    SleepyAllocator *alloc = &tc_allocator;
    SleepyChunk chunk;
    sleepy_chunk_init(&chunk, alloc);
    sleepy_chunk_write(&chunk, OP_PUSH_CONST, 1);
    sleepy_chunk_write_short(&chunk, 0, 1);
    sleepy_chunk_write(&chunk, OP_PUSH_CONST, 1);
    sleepy_chunk_write_short(&chunk, 1, 1);
    sleepy_chunk_write(&chunk, OP_ADD, 1);
    sleepy_chunk_write(&chunk, OP_RETURN, 1);
    sleepy_chunk_add_constant(&chunk, SLEEPY_NUM_VAL(1.0));
    sleepy_chunk_add_constant(&chunk, SLEEPY_NUM_VAL(2.0));

    SleepyBytecodeWriter writer;
    sleepy_bytecode_writer_init(&writer, alloc);
    bool ok = sleepy_bytecode_serialize_chunk(&writer, &chunk);
    CHECK(ok);
    CHECK(writer.count > 0);

    SleepyBytecodeReader reader = sleepy_bytecode_reader_init(writer.buffer, writer.count);
    SleepyChunk *chunk2 = sleepy_bytecode_deserialize_chunk(&reader, alloc);
    REQUIRE(chunk2 != nullptr);
    CHECK(chunk2->count == chunk.count);
    for (int i = 0; i < chunk.count; i++)
        CHECK(chunk2->code[i] == chunk.code[i]);
    CHECK(chunk2->constant_count == 2);
    CHECK(SLEEPY_AS_NUM(chunk2->constants[0]) == doctest::Approx(1.0));
    CHECK(SLEEPY_AS_NUM(chunk2->constants[1]) == doctest::Approx(2.0));

    sleepy_chunk_free(chunk2);
    sleepy_bytecode_writer_free(&writer);
    sleepy_chunk_free(&chunk);
}

TEST_CASE("Bytecode: serialize/deserialize function") {
    SleepyVM *vm = sleepy_vm_new(&tc_allocator);
    SleepyObjFunction *fn = compile_source("1 + 2;", vm);
    REQUIRE(fn != nullptr);

    SleepyBytecodeWriter writer;
    sleepy_bytecode_writer_init(&writer, &tc_allocator);
    bool ok = sleepy_bytecode_serialize_function(&writer, fn);
    CHECK(ok);
    CHECK(writer.count > 0);

    SleepyBytecodeReader reader = sleepy_bytecode_reader_init(writer.buffer, writer.count);
    SleepyObjFunction *fn2 = sleepy_bytecode_deserialize_function(&reader, &tc_allocator);
    REQUIRE(fn2 != nullptr);
    CHECK(fn2->chunk != nullptr);
    CHECK(fn2->chunk->count == fn->chunk->count);
    for (int i = 0; i < fn->chunk->count; i++)
        CHECK(fn2->chunk->code[i] == fn->chunk->code[i]);
    CHECK(fn2->chunk->constant_count == fn->chunk->constant_count);

    sleepy_bytecode_writer_free(&writer);
    sleepy_vm_free(vm);
}

TEST_CASE("Bytecode: serialize string constants") {
    SleepyAllocator *alloc = &tc_allocator;
    SleepyChunk chunk;
    sleepy_chunk_init(&chunk, alloc);
    sleepy_chunk_write(&chunk, OP_PUSH_CONST, 1);
    sleepy_chunk_write_short(&chunk, 0, 1);
    sleepy_chunk_add_constant(&chunk, SLEEPY_OBJ_VAL(sleepy_obj_string_new(alloc, "hello", 5)));

    SleepyBytecodeWriter writer;
    sleepy_bytecode_writer_init(&writer, alloc);
    bool ok = sleepy_bytecode_serialize_chunk(&writer, &chunk);
    CHECK(ok);

    SleepyBytecodeReader reader = sleepy_bytecode_reader_init(writer.buffer, writer.count);
    SleepyChunk *chunk2 = sleepy_bytecode_deserialize_chunk(&reader, alloc);
    REQUIRE(chunk2 != nullptr);
    CHECK(chunk2->constant_count == 1);
    CHECK(SLEEPY_IS_OBJ(chunk2->constants[0]));
    CHECK(SLEEPY_OBJ_TYPE(chunk2->constants[0]) == SLEEPY_OBJ_STRING);
    SleepyObjString *s = SLEEPY_AS_STRING(chunk2->constants[0]);
    CHECK(s->length == 5);
    CHECK(memcmp(s->chars, "hello", 5) == 0);

    sleepy_chunk_free(chunk2);
    sleepy_bytecode_writer_free(&writer);
    sleepy_chunk_free(&chunk);
}

TEST_CASE("Bytecode: bad magic returns null") {
    uint8_t bad_data[] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01};
    SleepyBytecodeReader reader = sleepy_bytecode_reader_init(bad_data, sizeof(bad_data));
    SleepyObjFunction *fn = sleepy_bytecode_deserialize_function(&reader, &tc_allocator);
    CHECK(fn == nullptr);
}

TEST_CASE("Bytecode: truncated data returns null") {
    uint8_t data[] = {0x53, 0x4C, 0x59, 0x50};
    SleepyBytecodeReader reader = sleepy_bytecode_reader_init(data, sizeof(data));
    SleepyObjFunction *fn = sleepy_bytecode_deserialize_function(&reader, &tc_allocator);
    CHECK(fn == nullptr);
}
