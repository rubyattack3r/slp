#include "doctest.h"

extern "C" {
#include "slp_vm.h"
}

#include <stdlib.h>

#include <string>

namespace {

static void *parsed_string_alloc(void *memory, size_t size, void *user_data) {
    (void)user_data;
    if (size == 0) {
        free(memory);
        return NULL;
    }
    return realloc(memory, size);
}

static SlpAllocator parsed_string_allocator = {parsed_string_alloc, NULL};

static void capture_parsed_string(void *user_data, const char *text) {
    std::string *output = static_cast<std::string *>(user_data);
    if (text) *output += text;
}

} // namespace

TEST_CASE("Parsed strings use Sleep variable terminators and alignment") {
    SlpVM *vm = slp_vm_new(&parsed_string_allocator);
    REQUIRE(vm != NULL);
    std::string output;
    slp_vm_set_write_fn(vm, capture_parsed_string, &output);

    SlpResult result = slp_vm_interpret(
        vm,
        "$x = 'cat';\n"
        "$width = 5;\n"
        "println(\"[$[$width]x $+ ]\");\n"
        "println(\"[$[-5]x $+ ]\");\n"
        "println(\"undefined punctuation disappears: $%^&\");\n");

    CHECK(result == SLP_OK);
    CHECK(output ==
          "[cat  ]\n"
          "[  cat]\n"
          "undefined punctuation disappears: \n");
    slp_vm_free(vm);
}

TEST_CASE("Parsed strings reject malformed interpolation operators") {
    SlpVM *vm = slp_vm_new(&parsed_string_allocator);
    REQUIRE(vm != NULL);

    CHECK(slp_vm_interpret(vm, "\"$x$+!\";") == SLP_COMPILE_ERROR);
    CHECK(slp_vm_interpret(vm, "\"$[]x\";") == SLP_COMPILE_ERROR);
    slp_vm_free(vm);
}
