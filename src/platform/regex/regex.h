#ifndef SLP_PLATFORM_REGEX_H
#define SLP_PLATFORM_REGEX_H

#include <stddef.h>
#include "slp_core.h"

/*
 * Private POSIX-regex compatibility declarations for Windows builds.
 *
 * The implementation is the C99 TRE engine distributed by musl libc. Keep
 * this header private to the platform backend so non-Windows builds continue
 * using their system <regex.h> ABI.
 */
typedef ptrdiff_t regoff_t;

typedef struct re_pattern_buffer {
    size_t re_nsub;
    void *__opaque;
    void *__padding[4];
    size_t __nsub2;
    char __padding2;
    SlpAllocator *__slp_allocator;
} regex_t;

typedef struct {
    regoff_t rm_so;
    regoff_t rm_eo;
} regmatch_t;

#define REG_EXTENDED 1
#define REG_ICASE 2
#define REG_NEWLINE 4
#define REG_NOSUB 8

#define REG_NOTBOL 1
#define REG_NOTEOL 2

#define REG_OK 0
#define REG_NOMATCH 1
#define REG_BADPAT 2
#define REG_ECOLLATE 3
#define REG_ECTYPE 4
#define REG_EESCAPE 5
#define REG_ESUBREG 6
#define REG_EBRACK 7
#define REG_EPAREN 8
#define REG_EBRACE 9
#define REG_BADBR 10
#define REG_ERANGE 11
#define REG_ESPACE 12
#define REG_BADRPT 13
#define REG_ENOSYS (-1)

#ifndef RE_DUP_MAX
#define RE_DUP_MAX 255
#endif

#ifndef CHARCLASS_NAME_MAX
#define CHARCLASS_NAME_MAX 14
#endif

int slp_regex_compile(
    SlpAllocator *allocator, regex_t *regex,
    const char *pattern, int flags);
int slp_regex_execute(
    const regex_t *regex, const char *text,
    size_t match_count, regmatch_t *matches, int flags);
void slp_regex_free(regex_t *regex);

#endif
