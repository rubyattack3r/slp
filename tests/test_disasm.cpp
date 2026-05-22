#include "doctest.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern "C" {
#include "sleepy_common.h"
#include "sleepy_core.h"
#include "sleepy_value.h"
#include "sleepy_vm.h"
#include "sleepy_compiler.h"
#include "sleepy_parser.h"
#include "sleepy_ast.h"
#include "sleepy_disasm.h"

static void *td_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static SleepyAllocator td_allocator = {td_alloc, NULL};
}

TEST_CASE("Disassembler: Opcode names") {
    CHECK(strcmp(sleepy_opcode_name(OP_PUSH_NULL), "PUSH_NULL") == 0);
    CHECK(strcmp(sleepy_opcode_name(OP_ADD), "ADD") == 0);
    CHECK(strcmp(sleepy_opcode_name(OP_RETURN), "RETURN") == 0);
    CHECK(strcmp(sleepy_opcode_name(OP_HALT), "HALT") == 0);
}

TEST_CASE("Disassembler: Instruction & Chunk Disassembly") {
    SleepyVM *vm = sleepy_vm_new(&td_allocator);
    REQUIRE(vm != nullptr);

    const char *source = "$x = 10; $y = $x + 2;";
    SleepyParser parser;
    sleepy_parser_init(&parser, source, &td_allocator);
    SleepyASTNode *ast = sleepy_parser_parse(&parser);
    REQUIRE(ast != nullptr);

    SleepyObjFunction *fn = sleepy_compile(vm, ast, &td_allocator);
    REQUIRE(fn != nullptr);

    // Let's disassemble the chunk to a temporary file
    FILE *tmp = tmpfile();
    REQUIRE(tmp != nullptr);

    sleepy_disasm_chunk(fn->chunk, "test_script", tmp);

    // Read the file contents back to verify
    rewind(tmp);
    char buffer[4096];
    size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, tmp);
    buffer[bytes_read] = '\0';
    fclose(tmp);

    // Verify key disassembler output markers are present
    CHECK(strstr(buffer, "=== test_script ===") != nullptr);
    CHECK(strstr(buffer, "PUSH_CONST") != nullptr);
    CHECK(strstr(buffer, "STORE_GLOBAL") != nullptr);
    CHECK(strstr(buffer, "LOAD_GLOBAL") != nullptr);
    CHECK(strstr(buffer, "ADD") != nullptr);
    CHECK(strstr(buffer, "RETURN") != nullptr);

    sleepy_parser_free_node(ast, &td_allocator);
    sleepy_vm_free(vm);
}
