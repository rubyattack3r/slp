#include "doctest.h"

extern "C" {
#include "slp_stdlib.h"
#include "slp_vm.h"
}

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <set>
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

struct UnverifiedFixture {
    std::string name;
    std::string status;
    std::string reason;
};

static std::vector<UnverifiedFixture> unverified_fixtures() {
    std::vector<UnverifiedFixture> fixtures;
    std::string contents = read_file("tests/reference_unverified.tsv");
    size_t cursor = 0;
    while (cursor < contents.size()) {
        size_t end = contents.find('\n', cursor);
        if (end == std::string::npos) end = contents.size();
        std::string line = contents.substr(cursor, end - cursor);
        while (!line.empty() && line.back() == '\r') line.pop_back();
        size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] != '#') {
            line = line.substr(first);
            size_t first_tab = line.find('\t');
            size_t second_tab = first_tab == std::string::npos
                                    ? std::string::npos
                                    : line.find('\t', first_tab + 1);
            UnverifiedFixture fixture;
            if (first_tab != std::string::npos &&
                second_tab != std::string::npos) {
                fixture.name = line.substr(0, first_tab);
                fixture.status =
                    line.substr(first_tab + 1, second_tab - first_tab - 1);
                fixture.reason = line.substr(second_tab + 1);
            }
            fixtures.push_back(fixture);
        }
        cursor = end + 1;
    }
    return fixtures;
}

static std::vector<std::string> fixture_names(const char *directory_path) {
    std::vector<std::string> names;
    DIR *directory = opendir(directory_path);
    if (!directory) return names;

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        const char *extension = strrchr(entry->d_name, '.');
        if (extension && strcmp(extension, ".sl") == 0)
            names.push_back(entry->d_name);
    }
    closedir(directory);
    std::sort(names.begin(), names.end());
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
    std::vector<std::string> sources = fixture_names("tests/fixtures");
    std::vector<std::string> outputs = fixture_names("tests/reference_output");
    std::vector<std::string> supported = supported_fixtures();
    std::vector<UnverifiedFixture> unverified = unverified_fixtures();

    REQUIRE(sources.size() == 342);
    CHECK(outputs == sources);
    CHECK(supported.size() == 227);
    CHECK(unverified.size() == 115);

    std::set<std::string> source_set(sources.begin(), sources.end());
    std::set<std::string> ledger;
    for (size_t i = 0; i < supported.size(); i++) {
        CAPTURE(supported[i]);
        CHECK(ledger.insert(supported[i]).second);
    }

    const std::set<std::string> valid_statuses = {
        "execution-error", "fixture-cwd", "output-mismatch", "timeout"};
    for (size_t i = 0; i < unverified.size(); i++) {
        CAPTURE(unverified[i].name);
        CHECK(!unverified[i].name.empty());
        CHECK(valid_statuses.count(unverified[i].status) == 1);
        CHECK(!unverified[i].reason.empty());
        CHECK(ledger.insert(unverified[i].name).second);
    }

    CHECK(ledger == source_set);
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
