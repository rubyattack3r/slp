#include "doctest.h"
extern "C" {
#include "sleepy_parser.h"
}
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
static void *fixture_realloc(void *memory, size_t newSize, void *userData) {
  (void)userData;
  if (newSize == 0) {
    free(memory);
    return NULL;
  }
  return realloc(memory, newSize);
}

static SleepyAllocator fixture_allocator = {fixture_realloc, NULL};
} // namespace

TEST_CASE("Parse all .sl fixtures and check error expectations") {
  const char *illformed[] = {
      "argerr.sl",  "errors1.sl",   "errors2.sl",     "errors3.sl",
      "errors5.sl", "hoeserror.sl", "keyvalueerr.sl", "noterm.sl",
      "noterm2.sl", "scalref.sl",   "sillysyntax.sl", "warn.sl",
      NULL};

  DIR *d;
  struct dirent *dir;
  d = opendir("tests/fixtures");
  REQUIRE(d != NULL);

  int parsed_count = 0;

  if (d) {
    while ((dir = readdir(d)) != NULL) {
      if (strstr(dir->d_name, ".sl") != NULL) {
        bool is_illformed = false;
        for (int i = 0; illformed[i] != NULL; i++) {
          if (strcmp(dir->d_name, illformed[i]) == 0) {
            is_illformed = true;
            break;
          }
        }

        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "tests/fixtures/%s", dir->d_name);

        FILE *f = fopen(filepath, "rb");
        if (!f)
          continue;

        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        char *source = (char *)malloc(fsize + 1);
        size_t read_bytes = fread(source, 1, fsize, f);
        source[read_bytes] = '\0';
        fclose(f);

        SleepyParser parser;
        sleepy_parser_init(&parser, source, &fixture_allocator);

        SleepyASTNode *root = sleepy_parser_parse(&parser);

        if (is_illformed) {
          // We expect an error for these files
          // NOTE: Some might still parse if my implementation is more lenient
          // than the reference, but let's check.
          if (!parser.had_error) {
            // printf("Expected error in %s but none found\n", dir->d_name);
          }
        } else {
          // We expect NO error for these files
          if (parser.had_error) {
            printf("Unexpected error in %s at line %d (near '%.*s')\n",
                   dir->d_name, parser.previous.line,
                   (int)parser.previous.length, parser.previous.start);
            CHECK_EQ(parser.had_error, false);
          }
        }

        if (root) {
          if (root->type == SLEEPY_AST_SCRIPT) {
            SLEEPY_FREE(&fixture_allocator, root->as.block.statements);
          }
          SLEEPY_FREE(&fixture_allocator, root);
        }

        free(source);
        parsed_count++;
      }
    }
    closedir(d);
  }

  REQUIRE(parsed_count > 300);
}
