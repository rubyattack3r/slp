#include "doctest.h"

extern "C" {
#include "slp_stdlib.h"
#include "slp_vm.h"
}

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

namespace {

static void *reference_alloc(void *memory, size_t size, void *user_data) {
    (void)user_data;
    if (size == 0) {
        free(memory);
        return NULL;
    }
    return realloc(memory, size);
}

static SlpAllocator reference_allocator = {reference_alloc, NULL};

static std::string read_file(const std::string &path) {
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) return std::string();
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    std::string contents;
    if (length > 0) {
        contents.resize((size_t)length);
        size_t read = fread(&contents[0], 1, (size_t)length, file);
        contents.resize(read);
    }
    fclose(file);
    return contents;
}

static std::vector<std::string> supported_fixtures() {
    std::vector<std::string> names;
    std::string contents = read_file("tests/reference_supported.txt");
    size_t cursor = 0;
    while (cursor < contents.size()) {
        size_t end = contents.find('\n', cursor);
        if (end == std::string::npos) end = contents.size();
        std::string line = contents.substr(cursor, end - cursor);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                                 line.back() == '\t'))
            line.pop_back();
        size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] != '#')
            names.push_back(line.substr(first));
        cursor = end + 1;
    }
    return names;
}

struct OutputCapture {
    std::string output;
    std::string errors;
};

static void capture_write(void *user_data, const char *text) {
    OutputCapture *capture = (OutputCapture *)user_data;
    if (text) capture->output += text;
}

static void capture_error(void *user_data, int line, const char *message) {
    OutputCapture *capture = (OutputCapture *)user_data;
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "line %d: ", line);
    capture->errors += prefix;
    if (message) capture->errors += message;
    capture->errors += "\n";
}

} // namespace

TEST_CASE("Reference conformance ledger contains the complete upstream oracle") {
    DIR *directory = opendir("tests/reference_output");
    REQUIRE(directory != NULL);

    int output_count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        const char *extension = strrchr(entry->d_name, '.');
        if (extension && strcmp(extension, ".sl") == 0)
            output_count++;
    }
    closedir(directory);

    CHECK(output_count == 342);
}

TEST_CASE("Supported fixtures match official Sleep 2.1 output byte-for-byte") {
    std::vector<std::string> names = supported_fixtures();
    REQUIRE(!names.empty());

    for (size_t i = 0; i < names.size(); i++) {
        CAPTURE(names[i]);
        std::string source_path = "tests/fixtures/" + names[i];
        std::string output_path = "tests/reference_output/" + names[i];
        std::string source = read_file(source_path);
        std::string expected = read_file(output_path);
        REQUIRE(!source.empty());

        OutputCapture capture;
        SlpVM *vm = slp_vm_new(&reference_allocator);
        REQUIRE(vm != NULL);
        slp_stdlib_init(vm);
        slp_vm_set_write_fn(vm, capture_write, &capture);
        slp_vm_set_error_fn(vm, capture_error, &capture);
        slp_vm_set_source_name(vm, names[i].c_str());

        SlpResult result = slp_vm_interpret(vm, source.c_str());
        CHECK_MESSAGE(result == SLP_OK, capture.errors);
        CHECK(capture.output == expected);
        slp_vm_free(vm);
    }
}
