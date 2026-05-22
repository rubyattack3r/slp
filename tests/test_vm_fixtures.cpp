#include "doctest.h"
extern "C" {
#include "slp_common.h"
#include "slp_core.h"
#include "slp_vm.h"
}
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
static void *vmfix_alloc(void *ptr, size_t size, void *ud) {
    (void)ud;
    if (size == 0) { free(ptr); return NULL; }
    return realloc(ptr, size);
}
static SlpAllocator vmfix_allocator = {vmfix_alloc, NULL};

struct VMFixtureResult {
    char name[128];
    SlpResult result;
    bool found;
};

static int compare_fixture_names(const void *a, const void *b) {
    return strcmp(((const VMFixtureResult*)a)->name, ((const VMFixtureResult*)b)->name);
}
}

static void fix_err_handler(void *ud, int line, const char *msg) {
    printf("ERR line %d: %s\n", line, msg);
}

static VMFixtureResult run_vm_fixture(const char *filepath, const char *name) {
    VMFixtureResult r;
    snprintf(r.name, sizeof(r.name), "%s", name);
    r.result = SLP_RUNTIME_ERROR;
    r.found = false;

    FILE *f = fopen(filepath, "rb");
    if (!f) return r;
    r.found = true;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *source = (char*)malloc(fsize + 1);
    size_t nread = fread(source, 1, fsize, f);
    source[nread] = '\0';
    fclose(f);

    SlpVM *vm = slp_vm_new(&vmfix_allocator);
    slp_vm_set_error_fn(vm, fix_err_handler, NULL);
    r.result = slp_vm_interpret(vm, source);
    slp_vm_free(vm);
    free(source);
    return r;
}

TEST_CASE("VM fixtures: all scripts pass self-checking asserts") {
    const char *dir_path = "tests/fixtures_vm";
    DIR *d = opendir(dir_path);
    REQUIRE(d != nullptr);

    VMFixtureResult results[256];
    int count = 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != nullptr && count < 256) {
        const char *ext = strrchr(ent->d_name, '.');
        if (!ext || strcmp(ext, ".sl") != 0) continue;

        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, ent->d_name);
        results[count++] = run_vm_fixture(filepath, ent->d_name);
    }
    closedir(d);

    qsort(results, count, sizeof(VMFixtureResult), compare_fixture_names);

    int passed = 0, failed = 0;
    printf("\n=== VM Fixture Test Results ===\n");
    for (int i = 0; i < count; i++) {
        if (results[i].result == SLP_OK) {
            printf("  [PASS] %s\n", results[i].name);
            passed++;
        } else {
            printf("  [FAIL] %s (result=%d)\n", results[i].name, results[i].result);
            failed++;
        }
    }
    printf("Total: %d | Passed: %d | Failed: %d\n", count, passed, failed);

    for (int i = 0; i < count; i++) {
        if (results[i].result != SLP_OK) {
            FAIL(results[i].name, " did not pass its asserts");
        }
    }

    REQUIRE(count > 0);
    CHECK(passed == count);
}
