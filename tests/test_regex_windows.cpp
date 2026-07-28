#include "doctest.h"

#ifdef _WIN32

extern "C" {
#include "../src/platform/regex/regex.h"
}

#include <cstdlib>

struct RegexAllocationTracker {
    size_t active;
    size_t calls;
};

static void *tracked_regex_reallocate(
    void *pointer, size_t size, void *user_data) {
    RegexAllocationTracker *tracker =
        static_cast<RegexAllocationTracker *>(user_data);
    tracker->calls++;
    if (size == 0) {
        if (pointer)
            tracker->active--;
        std::free(pointer);
        return nullptr;
    }
    if (pointer)
        return std::realloc(pointer, size);
    void *allocated = std::malloc(size);
    if (allocated)
        tracker->active++;
    return allocated;
}

TEST_CASE("Windows regex backend uses and balances SlpAllocator") {
    RegexAllocationTracker tracker = {};
    SlpAllocator allocator = {
        tracked_regex_reallocate, &tracker
    };
    regex_t regex = {};
    regmatch_t matches[3] = {};

    REQUIRE(
        slp_regex_compile(
            &allocator, &regex,
            "(Access)[[:space:]]+(token)",
            REG_EXTENDED) == REG_OK);
    CHECK(tracker.calls > 0);
    CHECK(tracker.active > 0);
    CHECK(
        slp_regex_execute(
            &regex, "Access token",
            3, matches, 0) == REG_OK);
    CHECK(matches[1].rm_so == 0);
    CHECK(matches[1].rm_eo == 6);
    CHECK(matches[2].rm_so == 7);
    CHECK(matches[2].rm_eo == 12);

    slp_regex_free(&regex);
    CHECK(tracker.active == 0);

    regex_t invalid_regex = {};
    CHECK(
        slp_regex_compile(
            &allocator, &invalid_regex,
            "(", REG_EXTENDED) == REG_EPAREN);
    CHECK(tracker.active == 0);
}

#endif
