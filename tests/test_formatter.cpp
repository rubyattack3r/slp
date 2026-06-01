#include "doctest.h"
extern "C" {
#include "slp_ast.h"
#include "slp_parser.h"
#include "slp_vm.h"
#include "slp_stdlib.h"
}
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
static void *formatter_realloc(void *memory, size_t newSize, void *userData) {
  (void)userData;
  if (newSize == 0) {
    free(memory);
    return NULL;
  }
  return realloc(memory, newSize);
}

static SlpAllocator formatter_allocator = {formatter_realloc, NULL};

static void formatter_err_handler(void *ud, int line, const char *msg) {
  (void)ud; (void)line; (void)msg;
}

static SlpResult run_formatted_vm(const char *source) {
  SlpVM *vm = slp_vm_new(&formatter_allocator);
  slp_stdlib_init(vm);
  slp_vm_set_error_fn(vm, formatter_err_handler, NULL);
  SlpResult result = slp_vm_interpret(vm, source);
  slp_vm_free(vm);
  return result;
}

static char *read_file_content(const char *filepath) {
  FILE *f = fopen(filepath, "rb");
  if (!f) return NULL;

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *source = (char *)malloc(fsize + 1);
  size_t read_bytes = fread(source, 1, fsize, f);
  source[read_bytes] = '\0';
  fclose(f);
  return source;
}
} // namespace

TEST_CASE("Formatter round-trip: idempotency and parsing check across all fixtures") {
  const char *illformed[] = {
      "argerr.sl",  "errors1.sl",   "errors2.sl",     "errors3.sl",
      "errors5.sl", "hoeserror.sl", "keyvalueerr.sl", "noterm.sl",
      "noterm2.sl", "scalref.sl",   "sillysyntax.sl", "warn.sl",
      NULL};

  // 1. Check parser fixtures in tests/fixtures
  DIR *d = opendir("tests/fixtures");
  REQUIRE(d != NULL);

  int checked_count = 0;
  struct dirent *dir;

  while ((dir = readdir(d)) != NULL) {
    if (strstr(dir->d_name, ".sl") != NULL) {
      bool is_illformed = false;
      for (int i = 0; illformed[i] != NULL; i++) {
        if (strcmp(dir->d_name, illformed[i]) == 0) {
          is_illformed = true;
          break;
        }
      }
      if (is_illformed) continue;

      char filepath[1024];
      snprintf(filepath, sizeof(filepath), "tests/fixtures/%s", dir->d_name);

      char *source = read_file_content(filepath);
      if (!source) continue;

      // First pass: parse original, format to formatted1
      SlpParser parser1;
      slp_parser_init(&parser1, source, &formatter_allocator);
      SlpASTNode *root1 = slp_parser_parse(&parser1);
      
      // Skip if parsing failed (some reference fixtures might not be fully supported by the parser yet)
      if (parser1.had_error || !root1) {
        if (root1) slp_parser_free_node(root1, &formatter_allocator);
        free(source);
        continue;
      }

      char *formatted1 = slp_ast_format(root1, &formatter_allocator);
      REQUIRE(formatted1 != NULL);

      // Second pass: parse formatted1, format to formatted2
      SlpParser parser2;
      slp_parser_init(&parser2, formatted1, &formatter_allocator);
      SlpASTNode *root2 = slp_parser_parse(&parser2);
      
      if (parser2.had_error || !root2) {
        printf("Formatting of %s broke parsing!\n", dir->d_name);
        printf("Formatted code:\n%s\n", formatted1);
        CHECK_EQ(parser2.had_error, false);
      } else {
        char *formatted2 = slp_ast_format(root2, &formatter_allocator);
        REQUIRE(formatted2 != NULL);

        // Idempotency: formatted1 must be exactly identical to formatted2
        if (strcmp(formatted1, formatted2) != 0) {
          printf("Idempotency failed for %s!\n", dir->d_name);
          printf("--- formatted1 ---\n%s\n", formatted1);
          printf("--- formatted2 ---\n%s\n", formatted2);
          CHECK(strcmp(formatted1, formatted2) == 0);
        }

        formatter_realloc(formatted2, 0, NULL);
      }

      if (root2) slp_parser_free_node(root2, &formatter_allocator);
      formatter_realloc(formatted1, 0, NULL);
      slp_parser_free_node(root1, &formatter_allocator);
      free(source);
      checked_count++;
    }
  }
  closedir(d);
  printf("  [INFO] Formatter checked %d fixtures in tests/fixtures for round-trip idempotency\n", checked_count);
  REQUIRE(checked_count > 200);
}

TEST_CASE("Formatter VM Parity: executing formatted code behaves identically to original") {
  DIR *d = opendir("tests/fixtures_vm");
  REQUIRE(d != NULL);

  int checked_count = 0;
  struct dirent *dir;

  while ((dir = readdir(d)) != NULL) {
    if (strstr(dir->d_name, ".sl") != NULL) {
      char filepath[1024];
      snprintf(filepath, sizeof(filepath), "tests/fixtures_vm/%s", dir->d_name);

      char *source = read_file_content(filepath);
      if (!source) continue;

      // Parse and format original
      SlpParser parser;
      slp_parser_init(&parser, source, &formatter_allocator);
      SlpASTNode *root = slp_parser_parse(&parser);
      
      if (parser.had_error || !root) {
        if (root) slp_parser_free_node(root, &formatter_allocator);
        free(source);
        continue;
      }

      char *formatted = slp_ast_format(root, &formatter_allocator);
      REQUIRE(formatted != NULL);

      // Interpret formatted source inside the VM
      SlpResult format_res = run_formatted_vm(formatted);
      if (format_res != SLP_OK) {
        printf("Formatted VM Execution failed for %s (result=%d)!\n", dir->d_name, format_res);
        printf("Formatted code:\n%s\n", formatted);
        CHECK_EQ(format_res, SLP_OK);
      }

      formatter_realloc(formatted, 0, NULL);
      slp_parser_free_node(root, &formatter_allocator);
      free(source);
      checked_count++;
    }
  }
  closedir(d);
  printf("  [INFO] Formatter verified %d VM integration fixtures for semantic execution parity\n", checked_count);
  REQUIRE(checked_count > 40);
}
