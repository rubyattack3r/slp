#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "slp_stdlib.h"
#include "slp_value.h"
#include "slp_vm.h"
#include "slp_platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/wait.h>
#endif

/* Allocator wrappers using VM allocator */
static void *stdlib_alloc(SlpVM *vm, size_t size) {
    return vm->allocator->reallocate(NULL, size, vm->allocator->user_data);
}

static void stdlib_free(SlpVM *vm, void *ptr) {
    vm->allocator->reallocate(ptr, 0, vm->allocator->user_data);
}

/* -----------------------------------------------------------------------
 * Control Flow Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_iff(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 3) return SLP_NULL_VAL;
    if (SLP_IS_TRUE(args[0])) return args[1];
    return args[2];
}

/* -----------------------------------------------------------------------
 * Math Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_abs(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    double v = SLP_AS_NUM(args[0]);
    return SLP_NUM_VAL(v < 0 ? -v : v);
}

static SlpValue builtin_ceil(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(ceil(SLP_AS_NUM(args[0])));
}

static SlpValue builtin_floor(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(floor(SLP_AS_NUM(args[0])));
}

static SlpValue builtin_sqrt(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(sqrt(SLP_AS_NUM(args[0])));
}

static SlpValue builtin_sin(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(sin(SLP_AS_NUM(args[0])));
}

static SlpValue builtin_cos(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(cos(SLP_AS_NUM(args[0])));
}

static SlpValue builtin_tan(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(tan(SLP_AS_NUM(args[0])));
}

static SlpValue builtin_asin(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(asin(SLP_AS_NUM(args[0])));
}

static SlpValue builtin_acos(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(acos(SLP_AS_NUM(args[0])));
}

static SlpValue builtin_atan(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(atan(SLP_AS_NUM(args[0])));
}

static SlpValue builtin_atan2(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2 || !SLP_IS_NUM(args[0]) || !SLP_IS_NUM(args[1]))
        return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(atan2(SLP_AS_NUM(args[0]), SLP_AS_NUM(args[1])));
}

static SlpValue builtin_exp(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(exp(SLP_AS_NUM(args[0])));
}

static SlpValue builtin_log(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(log(SLP_AS_NUM(args[0])));
}

static SlpValue builtin_round(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(round(SLP_AS_NUM(args[0])));
}

static SlpValue builtin_double(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    if (SLP_IS_NUM(args[0])) return args[0];
    if (SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
        return SLP_NUM_VAL(atof(SLP_AS_STRING(args[0])->chars));
    }
    return SLP_NUM_VAL(0);
}

/* -----------------------------------------------------------------------
 * String Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_strlen(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    if (!SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NUM_VAL(0);
    return SLP_NUM_VAL((double)SLP_AS_STRING(args[0])->length);
}

static SlpValue builtin_substr(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2) return SLP_NULL_VAL;
    if (!SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    int start = SLP_IS_NUM(args[1]) ? (int)SLP_AS_NUM(args[1]) : 0;
    int end = (argc >= 3 && SLP_IS_NUM(args[2]))
              ? (int)SLP_AS_NUM(args[2])
              : (int)str->length;
    if (start < 0) start = 0;
    if (end > (int)str->length) end = (int)str->length;
    if (start >= end) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "", 0));
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, str->chars + start, end - start));
}

static SlpValue builtin_lc(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    char *buf = stdlib_alloc(vm, str->length + 1);
    for (uint32_t i = 0; i < str->length; i++) buf[i] = (char)tolower(str->chars[i]);
    buf[str->length] = '\0';
    SlpObjString *result = slp_vm_copy_string(vm, buf, str->length);
    stdlib_free(vm, buf);
    return SLP_OBJ_VAL(result);
}

static SlpValue builtin_uc(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    char *buf = stdlib_alloc(vm, str->length + 1);
    for (uint32_t i = 0; i < str->length; i++) buf[i] = (char)toupper(str->chars[i]);
    buf[str->length] = '\0';
    SlpObjString *result = slp_vm_copy_string(vm, buf, str->length);
    stdlib_free(vm, buf);
    return SLP_OBJ_VAL(result);
}

static SlpValue builtin_replace(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 3) return SLP_NULL_VAL;
    if (!SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING) return args[0];
    if (!SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING) return args[0];
    if (!SLP_IS_OBJ(args[2]) || SLP_OBJ_TYPE(args[2]) != SLP_OBJ_STRING) return args[0];
    SlpObjString *src = SLP_AS_STRING(args[0]);
    SlpObjString *old = SLP_AS_STRING(args[1]);
    SlpObjString *rep = SLP_AS_STRING(args[2]);
    if (old->length == 0) return args[0];

    int count = 0;
    const char *p = src->chars;
    while ((p = strstr(p, old->chars)) != NULL) { count++; p += old->length; }
    if (count == 0) return args[0];

    uint32_t new_len = src->length + (uint32_t)count * (rep->length - old->length);
    char *buf = stdlib_alloc(vm, new_len + 1);
    char *dst = buf;
    p = src->chars;
    const char *prev = p;
    while ((p = strstr(p, old->chars)) != NULL) {
        memcpy(dst, prev, p - prev);
        dst += p - prev;
        memcpy(dst, rep->chars, rep->length);
        dst += rep->length;
        p += old->length;
        prev = p;
    }
    memcpy(dst, prev, src->chars + src->length - prev);
    dst += src->chars + src->length - prev;
    *dst = '\0';

    SlpObjString *result = slp_vm_copy_string(vm, buf, new_len);
    stdlib_free(vm, buf);
    return SLP_OBJ_VAL(result);
}

static SlpValue builtin_split(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
                    !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING) {
        return SLP_OBJ_VAL(slp_vm_new_array(vm));
    }
    const char *pattern = SLP_AS_STRING(args[0])->chars;
    const char *str = SLP_AS_STRING(args[1])->chars;
    SlpObjArray *arr = slp_vm_new_array(vm);
    
    if (strlen(pattern) == 0) {
        size_t len = strlen(str);
        for (size_t i = 0; i < len; i++) {
            slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(slp_vm_copy_string(vm, &str[i], 1)));
        }
        return SLP_OBJ_VAL(arr);
    }
    
    const char *curr = str;
    const char *next;
    size_t pat_len = strlen(pattern);
    while ((next = strstr(curr, pattern)) != NULL) {
        size_t len = next - curr;
        slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(slp_vm_copy_string(vm, curr, (uint32_t)len)));
        curr = next + pat_len;
    }
    slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(slp_vm_copy_string(vm, curr, (uint32_t)strlen(curr))));
    return SLP_OBJ_VAL(arr);
}

static SlpValue builtin_join(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
                    !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_ARRAY) {
        return SLP_OBJ_VAL(slp_vm_copy_string(vm, "", 0));
    }
    const char *sep = SLP_AS_STRING(args[0])->chars;
    SlpObjArray *arr = (SlpObjArray*)SLP_AS_OBJ(args[1]);
    
    if (arr->count == 0) {
        return SLP_OBJ_VAL(slp_vm_copy_string(vm, "", 0));
    }
    
    size_t sep_len = strlen(sep);
    size_t total_len = 0;
    for (int i = 0; i < arr->count; i++) {
        if (i > 0) total_len += sep_len;
        SlpValue val = arr->elements[i];
        if (SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING) {
            total_len += SLP_AS_STRING(val)->length;
        } else {
            if (SLP_IS_NUM(val)) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%g", SLP_AS_NUM(val));
                total_len += strlen(buf);
            } else if (SLP_IS_BOOL(val)) {
                total_len += SLP_AS_BOOL(val) ? 4 : 5;
            } else if (SLP_IS_NULL(val)) {
                total_len += 5;
            }
        }
    }
    
    char *buf = stdlib_alloc(vm, total_len + 1);
    char *curr = buf;
    *curr = '\0';
    
    for (int i = 0; i < arr->count; i++) {
        if (i > 0) {
            strcpy(curr, sep);
            curr += sep_len;
        }
        SlpValue val = arr->elements[i];
        if (SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING) {
            SlpObjString *str = SLP_AS_STRING(val);
            memcpy(curr, str->chars, str->length);
            curr += str->length;
        } else {
            if (SLP_IS_NUM(val)) {
                int written = snprintf(curr, 32, "%g", SLP_AS_NUM(val));
                curr += written;
            } else if (SLP_IS_BOOL(val)) {
                const char *s = SLP_AS_BOOL(val) ? "true" : "false";
                strcpy(curr, s);
                curr += strlen(s);
            } else if (SLP_IS_NULL(val)) {
                strcpy(curr, "$null");
                curr += 5;
            }
        }
    }
    *curr = '\0';
    
    SlpObjString *res = slp_vm_copy_string(vm, buf, (uint32_t)total_len);
    stdlib_free(vm, buf);
    return SLP_OBJ_VAL(res);
}

static SlpValue builtin_tr(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 3 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING ||
        !SLP_IS_OBJ(args[2]) || SLP_OBJ_TYPE(args[2]) != SLP_OBJ_STRING)
        return args[0];
    SlpObjString *str = SLP_AS_STRING(args[0]);
    SlpObjString *from = SLP_AS_STRING(args[1]);
    SlpObjString *to = SLP_AS_STRING(args[2]);
    char *buf = stdlib_alloc(vm, str->length + 1);
    for (uint32_t i = 0; i < str->length; i++) {
        char c = str->chars[i];
        for (uint32_t j = 0; j < from->length && j < to->length; j++) {
            if (c == from->chars[j]) { c = to->chars[j]; break; }
        }
        buf[i] = c;
    }
    buf[str->length] = '\0';
    SlpObjString *result = slp_vm_copy_string(vm, buf, str->length);
    stdlib_free(vm, buf);
    return SLP_OBJ_VAL(result);
}

static SlpValue builtin_replaceAt(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 3 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_NUM(args[1]) || !SLP_IS_OBJ(args[2]) || SLP_OBJ_TYPE(args[2]) != SLP_OBJ_STRING)
        return args[0];
    SlpObjString *str = SLP_AS_STRING(args[0]);
    int idx = (int)SLP_AS_NUM(args[1]);
    SlpObjString *rep = SLP_AS_STRING(args[2]);
    if (idx < 0 || idx >= (int)str->length) return args[0];
    char *buf = stdlib_alloc(vm, str->length + 1);
    memcpy(buf, str->chars, str->length);
    buf[str->length] = '\0';
    for (uint32_t i = 0; i < rep->length && (uint32_t)idx + i < str->length; i++)
        buf[idx + i] = rep->chars[i];
    SlpObjString *result = slp_vm_copy_string(vm, buf, str->length);
    stdlib_free(vm, buf);
    return SLP_OBJ_VAL(result);
}

static SlpValue builtin_lindexOf(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING)
        return SLP_NUM_VAL(-1);
    SlpObjString *haystack = SLP_AS_STRING(args[0]);
    SlpObjString *needle = SLP_AS_STRING(args[1]);
    if (needle->length == 0) return SLP_NUM_VAL((double)(haystack->length > 0 ? (int)haystack->length - 1 : 0));
    const char *p = haystack->chars + haystack->length;
    while (p >= haystack->chars) {
        const char *found = strstr(haystack->chars, needle->chars);
        if (!found) break;
        const char *next = found + 1;
        if (next > haystack->chars + haystack->length) break;
        p = strstr(next, needle->chars);
        if (!p) { return SLP_NUM_VAL((double)(found - haystack->chars)); }
        while (strstr(p + 1, needle->chars)) p = strstr(p + 1, needle->chars);
        return SLP_NUM_VAL((double)(p - haystack->chars));
    }
    const char *last = NULL;
    const char *scan = haystack->chars;
    while ((scan = strstr(scan, needle->chars)) != NULL) {
        last = scan;
        scan++;
    }
    if (last) return SLP_NUM_VAL((double)(last - haystack->chars));
    return SLP_NUM_VAL(-1);
}

/* -----------------------------------------------------------------------
 * Collection & Hash Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_local(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
    return SLP_NULL_VAL;
}

static SlpValue builtin_int(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    if (!SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL((double)(int)SLP_AS_NUM(args[0]));
}

static SlpValue builtin_long(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NULL_VAL;
    if (SLP_IS_NUM(args[0]))
        return SLP_OBJ_VAL(slp_obj_long_new(vm->allocator, (int64_t)SLP_AS_NUM(args[0])));
    return SLP_NULL_VAL;
}

static SlpValue builtin_uint(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL((double)(unsigned int)SLP_AS_NUM(args[0]));
}

static SlpValue builtin_chr(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NULL_VAL;
    char buf[2] = { (char)(int)SLP_AS_NUM(args[0]), '\0' };
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, buf, 1));
}

static SlpValue builtin_asc(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NUM_VAL(0);
    SlpObjString *str = SLP_AS_STRING(args[0]);
    if (str->length == 0) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL((double)(unsigned char)str->chars[0]);
}

static SlpValue builtin_charAt(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_NUM(args[1]))
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    int idx = (int)SLP_AS_NUM(args[1]);
    if (idx < 0 || idx >= (int)str->length) return SLP_NULL_VAL;
    char buf[2] = { str->chars[idx], '\0' };
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, buf, 1));
}

static SlpValue builtin_left(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_NUM(args[1]))
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    int n = (int)SLP_AS_NUM(args[1]);
    if (n < 0) n = 0;
    if (n > (int)str->length) n = (int)str->length;
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, str->chars, (uint32_t)n));
}

static SlpValue builtin_right(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_NUM(args[1]))
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    int n = (int)SLP_AS_NUM(args[1]);
    if (n < 0) n = 0;
    if (n > (int)str->length) n = (int)str->length;
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, str->chars + str->length - n, (uint32_t)n));
}

static SlpValue builtin_mid(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 3 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_NUM(args[1]) || !SLP_IS_NUM(args[2]))
        return SLP_NULL_VAL;
    SlpObjString *str = SLP_AS_STRING(args[0]);
    int start = (int)SLP_AS_NUM(args[1]);
    int len = (int)SLP_AS_NUM(args[2]);
    if (start < 0) start = 0;
    if (start >= (int)str->length) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "", 0));
    if (len < 0) len = 0;
    if (start + len > (int)str->length) len = (int)str->length - start;
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, str->chars + start, (uint32_t)len));
}

static SlpValue builtin_find(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING)
        return SLP_NUM_VAL(-1);
    SlpObjString *haystack = SLP_AS_STRING(args[0]);
    SlpObjString *needle = SLP_AS_STRING(args[1]);
    if (needle->length == 0) return SLP_NUM_VAL(0);
    const char *p = strstr(haystack->chars, needle->chars);
    if (!p) return SLP_NUM_VAL(-1);
    return SLP_NUM_VAL((double)(p - haystack->chars));
}

static SlpValue builtin_indexOf(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 3 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING ||
        !SLP_IS_NUM(args[2]))
        return builtin_find(vm, args, argc);
    SlpObjString *haystack = SLP_AS_STRING(args[0]);
    SlpObjString *needle = SLP_AS_STRING(args[1]);
    int from = (int)SLP_AS_NUM(args[2]);
    if (from < 0) from = 0;
    if (needle->length == 0) return SLP_NUM_VAL((double)from);
    if (from >= (int)haystack->length) return SLP_NUM_VAL(-1);
    const char *p = strstr(haystack->chars + from, needle->chars);
    if (!p) return SLP_NUM_VAL(-1);
    return SLP_NUM_VAL((double)(p - haystack->chars));
}

static SlpValue builtin_formatNumber(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_NUM(args[0]) || !SLP_IS_OBJ(args[1]) ||
        SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    double val = SLP_AS_NUM(args[0]);
    const char *fmt = SLP_AS_STRING(args[1])->chars;
    char buf[128];
    snprintf(buf, sizeof(buf), fmt, val);
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, buf, (uint32_t)strlen(buf)));
}

static SlpValue builtin_cast(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2) return SLP_NULL_VAL;
    if (!SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING)
        return args[0];
    SlpObjString *type = SLP_AS_STRING(args[1]);
    SlpValue val = args[0];
    if (strcmp(type->chars, "int") == 0) {
        if (SLP_IS_NUM(val)) return SLP_NUM_VAL((double)(int)SLP_AS_NUM(val));
        if (SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING)
            return SLP_NUM_VAL((double)atoi(SLP_AS_STRING(val)->chars));
        return SLP_NUM_VAL(0);
    }
    if (strcmp(type->chars, "double") == 0 || strcmp(type->chars, "number") == 0) {
        if (SLP_IS_NUM(val)) return val;
        if (SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING)
            return SLP_NUM_VAL(atof(SLP_AS_STRING(val)->chars));
        return SLP_NUM_VAL(0);
    }
    if (strcmp(type->chars, "string") == 0) {
        if (SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING) return val;
        if (SLP_IS_NUM(val)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", SLP_AS_NUM(val));
            return SLP_OBJ_VAL(slp_vm_copy_string(vm, buf, (uint32_t)strlen(buf)));
        }
        if (SLP_IS_BOOL(val))
            return SLP_OBJ_VAL(slp_vm_copy_string(vm, SLP_AS_BOOL(val) ? "true" : "false", SLP_AS_BOOL(val) ? 4 : 5));
        if (SLP_IS_NULL(val))
            return SLP_OBJ_VAL(slp_vm_copy_string(vm, "$null", 5));
        return SLP_NULL_VAL;
    }
    return val;
}

static SlpValue builtin_sleep_ms(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NULL_VAL;
    double ms = SLP_AS_NUM(args[0]);
    slp_platform_sleep_ms((unsigned int)ms);
    return SLP_NULL_VAL;
}

static SlpValue builtin_srand(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc >= 1 && SLP_IS_NUM(args[0])) {
        srand((unsigned int)SLP_AS_NUM(args[0]));
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_degrees(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(SLP_AS_NUM(args[0]) * 180.0 / 3.14159265358979323846);
}

static SlpValue builtin_radians(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(SLP_AS_NUM(args[0]) * 3.14159265358979323846 / 180.0);
}

static SlpValue builtin_min(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2 || !SLP_IS_NUM(args[0]) || !SLP_IS_NUM(args[1]))
        return SLP_NUM_VAL(0);
    double a = SLP_AS_NUM(args[0]), b = SLP_AS_NUM(args[1]);
    return SLP_NUM_VAL(a < b ? a : b);
}

static SlpValue builtin_max(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2 || !SLP_IS_NUM(args[0]) || !SLP_IS_NUM(args[1]))
        return SLP_NUM_VAL(0);
    double a = SLP_AS_NUM(args[0]), b = SLP_AS_NUM(args[1]);
    return SLP_NUM_VAL(a > b ? a : b);
}

/* -----------------------------------------------------------------------
 * Array Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_add(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0])) return SLP_NULL_VAL;
    if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_ARRAY) {
        slp_obj_array_push(vm->allocator, SLP_AS_ARRAY(args[0]), args[1]);
        return args[0];
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_copy(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0])) return SLP_NULL_VAL;
    if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_ARRAY) {
        SlpObjArray *src = SLP_AS_ARRAY(args[0]);
        SlpObjArray *dst = slp_vm_new_array(vm);
        for (int i = 0; i < src->count; i++)
            slp_obj_array_push(vm->allocator, dst, src->elements[i]);
        return SLP_OBJ_VAL(dst);
    }
    if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_HASH) {
        SlpObjHash *src = SLP_AS_HASH(args[0]);
        SlpObjHash *dst = slp_vm_new_hash(vm);
        for (int i = 0; i < src->capacity; i++) {
            if (!SLP_IS_NULL(src->entries[i].key))
                slp_obj_hash_set(vm->allocator, dst, src->entries[i].key, src->entries[i].value);
        }
        return SLP_OBJ_VAL(dst);
    }
    return args[0];
}

static SlpValue builtin_clear(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0])) return SLP_NULL_VAL;
    if (SLP_OBJ_TYPE(args[0]) == SLP_OBJ_ARRAY) {
        SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
        arr->count = 0;
    }
    return SLP_NULL_VAL;
}

static int cmp_nums_asc(const void *a, const void *b) {
    double da = SLP_AS_NUM(*(const SlpValue*)a);
    double db = SLP_AS_NUM(*(const SlpValue*)b);
    return (da > db) - (da < db);
}

static int cmp_nums_desc(const void *a, const void *b) {
    return -cmp_nums_asc(a, b);
}

static SlpValue builtin_sort(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY)
        return args[0];
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    if (arr->count <= 1) return args[0];
    qsort(arr->elements, (size_t)arr->count, sizeof(SlpValue), cmp_nums_asc);
    return args[0];
}

static SlpValue builtin_sorta(SlpVM *vm, SlpValue *args, int argc) {
    return builtin_sort(vm, args, argc);
}

static SlpValue builtin_sortd(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY)
        return args[0];
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    if (arr->count <= 1) return args[0];
    qsort(arr->elements, (size_t)arr->count, sizeof(SlpValue), cmp_nums_desc);
    return args[0];
}

static SlpValue builtin_sortn(SlpVM *vm, SlpValue *args, int argc) {
    return builtin_sort(vm, args, argc);
}

static SlpValue builtin_reverse(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY)
        return args[0];
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    for (int i = 0; i < arr->count / 2; i++) {
        SlpValue tmp = arr->elements[i];
        arr->elements[i] = arr->elements[arr->count - 1 - i];
        arr->elements[arr->count - 1 - i] = tmp;
    }
    return args[0];
}

static SlpValue builtin_shift(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY)
        return SLP_NULL_VAL;
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    if (arr->count == 0) return SLP_NULL_VAL;
    SlpValue val = arr->elements[0];
    for (int i = 1; i < arr->count; i++)
        arr->elements[i - 1] = arr->elements[i];
    arr->count--;
    return val;
}

static SlpValue builtin_flatten(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY)
        return SLP_NULL_VAL;
    SlpObjArray *src = SLP_AS_ARRAY(args[0]);
    SlpObjArray *dst = slp_vm_new_array(vm);
    for (int i = 0; i < src->count; i++) {
        SlpValue v = src->elements[i];
        if (SLP_IS_OBJ(v) && SLP_OBJ_TYPE(v) == SLP_OBJ_ARRAY) {
            SlpObjArray *inner = SLP_AS_ARRAY(v);
            for (int j = 0; j < inner->count; j++)
                slp_obj_array_push(vm->allocator, dst, inner->elements[j]);
        } else {
            slp_obj_array_push(vm->allocator, dst, v);
        }
    }
    return SLP_OBJ_VAL(dst);
}

static SlpValue builtin_concat(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_ARRAY)
        return args[0];
    SlpObjArray *a = SLP_AS_ARRAY(args[0]);
    SlpObjArray *b = SLP_AS_ARRAY(args[1]);
    for (int i = 0; i < b->count; i++)
        slp_obj_array_push(vm->allocator, a, b->elements[i]);
    return SLP_OBJ_VAL(a);
}

static SlpValue builtin_sum(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY)
        return SLP_NUM_VAL(0);
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    double s = 0;
    for (int i = 0; i < arr->count; i++) {
        if (SLP_IS_NUM(arr->elements[i]))
            s += SLP_AS_NUM(arr->elements[i]);
    }
    return SLP_NUM_VAL(s);
}

static SlpValue builtin_removeAt(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY ||
        !SLP_IS_NUM(args[1]))
        return SLP_NULL_VAL;
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    int idx = (int)SLP_AS_NUM(args[1]);
    if (idx < 0 || idx >= arr->count) return SLP_NULL_VAL;
    SlpValue val = arr->elements[idx];
    for (int i = idx + 1; i < arr->count; i++)
        arr->elements[i - 1] = arr->elements[i];
    arr->count--;
    return val;
}

static SlpValue builtin_contains(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2) return SLP_BOOL_VAL(false);
    SlpValue item = args[0];
    SlpValue container = args[1];
    if (SLP_IS_OBJ(container)) {
        if (SLP_OBJ_TYPE(container) == SLP_OBJ_ARRAY) {
            SlpObjArray *arr = SLP_AS_ARRAY(container);
            for (int i = 0; i < arr->count; i++) {
                if (slp_value_equals(arr->elements[i], item))
                    return SLP_BOOL_VAL(true);
            }
        } else if (SLP_OBJ_TYPE(container) == SLP_OBJ_HASH) {
            return SLP_BOOL_VAL(!SLP_IS_NULL(slp_obj_hash_get(SLP_AS_HASH(container), item)));
        }
    }
    return SLP_BOOL_VAL(false);
}

static SlpValue builtin_values(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_HASH) {
        return SLP_OBJ_VAL(slp_vm_new_array(vm));
    }
    SlpObjHash *hash = (SlpObjHash*)SLP_AS_OBJ(args[0]);
    SlpObjArray *arr = slp_vm_new_array(vm);
    for (int i = 0; i < hash->capacity; i++) {
        if (!SLP_IS_NULL(hash->entries[i].key)) {
            slp_obj_array_push(vm->allocator, arr, hash->entries[i].value);
        }
    }
    return SLP_OBJ_VAL(arr);
}

static SlpValue builtin_sublist(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY ||
                    !SLP_IS_NUM(args[1])) {
        return SLP_OBJ_VAL(slp_vm_new_array(vm));
    }
    SlpObjArray *arr = (SlpObjArray*)SLP_AS_OBJ(args[0]);
    int start = (int)SLP_AS_NUM(args[1]);
    int end = arr->count;
    if (argc >= 3 && SLP_IS_NUM(args[2])) {
        end = (int)SLP_AS_NUM(args[2]);
    }
    
    if (start < 0) start = 0;
    if (end > arr->count) end = arr->count;
    if (start > end) start = end;
    
    SlpObjArray *res = slp_vm_new_array(vm);
    for (int i = start; i < end; i++) {
        slp_obj_array_push(vm->allocator, res, arr->elements[i]);
    }
    return SLP_OBJ_VAL(res);
}

/* -----------------------------------------------------------------------
 * System & Utility Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_print(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc >= 1 && SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
        printf("%s", SLP_AS_STRING(args[0])->chars);
    } else if (argc >= 1 && SLP_IS_NUM(args[0])) {
        printf("%g", SLP_AS_NUM(args[0]));
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_println(SlpVM *vm, SlpValue *args, int argc) {
    builtin_print(vm, args, argc);
    printf("\n");
    fflush(stdout);
    return SLP_NULL_VAL;
}

static SlpValue builtin_ticks(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
    return SLP_NUM_VAL((double)clock());
}

static SlpValue builtin_rand(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    double limit = 1.0;
    if (argc >= 1 && SLP_IS_NUM(args[0])) {
        limit = SLP_AS_NUM(args[0]);
    }
    double r = (double)rand() / (double)RAND_MAX;
    return SLP_NUM_VAL(r * limit);
}

static SlpValue builtin_tstamp(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    /* Convert 100ns units to ms and offset to Unix Epoch */
    double ms = (double)(uli.QuadPart - 116444736000000000ULL) / 10000.0;
    return SLP_NUM_VAL(ms);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    double ms = (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
    return SLP_NUM_VAL(ms);
#endif
}

/* -----------------------------------------------------------------------
 * Utility Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_typeOf(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "null", 4));
    SlpValue v = args[0];
    if (SLP_IS_NULL(v)) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "null", 4));
    if (SLP_IS_BOOL(v)) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "boolean", 7));
    if (SLP_IS_NUM(v)) return SLP_OBJ_VAL(slp_vm_copy_string(vm, "number", 6));
    if (SLP_IS_OBJ(v)) {
        switch (SLP_OBJ_TYPE(v)) {
            case SLP_OBJ_STRING: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "string", 6));
            case SLP_OBJ_LONG: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "long", 4));
            case SLP_OBJ_ARRAY: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "array", 5));
            case SLP_OBJ_HASH: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "hash", 4));
            case SLP_OBJ_FUNCTION:
            case SLP_OBJ_CLOSURE:
            case SLP_OBJ_NATIVE:
            case SLP_OBJ_BRIDGE:
                return SLP_OBJ_VAL(slp_vm_copy_string(vm, "function", 8));
            default: return SLP_OBJ_VAL(slp_vm_copy_string(vm, "object", 6));
        }
    }
    return SLP_OBJ_VAL(slp_vm_copy_string(vm, "unknown", 7));
}

static SlpValue builtin_global(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING)
        return SLP_NULL_VAL;
    if (argc >= 2) {
        slp_obj_hash_set(vm->allocator, vm->globals, args[0], args[1]);
        return args[1];
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_byteAt(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING ||
        !SLP_IS_NUM(args[1]))
        return SLP_NUM_VAL(0);
    SlpObjString *str = SLP_AS_STRING(args[0]);
    int idx = (int)SLP_AS_NUM(args[1]);
    if (idx < 0 || idx >= (int)str->length) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL((double)(unsigned char)str->chars[idx]);
}

/* -----------------------------------------------------------------------
 * Regex & Capture Subgroups
 * ----------------------------------------------------------------------- */

static SlpValue builtin_matched(SlpVM *vm, SlpValue *args, int argc) {
    (void)args; (void)argc;
    if (vm->last_regex_matches) {
        return SLP_OBJ_VAL(vm->last_regex_matches);
    }
    SlpObjArray *empty_arr = slp_vm_new_array(vm);
    return SLP_OBJ_VAL(empty_arr);
}

/* -----------------------------------------------------------------------
 * Binary Packing & Unpacking Helper Functions & Structs
 * ----------------------------------------------------------------------- */

static bool is_host_big_endian(void) {
    union {
        uint32_t i;
        char c[4];
    } bint = {0x01020304};
    return bint.c[0] == 1;
}

typedef struct {
    char *data;
    size_t cap;
    size_t len;
} PackBuffer;

static void buf_push_byte(SlpAllocator *alloc, PackBuffer *buf, uint8_t b) {
    if (buf->len >= buf->cap) {
        size_t new_cap = buf->cap == 0 ? 128 : buf->cap * 2;
        buf->data = (char*)alloc->reallocate(buf->data, new_cap, alloc->user_data);
        buf->cap = new_cap;
    }
    buf->data[buf->len++] = (char)b;
}

static void buf_push_bytes(SlpAllocator *alloc, PackBuffer *buf, const uint8_t *bytes, size_t size) {
    for (size_t i = 0; i < size; i++) {
        buf_push_byte(alloc, buf, bytes[i]);
    }
}

static void write_short(SlpAllocator *alloc, PackBuffer *buf, uint16_t val, bool big_endian) {
    uint8_t bytes[2];
    if (big_endian) {
        bytes[0] = (uint8_t)((val >> 8) & 0xFF);
        bytes[1] = (uint8_t)(val & 0xFF);
    } else {
        bytes[0] = (uint8_t)(val & 0xFF);
        bytes[1] = (uint8_t)((val >> 8) & 0xFF);
    }
    buf_push_bytes(alloc, buf, bytes, 2);
}

static void write_int(SlpAllocator *alloc, PackBuffer *buf, uint32_t val, bool big_endian) {
    uint8_t bytes[4];
    if (big_endian) {
        bytes[0] = (uint8_t)((val >> 24) & 0xFF);
        bytes[1] = (uint8_t)((val >> 16) & 0xFF);
        bytes[2] = (uint8_t)((val >> 8) & 0xFF);
        bytes[3] = (uint8_t)(val & 0xFF);
    } else {
        bytes[0] = (uint8_t)(val & 0xFF);
        bytes[1] = (uint8_t)((val >> 8) & 0xFF);
        bytes[2] = (uint8_t)((val >> 16) & 0xFF);
        bytes[3] = (uint8_t)((val >> 24) & 0xFF);
    }
    buf_push_bytes(alloc, buf, bytes, 4);
}

static void write_long(SlpAllocator *alloc, PackBuffer *buf, uint64_t val, bool big_endian) {
    uint8_t bytes[8];
    if (big_endian) {
        bytes[0] = (uint8_t)((val >> 56) & 0xFF);
        bytes[1] = (uint8_t)((val >> 48) & 0xFF);
        bytes[2] = (uint8_t)((val >> 40) & 0xFF);
        bytes[3] = (uint8_t)((val >> 32) & 0xFF);
        bytes[4] = (uint8_t)((val >> 24) & 0xFF);
        bytes[5] = (uint8_t)((val >> 16) & 0xFF);
        bytes[6] = (uint8_t)((val >> 8) & 0xFF);
        bytes[7] = (uint8_t)(val & 0xFF);
    } else {
        bytes[0] = (uint8_t)(val & 0xFF);
        bytes[1] = (uint8_t)((val >> 8) & 0xFF);
        bytes[2] = (uint8_t)((val >> 16) & 0xFF);
        bytes[3] = (uint8_t)((val >> 24) & 0xFF);
        bytes[4] = (uint8_t)((val >> 32) & 0xFF);
        bytes[5] = (uint8_t)((val >> 40) & 0xFF);
        bytes[6] = (uint8_t)((val >> 48) & 0xFF);
        bytes[7] = (uint8_t)((val >> 56) & 0xFF);
    }
    buf_push_bytes(alloc, buf, bytes, 8);
}

static void write_float(SlpAllocator *alloc, PackBuffer *buf, float val, bool big_endian) {
    union {
        float f;
        uint32_t i;
    } u;
    u.f = val;
    write_int(alloc, buf, u.i, big_endian);
}

static void write_double(SlpAllocator *alloc, PackBuffer *buf, double val, bool big_endian) {
    union {
        double d;
        uint64_t i;
    } u;
    u.d = val;
    write_long(alloc, buf, u.i, big_endian);
}

typedef struct {
    SlpValue *args;
    int argc;
    int arg_idx;
    SlpObjArray *array_arg;
    int array_idx;
} PackArgsState;

static SlpValue get_next_arg(PackArgsState *state) {
    if (state->array_arg) {
        if (state->array_idx < state->array_arg->count) {
            return state->array_arg->elements[state->array_idx++];
        }
        return SLP_NULL_VAL;
    }
    if (state->arg_idx < state->argc) {
        return state->args[state->arg_idx++];
    }
    return SLP_NULL_VAL;
}

static uint16_t read_short_bytes(const uint8_t *bytes, bool big_endian) {
    if (big_endian) {
        return (uint16_t)((bytes[0] << 8) | bytes[1]);
    } else {
        return (uint16_t)(bytes[0] | (bytes[1] << 8));
    }
}

static uint32_t read_int_bytes(const uint8_t *bytes, bool big_endian) {
    if (big_endian) {
        return ((uint32_t)bytes[0] << 24) |
               ((uint32_t)bytes[1] << 16) |
               ((uint32_t)bytes[2] << 8)  |
               (uint32_t)bytes[3];
    } else {
        return (uint32_t)bytes[0] |
               ((uint32_t)bytes[1] << 8)  |
               ((uint32_t)bytes[2] << 16) |
               ((uint32_t)bytes[3] << 24);
    }
}

static uint64_t read_long_bytes(const uint8_t *bytes, bool big_endian) {
    if (big_endian) {
        return ((uint64_t)bytes[0] << 56) |
               ((uint64_t)bytes[1] << 48) |
               ((uint64_t)bytes[2] << 40) |
               ((uint64_t)bytes[3] << 32) |
               ((uint64_t)bytes[4] << 24) |
               ((uint64_t)bytes[5] << 16) |
               ((uint64_t)bytes[6] << 8)  |
               (uint64_t)bytes[7];
    } else {
        return (uint64_t)bytes[0] |
               ((uint64_t)bytes[1] << 8)  |
               ((uint64_t)bytes[2] << 16) |
               ((uint64_t)bytes[3] << 24) |
               ((uint64_t)bytes[4] << 32) |
               ((uint64_t)bytes[5] << 40) |
               ((uint64_t)bytes[6] << 48) |
               ((uint64_t)bytes[7] << 56);
    }
}

static float read_float_bytes(const uint8_t *bytes, bool big_endian) {
    union {
        uint32_t i;
        float f;
    } u;
    u.i = read_int_bytes(bytes, big_endian);
    return u.f;
}

static double read_double_bytes_from_u(uint64_t val) {
    union {
        uint64_t i;
        double d;
    } u;
    u.i = val;
    return u.d;
}

static double read_double_bytes(const uint8_t *bytes, bool big_endian) {
    union {
        uint64_t i;
        double d;
    } u;
    u.d = read_double_bytes_from_u(read_long_bytes(bytes, big_endian));
    return u.d;
}

/* -----------------------------------------------------------------------
 * Pack & Unpack Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_pack(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    const char *format = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    
    PackArgsState state;
    state.args = args;
    state.argc = argc;
    state.arg_idx = 1;
    state.array_arg = NULL;
    state.array_idx = 0;
    if (argc == 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_ARRAY) {
        state.array_arg = SLP_AS_ARRAY(args[1]);
    }

    PackBuffer buf;
    buf.data = NULL;
    buf.cap = 0;
    buf.len = 0;

    bool big_endian = true;
    bool host_be = is_host_big_endian();

    int len = (int)strlen(format);
    int x = 0;
    while (x < len) {
        char c = format[x];
        if (c == '+') {
            big_endian = true;
            x++;
            continue;
        } else if (c == '-') {
            big_endian = false;
            x++;
            continue;
        } else if (c == '!') {
            big_endian = host_be;
            x++;
            continue;
        }

        if (isalpha((unsigned char)c)) {
            x++;
            int count = 1;
            if (x < len && format[x] == '*') {
                count = -1;
                x++;
            } else if (x < len && isdigit((unsigned char)format[x])) {
                count = 0;
                while (x < len && isdigit((unsigned char)format[x])) {
                    count = count * 10 + (format[x] - '0');
                    x++;
                }
            }

            if (c == 'z' || c == 'Z') {
                SlpValue val = get_next_arg(&state);
                const char *str = "";
                int str_len = 0;
                if (SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING) {
                    str = SLP_AS_STRING(val)->chars;
                    str_len = (int)SLP_AS_STRING(val)->length;
                }
                
                int limit = count;
                if (count == -1) {
                    limit = str_len;
                }
                
                for (int i = 0; i < limit; i++) {
                    if (i < str_len) {
                        buf_push_byte(vm->allocator, &buf, (uint8_t)str[i]);
                    } else {
                        buf_push_byte(vm->allocator, &buf, 0);
                    }
                }
                
                if (c == 'z' || (c == 'Z' && count == -1)) {
                    buf_push_byte(vm->allocator, &buf, 0);
                }
            } else if (c == 'h' || c == 'H') {
                SlpValue val = get_next_arg(&state);
                const char *hex = "";
                int hex_len = 0;
                if (SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING) {
                    hex = SLP_AS_STRING(val)->chars;
                    hex_len = (int)SLP_AS_STRING(val)->length;
                }
                
                for (int i = 0; i + 1 < hex_len; i += 2) {
                    char h1 = hex[i];
                    char h2 = hex[i+1];
                    if (c == 'h') {
                        h1 = hex[i+1];
                        h2 = hex[i];
                    }
                    char byte_str[3] = {h1, h2, '\0'};
                    uint8_t byte_val = (uint8_t)strtol(byte_str, NULL, 16);
                    buf_push_byte(vm->allocator, &buf, byte_val);
                }
            } else {
                int repeat = count;
                if (count == -1) {
                    repeat = state.array_arg ? (state.array_arg->count - state.array_idx) : (state.argc - state.arg_idx);
                }
                
                for (int r = 0; r < repeat; r++) {
                    SlpValue val = SLP_NULL_VAL;
                    if (c != 'x') {
                        val = get_next_arg(&state);
                    }
                    
                    switch (c) {
                        case 'x':
                            buf_push_byte(vm->allocator, &buf, 0);
                            break;
                        case 'C': {
                            const char *str = SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING ? SLP_AS_STRING(val)->chars : "";
                            buf_push_byte(vm->allocator, &buf, (uint8_t)str[0]);
                            break;
                        }
                        case 'c': {
                            const char *str = SLP_IS_OBJ(val) && SLP_OBJ_TYPE(val) == SLP_OBJ_STRING ? SLP_AS_STRING(val)->chars : "";
                            write_short(vm->allocator, &buf, (uint16_t)str[0], big_endian);
                            break;
                        }
                        case 'b':
                        case 'B':
                            buf_push_byte(vm->allocator, &buf, (uint8_t)SLP_AS_NUM(val));
                            break;
                        case 's':
                        case 'S':
                            write_short(vm->allocator, &buf, (uint16_t)SLP_AS_NUM(val), big_endian);
                            break;
                        case 'i':
                        case 'I':
                            write_int(vm->allocator, &buf, (uint32_t)SLP_AS_NUM(val), big_endian);
                            break;
                        case 'f':
                            write_float(vm->allocator, &buf, (float)SLP_AS_NUM(val), big_endian);
                            break;
                        case 'd':
                            write_double(vm->allocator, &buf, SLP_AS_NUM(val), big_endian);
                            break;
                        case 'l':
                            write_long(vm->allocator, &buf, (uint64_t)SLP_AS_NUM(val), big_endian);
                            break;
                        default:
                            break;
                    }
                }
            }
        } else {
            x++;
        }
    }

    SlpObjString *res = slp_vm_intern_string(vm, buf.data ? buf.data : "", (uint32_t)buf.len);
    if (buf.data) vm->allocator->reallocate(buf.data, 0, vm->allocator->user_data);
    return SLP_OBJ_VAL(res);
}

static SlpValue builtin_unpack(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2) return SLP_NULL_VAL;
    const char *format = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    
    if (!SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING) {
        return SLP_NULL_VAL;
    }
    const uint8_t *data = (const uint8_t *)SLP_AS_STRING(args[1])->chars;
    int data_len = (int)SLP_AS_STRING(args[1])->length;
    int offset = 0;

    SlpObjArray *arr = slp_vm_new_array(vm);
    if (!arr) return SLP_NULL_VAL;

    bool big_endian = true;
    bool host_be = is_host_big_endian();

    int len = (int)strlen(format);
    int x = 0;
    while (x < len && offset < data_len) {
        char c = format[x];
        if (c == '+') {
            big_endian = true;
            x++;
            continue;
        } else if (c == '-') {
            big_endian = false;
            x++;
            continue;
        } else if (c == '!') {
            big_endian = host_be;
            x++;
            continue;
        }

        if (isalpha((unsigned char)c)) {
            x++;
            int count = 1;
            if (x < len && format[x] == '*') {
                count = -1;
                x++;
            } else if (x < len && isdigit((unsigned char)format[x])) {
                count = 0;
                while (x < len && isdigit((unsigned char)format[x])) {
                    count = count * 10 + (format[x] - '0');
                    x++;
                }
            }

            if (c == 'z' || c == 'Z') {
                int start = offset;
                if (count == -1) {
                    while (offset < data_len && data[offset] != '\0') {
                        offset++;
                    }
                    SlpObjString *str = slp_vm_copy_string(vm, (const char *)(data + start), (uint32_t)(offset - start));
                    slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(str));
                    if (offset < data_len && data[offset] == '\0') {
                        offset++;
                    }
                } else {
                    int limit = count;
                    int actual_len = 0;
                    while (actual_len < limit && (offset + actual_len) < data_len && data[offset + actual_len] != '\0') {
                        actual_len++;
                    }
                    SlpObjString *str = slp_vm_copy_string(vm, (const char *)(data + offset), (uint32_t)actual_len);
                    slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(str));
                    offset += limit;
                }
            } else if (c == 'h' || c == 'H') {
                int limit = count;
                if (count == -1) {
                    limit = data_len - offset;
                }
                char *hex_buf = (char*)vm->allocator->reallocate(NULL, (size_t)limit * 2 + 1, vm->allocator->user_data);
                int hex_idx = 0;
                for (int i = 0; i < limit && offset < data_len; i++) {
                    uint8_t val = data[offset++];
                    uint8_t early = (uint8_t)((val & 0xF0) >> 4);
                    uint8_t later = (uint8_t)(val & 0x0F);
                    if (c == 'h') {
                        snprintf(hex_buf + hex_idx, 3, "%x%x", later, early);
                    } else {
                        snprintf(hex_buf + hex_idx, 3, "%x%x", early, later);
                    }
                    hex_idx += 2;
                }
                hex_buf[hex_idx] = '\0';
                SlpObjString *str = slp_vm_copy_string(vm, hex_buf, (uint32_t)hex_idx);
                vm->allocator->reallocate(hex_buf, 0, vm->allocator->user_data);
                slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(str));
            } else {
                int repeat = count;
                if (count == -1) {
                    int unit_size = 1;
                    switch (c) {
                        case 'c': case 's': case 'S': unit_size = 2; break;
                        case 'i': case 'I': case 'f': unit_size = 4; break;
                        case 'd': case 'l': unit_size = 8; break;
                        default: unit_size = 1; break;
                    }
                    repeat = (data_len - offset) / unit_size;
                }

                for (int r = 0; r < repeat && offset < data_len; r++) {
                    switch (c) {
                        case 'x':
                            offset++;
                            break;
                        case 'C': {
                            char str[2] = {(char)data[offset++], '\0'};
                            SlpObjString *ch = slp_vm_copy_string(vm, str, 1);
                            slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(ch));
                            break;
                        }
                        case 'c': {
                            if (offset + 2 <= data_len) {
                                uint16_t val = read_short_bytes(data + offset, big_endian);
                                char str[2] = {(char)val, '\0'};
                                SlpObjString *ch = slp_vm_copy_string(vm, str, 1);
                                slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(ch));
                                offset += 2;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'b': {
                            int8_t val = (int8_t)data[offset++];
                            slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL((double)val));
                            break;
                        }
                        case 'B': {
                            uint8_t val = data[offset++];
                            slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL((double)val));
                            break;
                        }
                        case 's': {
                            if (offset + 2 <= data_len) {
                                int16_t val = (int16_t)read_short_bytes(data + offset, big_endian);
                                slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL((double)val));
                                offset += 2;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'S': {
                            if (offset + 2 <= data_len) {
                                uint16_t val = read_short_bytes(data + offset, big_endian);
                                slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL((double)val));
                                offset += 2;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'i': {
                            if (offset + 4 <= data_len) {
                                int32_t val = (int32_t)read_int_bytes(data + offset, big_endian);
                                slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL((double)val));
                                offset += 4;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'I': {
                            if (offset + 4 <= data_len) {
                                uint32_t val = read_int_bytes(data + offset, big_endian);
                                slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL((double)val));
                                offset += 4;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'f': {
                            if (offset + 4 <= data_len) {
                                float val = read_float_bytes(data + offset, big_endian);
                                slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL((double)val));
                                offset += 4;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'd': {
                            if (offset + 8 <= data_len) {
                                double val = read_double_bytes(data + offset, big_endian);
                                slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL(val));
                                offset += 8;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        case 'l': {
                            if (offset + 8 <= data_len) {
                                int64_t val = (int64_t)read_long_bytes(data + offset, big_endian);
                                slp_obj_array_push(vm->allocator, arr, SLP_NUM_VAL((double)val));
                                offset += 8;
                            } else {
                                offset = data_len;
                            }
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        } else {
            x++;
        }
    }

    return SLP_OBJ_VAL(arr);
}

/* -----------------------------------------------------------------------
 * Filesystem Bridge Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_ls(SlpVM *vm, SlpValue *args, int argc) {
    SlpObjArray *arr = slp_vm_new_array(vm);
    if (!arr) return SLP_NULL_VAL;
    const char *dir_path = ".";
    if (argc >= 1 && SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
        dir_path = SLP_AS_STRING(args[0])->chars;
    }

    SlpPlatformDir *d = slp_platform_opendir(dir_path);
    if (d) {
        const char *name;
        char sep = slp_platform_path_separator();
        while ((name = slp_platform_readdir(d)) != NULL) {
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s%c%s", dir_path, sep, name);
                SlpObjString *s = slp_vm_copy_cstr(vm, full_path);
                slp_obj_array_push(vm->allocator, arr, SLP_OBJ_VAL(s));
            }
        }
        slp_platform_closedir(d);
    }
    return SLP_OBJ_VAL(arr);
}

static SlpValue builtin_mkdir(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_BOOL_VAL(false);
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    int res = slp_platform_mkdir(path);
    return SLP_BOOL_VAL(res == 0);
}

static SlpValue builtin_deleteFile(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_BOOL_VAL(false);
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    int res = remove(path);
    return SLP_BOOL_VAL(res == 0);
}

static SlpValue builtin_rename(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2) return SLP_BOOL_VAL(false);
    const char *old_path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    const char *new_path = SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[1])->chars : "";
    int res = rename(old_path, new_path);
    return SLP_BOOL_VAL(res == 0);
}

static SlpValue builtin_lof(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    return SLP_NUM_VAL(slp_platform_file_size(path));
}

static SlpValue builtin_lastModified(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    return SLP_NUM_VAL(slp_platform_last_modified(path));
}

static SlpValue builtin_cwd(SlpVM *vm, SlpValue *args, int argc) {
    (void)args; (void)argc;
    char path[1024];
    if (!slp_platform_getcwd(path, sizeof(path))) {
        path[0] = '\0';
    }
    SlpObjString *s = slp_vm_copy_cstr(vm, path);
    return SLP_OBJ_VAL(s);
}

static SlpValue builtin_chdir(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_BOOL_VAL(false);
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    int res = slp_platform_chdir(path);
    return SLP_BOOL_VAL(res == 0);
}

static SlpValue builtin_getFileName(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    const char *filename = strrchr(path, '/');
    if (slp_platform_path_separator() == '\\') {
        const char *filename_win = strrchr(path, '\\');
        if (filename_win && (!filename || filename_win > filename)) {
            filename = filename_win;
        }
    }
    if (filename) {
        filename++;
    } else {
        filename = path;
    }
    SlpObjString *s = slp_vm_copy_cstr(vm, filename);
    return SLP_OBJ_VAL(s);
}

static SlpValue builtin_getFileParent(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    const char *last_sep = strrchr(path, '/');
    if (slp_platform_path_separator() == '\\') {
        const char *last_sep_win = strrchr(path, '\\');
        if (last_sep_win && (!last_sep || last_sep_win > last_sep)) {
            last_sep = last_sep_win;
        }
    }
    if (last_sep && last_sep != path) {
        size_t len = (size_t)(last_sep - path);
        SlpObjString *s = slp_vm_copy_string(vm, path, (uint32_t)len);
        return SLP_OBJ_VAL(s);
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_getFileProper(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    if (!(SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING)) {
        return SLP_NULL_VAL;
    }
    const char *start = SLP_AS_STRING(args[0])->chars;
    char resolved[1024];
    strncpy(resolved, start, sizeof(resolved) - 1);
    resolved[sizeof(resolved) - 1] = '\0';

    char sep = (char)slp_platform_path_separator();

    for (int i = 1; i < argc; i++) {
        if (SLP_IS_OBJ(args[i]) && SLP_OBJ_TYPE(args[i]) == SLP_OBJ_STRING) {
            const char *child = SLP_AS_STRING(args[i])->chars;
            size_t len = strlen(resolved);
            if (len > 0 && resolved[len - 1] != '/' && resolved[len - 1] != '\\') {
                size_t child_len = strlen(child);
                if (len + 1 + child_len < sizeof(resolved)) {
                    resolved[len] = sep;
                    strncpy(resolved + len + 1, child, sizeof(resolved) - len - 1);
                }
            } else {
                size_t child_len = strlen(child);
                if (len + child_len < sizeof(resolved)) {
                    strncpy(resolved + len, child, sizeof(resolved) - len);
                }
            }
        }
    }
    return SLP_OBJ_VAL(slp_vm_copy_cstr(vm, resolved));
}

/* -----------------------------------------------------------------------
 * Date & Time Bridge Builtins
 * ----------------------------------------------------------------------- */

static void map_java_pattern_to_c(const char *java_pat, char *c_pat, size_t c_pat_max) {
    size_t i = 0;
    size_t j = 0;
    size_t len = strlen(java_pat);
    while (i < len && j + 2 < c_pat_max) {
        if (strncmp(java_pat + i, "yyyy", 4) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'Y'; i += 4;
        } else if (strncmp(java_pat + i, "yy", 2) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'y'; i += 2;
        } else if (strncmp(java_pat + i, "MM", 2) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'm'; i += 2;
        } else if (strncmp(java_pat + i, "dd", 2) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'd'; i += 2;
        } else if (strncmp(java_pat + i, "HH", 2) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'H'; i += 2;
        } else if (strncmp(java_pat + i, "hh", 2) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'I'; i += 2;
        } else if (strncmp(java_pat + i, "mm", 2) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'M'; i += 2;
        } else if (strncmp(java_pat + i, "ss", 2) == 0) {
            c_pat[j++] = '%'; c_pat[j++] = 'S'; i += 2;
        } else if (java_pat[i] == 'a') {
            c_pat[j++] = '%'; c_pat[j++] = 'p'; i++;
        } else if (java_pat[i] == 'z') {
            c_pat[j++] = '%'; c_pat[j++] = 'Z'; i++;
        } else if (java_pat[i] == 'Z') {
            c_pat[j++] = '%'; c_pat[j++] = 'z'; i++;
        } else {
            c_pat[j++] = java_pat[i++];
        }
    }
    c_pat[j] = '\0';
}

static time_t parse_date_custom(const char *date_str, const char *java_pattern) {
    struct tm tm_val;
    memset(&tm_val, 0, sizeof(struct tm));
    tm_val.tm_mday = 1;
    tm_val.tm_year = 70;

    char c_pattern[128];
    map_java_pattern_to_c(java_pattern, c_pattern, sizeof(c_pattern));
    char *res = slp_platform_strptime(date_str, c_pattern, &tm_val);
    if (res) {
        return mktime(&tm_val);
    }

    int year = 1970, month = 1, day = 1, hour = 0, min = 0, sec = 0;
    if (sscanf(date_str, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) >= 3 ||
        sscanf(date_str, "%d/%d/%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) >= 3) {
        tm_val.tm_year = year - 1900;
        tm_val.tm_mon = month - 1;
        tm_val.tm_mday = day;
        tm_val.tm_hour = hour;
        tm_val.tm_min = min;
        tm_val.tm_sec = sec;
        return mktime(&tm_val);
    }
    return 0;
}

static SlpValue builtin_formatDate(SlpVM *vm, SlpValue *args, int argc) {
    long long epoch_ms = 0;
    const char *java_pattern = "";
    
    if (argc == 1) {
#ifdef _WIN32
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER uli;
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        epoch_ms = (long long)((uli.QuadPart - 116444736000000000ULL) / 10000ULL);
#else
        struct timeval tv;
        gettimeofday(&tv, NULL);
        epoch_ms = (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
        if (SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
            java_pattern = SLP_AS_STRING(args[0])->chars;
        }
    } else if (argc >= 2) {
        epoch_ms = (long long)SLP_AS_NUM(args[0]);
        if (SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING) {
            java_pattern = SLP_AS_STRING(args[1])->chars;
        }
    } else {
        return SLP_NULL_VAL;
    }

    time_t raw_time = (time_t)(epoch_ms / 1000);
    struct tm *timeinfo = localtime(&raw_time);
    
    char c_pattern[128];
    map_java_pattern_to_c(java_pattern, c_pattern, sizeof(c_pattern));

    char out_buf[256];
    size_t written = strftime(out_buf, sizeof(out_buf), c_pattern, timeinfo);
    
    SlpObjString *s = slp_vm_copy_string(vm, out_buf, (uint32_t)written);
    return SLP_OBJ_VAL(s);
}

static SlpValue builtin_parseDate(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2) return SLP_NUM_VAL(0);
    const char *java_pattern = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    const char *date_str = SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[1])->chars : "";
    
    time_t t = parse_date_custom(date_str, java_pattern);
    return SLP_NUM_VAL((double)t * 1000.0);
}

/* -----------------------------------------------------------------------
 * Standalone Cryptographic Helpers (CRC32, MD5, SHA-256)
 * ----------------------------------------------------------------------- */

static uint32_t calculate_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
    }
    return ~crc;
}

#define S11 7
#define S12 12
#define S13 17
#define S14 22
#define S21 5
#define S22 9
#define S23 14
#define S24 20
#define S31 4
#define S32 11
#define S33 16
#define S34 23
#define S41 6
#define S42 10
#define S43 15
#define S44 21

typedef struct {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t buffer[64];
} MD5_CTX;

static uint8_t MD5_PADDING[64] = {
  0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#define MD5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~z)))

#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))

#define FF(a, b, c, d, x, s, ac) { \
 (a) += MD5_F ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
}
#define GG(a, b, c, d, x, s, ac) { \
 (a) += MD5_G ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
}
#define HH(a, b, c, d, x, s, ac) { \
 (a) += MD5_H ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
}
#define II(a, b, c, d, x, s, ac) { \
 (a) += MD5_I ((b), (c), (d)) + (x) + (uint32_t)(ac); \
 (a) = ROTATE_LEFT ((a), (s)); \
 (a) += (b); \
}

static void MD5_Encode(uint8_t *output, const uint32_t *input, uint32_t len) {
    uint32_t i, j;
    for (i = 0, j = 0; j < len; i++, j += 4) {
        output[j] = (uint8_t)(input[i] & 0xFF);
        output[j+1] = (uint8_t)((input[i] >> 8) & 0xFF);
        output[j+2] = (uint8_t)((input[i] >> 16) & 0xFF);
        output[j+3] = (uint8_t)((input[i] >> 24) & 0xFF);
    }
}

static void MD5_Decode(uint32_t *output, const uint8_t *input, uint32_t len) {
    uint32_t i, j;
    for (i = 0, j = 0; j < len; i++, j += 4)
        output[i] = ((uint32_t)input[j]) | (((uint32_t)input[j+1]) << 8) |
                    (((uint32_t)input[j+2]) << 16) | (((uint32_t)input[j+3]) << 24);
}

static void MD5_Transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
    MD5_Decode(x, block, 64);
    /* Round 1 */
    FF(a, b, c, d, x[ 0], S11, 0xd76aa478);
    FF(d, a, b, c, x[ 1], S12, 0xe8c7b756);
    FF(c, d, a, b, x[ 2], S13, 0x242070db);
    FF(b, c, d, a, x[ 3], S14, 0xc1bdceee);
    FF(a, b, c, d, x[ 4], S11, 0xf57c0faf);
    FF(d, a, b, c, x[ 5], S12, 0x4787c62a);
    FF(c, d, a, b, x[ 6], S13, 0xa8304613);
    FF(b, c, d, a, x[ 7], S14, 0xfd469501);
    FF(a, b, c, d, x[ 8], S11, 0x698098d8);
    FF(d, a, b, c, x[ 9], S12, 0x8b44f7af);
    FF(c, d, a, b, x[10], S13, 0xffff5bb1);
    FF(b, c, d, a, x[11], S14, 0x895cd7be);
    FF(a, b, c, d, x[12], S11, 0x6b901122);
    FF(d, a, b, c, x[13], S12, 0xfd987193);
    FF(c, d, a, b, x[14], S13, 0xa679438e);
    FF(b, c, d, a, x[15], S14, 0x49b40821);
    /* Round 2 */
    GG(a, b, c, d, x[ 1], S21, 0xf61e2562);
    GG(d, a, b, c, x[ 6], S22, 0xc040b340);
    GG(c, d, a, b, x[11], S23, 0x265e5a51);
    GG(b, c, d, a, x[ 0], S24, 0xe9b6c7aa);
    GG(a, b, c, d, x[ 5], S21, 0xd62f105d);
    GG(d, a, b, c, x[10], S22,  0x2441453);
    GG(c, d, a, b, x[15], S23, 0xd8a1e681);
    GG(b, c, d, a, x[ 4], S24, 0xe7d3fbc8);
    GG(a, b, c, d, x[ 9], S21, 0x21e1cde6);
    GG(d, a, b, c, x[14], S22, 0xc33707d6);
    GG(c, d, a, b, x[ 3], S23, 0xf4d50d87);
    GG(b, c, d, a, x[ 8], S24, 0x455a14ed);
    GG(a, b, c, d, x[13], S21, 0xa9e3e905);
    GG(d, a, b, c, x[ 2], S22, 0xfcefa3f8);
    GG(c, d, a, b, x[ 7], S23, 0x676f02d9);
    GG(b, c, d, a, x[12], S24, 0x8d2a4c8a);
    /* Round 3 */
    HH(a, b, c, d, x[ 5], S31, 0xfffa3942);
    HH(d, a, b, c, x[ 8], S32, 0x8771f681);
    HH(c, d, a, b, x[11], S33, 0x6d9d6122);
    HH(b, c, d, a, x[14], S34, 0xfde5380c);
    HH(a, b, c, d, x[ 1], S31, 0xa4beea44);
    HH(d, a, b, c, x[ 4], S32, 0x4bdecfa9);
    HH(c, d, a, b, x[ 7], S33, 0xf6bb4b60);
    HH(b, c, d, a, x[10], S34, 0xbebfbc70);
    HH(a, b, c, d, x[13], S31, 0x289b7ec6);
    HH(d, a, b, c, x[ 0], S32, 0xeaa127fa);
    HH(c, d, a, b, x[ 3], S33, 0xd4ef3085);
    HH(b, c, d, a, x[ 6], S34,  0x4881d05);
    HH(a, b, c, d, x[ 9], S31, 0xd9d4d039);
    HH(d, a, b, c, x[12], S32, 0xe6db99e5);
    HH(c, d, a, b, x[15], S33, 0x1fa27cf8);
    HH(b, c, d, a, x[ 2], S34, 0xc4ac5665);
    /* Round 4 */
    II(a, b, c, d, x[ 0], S41, 0xf4292244);
    II(d, a, b, c, x[ 7], S42, 0x432aff97);
    II(c, d, a, b, x[14], S43, 0xab9423a7);
    II(b, c, d, a, x[ 5], S44, 0xfc93a039);
    II(a, b, c, d, x[12], S41, 0x655b59c3);
    II(d, a, b, c, x[ 3], S42, 0x8f0ccc92);
    II(c, d, a, b, x[10], S43, 0xffeff47d);
    II(b, c, d, a, x[ 1], S44, 0x85845dd1);
    II(a, b, c, d, x[ 8], S41, 0x6fa87e4f);
    II(d, a, b, c, x[15], S42, 0xfe2ce6e0);
    II(c, d, a, b, x[ 6], S43, 0xa3014314);
    II(b, c, d, a, x[13], S44, 0x4e0811a1);
    II(a, b, c, d, x[ 4], S41, 0xf7537e82);
    II(d, a, b, c, x[11], S42, 0xbd3af235);
    II(c, d, a, b, x[ 2], S43, 0x2ad7d2bb);
    II(b, c, d, a, x[ 9], S44, 0xeb86d391);
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void MD5_Init(MD5_CTX *context) {
    context->count[0] = context->count[1] = 0;
    context->state[0] = 0x67452301;
    context->state[1] = 0xefcdab89;
    context->state[2] = 0x98badcfe;
    context->state[3] = 0x10325476;
}

static void MD5_Update(MD5_CTX *context, const uint8_t *input, uint32_t inputLen) {
    uint32_t i, idx, partLen;
    idx = (context->count[0] >> 3) & 0x3F;
    if ((context->count[0] += (inputLen << 3)) < (inputLen << 3))
        context->count[1]++;
    context->count[1] += (inputLen >> 29);
    partLen = 64 - idx;
    if (inputLen >= partLen) {
        memcpy(&context->buffer[idx], input, partLen);
        MD5_Transform(context->state, context->buffer);
        for (i = partLen; i + 63 < inputLen; i += 64)
            MD5_Transform(context->state, &input[i]);
        idx = 0;
    } else {
        i = 0;
    }
    memcpy(&context->buffer[idx], &input[i], inputLen - i);
}

static void MD5_Final(uint8_t digest[16], MD5_CTX *context) {
    uint8_t bits[8];
    uint32_t idx, padLen;
    MD5_Encode(bits, context->count, 8);
    idx = (context->count[0] >> 3) & 0x3F;
    padLen = (idx < 56) ? (56 - idx) : (120 - idx);
    MD5_Update(context, MD5_PADDING, padLen);
    MD5_Update(context, bits, 8);
    MD5_Encode(digest, context->state, 16);
    memset(context, 0, sizeof(*context));
}

// SHA-256 implementation
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} SHA256_CTX;

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32-(n))))
#define Ch(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define Sigma0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define Sigma1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define sigma0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define sigma1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static uint32_t SHA_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void SHA256_Transform(uint32_t state[8], const uint8_t buffer[64]) {
    uint32_t w[64], a, b, c, d, e, f, g, h, i, t1, t2;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)buffer[i*4] << 24) |
               ((uint32_t)buffer[i*4+1] << 16) |
               ((uint32_t)buffer[i*4+2] << 8) |
               (uint32_t)buffer[i*4+3];
    }
    for (i = 16; i < 64; i++) {
        w[i] = sigma1(w[i-2]) + w[i-7] + sigma0(w[i-15]) + w[i-16];
    }
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];
    for (i = 0; i < 64; i++) {
        t1 = h + Sigma1(e) + Ch(e, f, g) + SHA_K[i] + w[i];
        t2 = Sigma0(a) + Maj(a, b, c);
        h = g; g = f; f = e;
        e = d + t1;
        d = c; c = b; b = a;
        a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void SHA256_Init(SHA256_CTX *context) {
    context->state[0] = 0x6a09e667; context->state[1] = 0xbb67ae85;
    context->state[2] = 0x3c6ef372; context->state[3] = 0xa54ff53a;
    context->state[4] = 0x510e527f; context->state[5] = 0x9b05688c;
    context->state[6] = 0x1f83d9ab; context->state[7] = 0x5be0cd19;
    context->count = 0;
}

static void SHA256_Update(SHA256_CTX *context, const uint8_t *input, uint32_t len) {
    uint32_t i, idx, partLen;
    idx = (uint32_t)(context->count & 63);
    context->count += len;
    partLen = 64 - idx;
    if (len >= partLen) {
        memcpy(&context->buffer[idx], input, partLen);
        SHA256_Transform(context->state, context->buffer);
        for (i = partLen; i + 63 < len; i += 64)
            SHA256_Transform(context->state, &input[i]);
        idx = 0;
    } else {
        i = 0;
    }
    memcpy(&context->buffer[idx], &input[i], len - i);
}

static void SHA256_Final(uint8_t digest[32], SHA256_CTX *context) {
    uint8_t bits[8];
    uint32_t idx, padLen;
    uint64_t count_bits = context->count << 3;
    for (int i = 0; i < 8; i++) {
        bits[i] = (uint8_t)((count_bits >> (56 - i * 8)) & 255);
    }
    idx = (uint32_t)(context->count & 63);
    padLen = (idx < 56) ? (56 - idx) : (120 - idx);
    SHA256_Update(context, MD5_PADDING, padLen);
    SHA256_Update(context, bits, 8);
    for (int i = 0; i < 8; i++) {
        digest[i*4]   = (uint8_t)((context->state[i] >> 24) & 255);
        digest[i*4+1] = (uint8_t)((context->state[i] >> 16) & 255);
        digest[i*4+2] = (uint8_t)((context->state[i] >> 8) & 255);
        digest[i*4+3] = (uint8_t)(context->state[i] & 255);
    }
    memset(context, 0, sizeof(*context));
}

/* -----------------------------------------------------------------------
 * Cryptographic Digest & Checksum Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_checksum(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING) {
        return SLP_NUM_VAL(0);
    }
    SlpObjString *str = SLP_AS_STRING(args[0]);
    uint32_t crc = calculate_crc32((const uint8_t*)str->chars, str->length);
    return SLP_NUM_VAL((double)crc);
}

static SlpValue builtin_digest(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING) {
        return SLP_NULL_VAL;
    }
    SlpObjString *str = SLP_AS_STRING(args[0]);
    const char *algo = "MD5";
    if (argc >= 2 && SLP_IS_OBJ(args[1]) && SLP_OBJ_TYPE(args[1]) == SLP_OBJ_STRING) {
        algo = SLP_AS_STRING(args[1])->chars;
    }
    
    if (strcmp(algo, "SHA-256") == 0 || strcmp(algo, "SHA256") == 0) {
        SHA256_CTX ctx;
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, (const uint8_t*)str->chars, str->length);
        uint8_t hash[32];
        SHA256_Final(hash, &ctx);
        
        char hex[65];
        for (int i = 0; i < 32; i++) {
            snprintf(hex + i*2, 3, "%02x", hash[i]);
        }
        hex[64] = '\0';
        SlpObjString *res = slp_vm_copy_string(vm, hex, 64);
        return SLP_OBJ_VAL(res);
    } else {
        MD5_CTX ctx;
        MD5_Init(&ctx);
        MD5_Update(&ctx, (const uint8_t*)str->chars, str->length);
        uint8_t hash[16];
        MD5_Final(hash, &ctx);
        
        char hex[33];
        for (int i = 0; i < 16; i++) {
            snprintf(hex + i*2, 3, "%02x", hash[i]);
        }
        hex[32] = '\0';
        SlpObjString *res = slp_vm_copy_string(vm, hex, 32);
        return SLP_OBJ_VAL(res);
    }
}

/* -----------------------------------------------------------------------
 * Functional Programming Builtins (map, filter, reduce)
 * ----------------------------------------------------------------------- */

static SlpValue builtin_map(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_CLOSURE ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_ARRAY) {
        return SLP_NULL_VAL;
    }
    SlpObjClosure *closure = SLP_AS_CLOSURE(args[0]);
    SlpObjArray *arr = SLP_AS_ARRAY(args[1]);
    
    SlpObjArray *res = slp_vm_new_array(vm);
    if (!res) return SLP_NULL_VAL;
    
    for (int i = 0; i < arr->count; i++) {
        SlpValue elem = arr->elements[i];
        slp_vm_push(vm, SLP_OBJ_VAL(closure));
        slp_vm_push(vm, elem);
        if (slp_vm_call(vm, 1, false) == SLP_OK) {
            SlpValue val = slp_vm_pop(vm);
            slp_obj_array_push(vm->allocator, res, val);
        } else {
            return SLP_NULL_VAL;
        }
    }
    return SLP_OBJ_VAL(res);
}

static SlpValue builtin_filter(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_CLOSURE ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_ARRAY) {
        return SLP_NULL_VAL;
    }
    SlpObjClosure *closure = SLP_AS_CLOSURE(args[0]);
    SlpObjArray *arr = SLP_AS_ARRAY(args[1]);
    
    SlpObjArray *res = slp_vm_new_array(vm);
    if (!res) return SLP_NULL_VAL;
    
    for (int i = 0; i < arr->count; i++) {
        SlpValue elem = arr->elements[i];
        slp_vm_push(vm, SLP_OBJ_VAL(closure));
        slp_vm_push(vm, elem);
        if (slp_vm_call(vm, 1, false) == SLP_OK) {
            SlpValue val = slp_vm_pop(vm);
            if (!slp_value_is_falsy(val)) {
                slp_obj_array_push(vm->allocator, res, elem);
            }
        } else {
            return SLP_NULL_VAL;
        }
    }
    return SLP_OBJ_VAL(res);
}

static SlpValue builtin_reduce(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_CLOSURE ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_ARRAY) {
        return SLP_NULL_VAL;
    }
    SlpObjClosure *closure = SLP_AS_CLOSURE(args[0]);
    SlpObjArray *arr = SLP_AS_ARRAY(args[1]);
    if (arr->count == 0) return SLP_NULL_VAL;
    
    SlpValue accum = arr->elements[0];
    for (int i = 1; i < arr->count; i++) {
        SlpValue next_val = arr->elements[i];
        slp_vm_push(vm, SLP_OBJ_VAL(closure));
        slp_vm_push(vm, next_val); // b
        slp_vm_push(vm, accum);    // a
        if (slp_vm_call(vm, 2, false) == SLP_OK) {
            accum = slp_vm_pop(vm);
        } else {
            return SLP_NULL_VAL;
        }
    }
    return accum;
}

/* -----------------------------------------------------------------------
 * Splicing & Subarray Utilities
 * ----------------------------------------------------------------------- */

static SlpValue builtin_splice(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 3 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY) {
        return SLP_NULL_VAL;
    }
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    int idx = (int)SLP_AS_NUM(args[1]);
    int len = (int)SLP_AS_NUM(args[2]);
    if (idx < 0) idx = arr->count + idx;
    if (idx < 0) idx = 0;
    if (idx > arr->count) idx = arr->count;
    if (len < 0) len = 0;
    if (idx + len > arr->count) len = arr->count - idx;
    
    SlpObjArray *removed = slp_vm_new_array(vm);
    for (int i = 0; i < len; i++) {
        slp_obj_array_push(vm->allocator, removed, arr->elements[idx + i]);
    }
    
    int repl_count = 0;
    SlpValue *repl_elems = NULL;
    if (argc >= 4) {
        SlpValue r = args[3];
        if (SLP_IS_OBJ(r) && SLP_OBJ_TYPE(r) == SLP_OBJ_ARRAY) {
            SlpObjArray *r_arr = SLP_AS_ARRAY(r);
            repl_count = r_arr->count;
            repl_elems = r_arr->elements;
        } else {
            repl_count = 1;
            repl_elems = &args[3];
        }
    }
    
    int diff = repl_count - len;
    if (diff > 0) {
        for (int i = 0; i < diff; i++) {
            slp_obj_array_push(vm->allocator, arr, SLP_NULL_VAL);
        }
        for (int i = arr->count - 1; i >= idx + repl_count; i--) {
            arr->elements[i] = arr->elements[i - diff];
        }
    } else if (diff < 0) {
        for (int i = idx + repl_count; i < arr->count + diff; i++) {
            arr->elements[i] = arr->elements[i - diff];
        }
        arr->count += diff;
    }
    
    for (int i = 0; i < repl_count; i++) {
        arr->elements[idx + i] = repl_elems[i];
    }
    
    return SLP_OBJ_VAL(removed);
}

static SlpValue builtin_subarray(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY) {
        return SLP_NULL_VAL;
    }
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    int start = (int)SLP_AS_NUM(args[1]);
    int end = arr->count;
    if (argc >= 3) {
        end = (int)SLP_AS_NUM(args[2]);
    }
    if (start < 0) start = arr->count + start;
    if (start < 0) start = 0;
    if (end < 0) end = arr->count + end;
    if (end < start) end = start;
    if (end > arr->count) end = arr->count;
    
    SlpObjArray *res = slp_vm_new_array(vm);
    for (int i = start; i < end; i++) {
        slp_obj_array_push(vm->allocator, res, arr->elements[i]);
    }
    return SLP_OBJ_VAL(res);
}

/* -----------------------------------------------------------------------
 * Bulk Collection Operations
 * ----------------------------------------------------------------------- */

static SlpValue builtin_addAll(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_ARRAY) {
        return SLP_BOOL_VAL(false);
    }
    SlpObjArray *a = SLP_AS_ARRAY(args[0]);
    SlpObjArray *b = SLP_AS_ARRAY(args[1]);
    for (int i = 0; i < b->count; i++) {
        slp_obj_array_push(vm->allocator, a, b->elements[i]);
    }
    return SLP_BOOL_VAL(b->count > 0);
}

static SlpValue builtin_removeAll(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_ARRAY) {
        return SLP_BOOL_VAL(false);
    }
    SlpObjArray *a = SLP_AS_ARRAY(args[0]);
    SlpObjArray *b = SLP_AS_ARRAY(args[1]);
    int removed = 0;
    for (int i = 0; i < a->count; ) {
        bool found = false;
        for (int j = 0; j < b->count; j++) {
            if (slp_value_equals(a->elements[i], b->elements[j])) {
                found = true;
                break;
            }
        }
        if (found) {
            for (int k = i; k < a->count - 1; k++) {
                a->elements[k] = a->elements[k + 1];
            }
            a->count--;
            removed++;
        } else {
            i++;
        }
    }
    return SLP_BOOL_VAL(removed > 0);
}

static SlpValue builtin_retainAll(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_ARRAY) {
        return SLP_BOOL_VAL(false);
    }
    SlpObjArray *a = SLP_AS_ARRAY(args[0]);
    SlpObjArray *b = SLP_AS_ARRAY(args[1]);
    int removed = 0;
    for (int i = 0; i < a->count; ) {
        bool found = false;
        for (int j = 0; j < b->count; j++) {
            if (slp_value_equals(a->elements[i], b->elements[j])) {
                found = true;
                break;
            }
        }
        if (!found) {
            for (int k = i; k < a->count - 1; k++) {
                a->elements[k] = a->elements[k + 1];
            }
            a->count--;
            removed++;
        } else {
            i++;
        }
    }
    return SLP_BOOL_VAL(removed > 0);
}

/* -----------------------------------------------------------------------
 * Queue / Stack & Search Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_pushl(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY) {
        return SLP_NULL_VAL;
    }
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    int count = argc - 1;
    for (int i = 0; i < count; i++) {
        slp_obj_array_push(vm->allocator, arr, SLP_NULL_VAL);
    }
    for (int i = arr->count - 1; i >= count; i--) {
        arr->elements[i] = arr->elements[i - count];
    }
    for (int i = 0; i < count; i++) {
        arr->elements[i] = args[i + 1];
    }
    return args[0];
}

static SlpValue builtin_popl(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY) {
        return SLP_NULL_VAL;
    }
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    if (arr->count == 0) return SLP_NULL_VAL;
    SlpValue front = arr->elements[0];
    for (int i = 0; i < arr->count - 1; i++) {
        arr->elements[i] = arr->elements[i + 1];
    }
    arr->count--;
    return front;
}

static SlpValue builtin_search(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_ARRAY ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_CLOSURE) {
        return SLP_NULL_VAL;
    }
    SlpObjArray *arr = SLP_AS_ARRAY(args[0]);
    SlpObjClosure *closure = SLP_AS_CLOSURE(args[1]);
    for (int i = 0; i < arr->count; i++) {
        slp_vm_push(vm, SLP_OBJ_VAL(closure));
        slp_vm_push(vm, arr->elements[i]);
        if (slp_vm_call(vm, 1, false) == SLP_OK) {
            SlpValue val = slp_vm_pop(vm);
            if (!slp_value_is_falsy(val)) {
                return arr->elements[i];
            }
        } else {
            return SLP_NULL_VAL;
        }
    }
    return SLP_NULL_VAL;
}

/* -----------------------------------------------------------------------
 * Number Parsing & Ordered Hash Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_parseNumber(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_STRING) {
        return SLP_NUM_VAL(0);
    }
    const char *s = SLP_AS_STRING(args[0])->chars;
    while (*s && isspace((unsigned char)*s)) s++;
    
    long long val = 0;
    if (strncmp(s, "0x", 2) == 0 || strncmp(s, "0X", 2) == 0) {
        val = strtoll(s, NULL, 16);
    } else if (s[0] == '0' && isdigit((unsigned char)s[1])) {
        val = strtoll(s, NULL, 8);
    } else {
        if (strchr(s, '.') || strchr(s, 'e') || strchr(s, 'E')) {
            double d = strtod(s, NULL);
            return SLP_NUM_VAL(d);
        }
        val = strtoll(s, NULL, 10);
    }
    return SLP_NUM_VAL((double)val);
}

static SlpValue builtin_ohash(SlpVM *vm, SlpValue *args, int argc) {
    (void)args; (void)argc;
    return SLP_OBJ_VAL(slp_vm_new_hash(vm));
}

/* -----------------------------------------------------------------------
 * Bidirectional Standard and Network I/O Bridge Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_openf(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    const char *path = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";
    
    const char *mode = "rb";
    if (path[0] == '>') {
        mode = "wb";
        path++;
    } else if (path[0] == '<') {
        mode = "rb";
        path++;
    }
    
    FILE *f = fopen(path, mode);
    if (!f) return SLP_NULL_VAL;
    
    SlpObjIOHandle *handle = slp_obj_io_handle_new(vm->allocator);
    if (!handle) {
        fclose(f);
        return SLP_NULL_VAL;
    }
    handle->file = f;
    handle->obj.next = vm->objects;
    vm->objects = &handle->obj;
    
    return SLP_OBJ_VAL(handle);
}

static SlpValue builtin_connect(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2) return SLP_NULL_VAL;
    const char *host = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "127.0.0.1";
    int port = (int)SLP_AS_NUM(args[1]);
    
    int sock = -1;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
    sock = (int)socket(AF_INET, SOCK_STREAM, 0);
#else
    sock = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (sock < 0) return SLP_NULL_VAL;
    
    struct hostent *server = gethostbyname(host);
    if (!server) {
        slp_platform_close_socket(sock);
        return SLP_NULL_VAL;
    }
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    char *addr_ptr;
    memcpy(&addr_ptr, server->h_addr_list, sizeof(char *));
    memcpy(&serv_addr.sin_addr.s_addr, addr_ptr, (size_t)server->h_length);
    serv_addr.sin_port = htons((uint16_t)port);
    
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        slp_platform_close_socket(sock);
        return SLP_NULL_VAL;
    }
    
    SlpObjIOHandle *handle = slp_obj_io_handle_new(vm->allocator);
    if (!handle) {
        slp_platform_close_socket(sock);
        return SLP_NULL_VAL;
    }
    handle->socket_fd = sock;
    handle->is_socket = true;
    
    handle->obj.next = vm->objects;
    vm->objects = &handle->obj;
    
    return SLP_OBJ_VAL(handle);
}

static SlpValue builtin_listen(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    int port = (int)SLP_AS_NUM(args[0]);
    
    int sock = -1;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
    sock = (int)socket(AF_INET, SOCK_STREAM, 0);
#else
    sock = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (sock < 0) return SLP_NULL_VAL;
    
    int opt = 1;
#ifdef _WIN32
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons((uint16_t)port);
    
    if (bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        slp_platform_close_socket(sock);
        return SLP_NULL_VAL;
    }
    
    if (listen(sock, 3) < 0) {
        slp_platform_close_socket(sock);
        return SLP_NULL_VAL;
    }
    
    SlpObjIOHandle *handle = slp_obj_io_handle_new(vm->allocator);
    if (!handle) {
        slp_platform_close_socket(sock);
        return SLP_NULL_VAL;
    }
    handle->socket_fd = sock;
    handle->is_socket = true;
    
    handle->obj.next = vm->objects;
    vm->objects = &handle->obj;
    
    return SLP_OBJ_VAL(handle);
}

static SlpValue builtin_exec(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1) return SLP_NULL_VAL;
    const char *cmd = SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING ? SLP_AS_STRING(args[0])->chars : "";

    SlpPlatformProcess *proc = slp_platform_exec(cmd, vm->allocator);
    if (!proc) return SLP_NULL_VAL;

    SlpObjIOHandle *handle = slp_obj_io_handle_new(vm->allocator);
    if (!handle) {
        slp_platform_close_process(proc);
        slp_platform_free_process(proc, vm->allocator);
        return SLP_NULL_VAL;
    }

    handle->file = proc->read_stream;
    handle->socket_fd = proc->write_fd;
    handle->pid = proc->pid;
    handle->is_pipeline = true;

    slp_platform_free_process(proc, vm->allocator);

    handle->obj.next = vm->objects;
    vm->objects = &handle->obj;

    return SLP_OBJ_VAL(handle);
}

static SlpValue builtin_readln(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE) {
        return SLP_NULL_VAL;
    }
    SlpObjIOHandle *h = SLP_AS_IO_HANDLE(args[0]);
    if (h->is_eof) return SLP_NULL_VAL;
    
    char buf[2048];
    if (h->file) {
        if (fgets(buf, sizeof(buf), h->file)) {
            size_t len = strlen(buf);
            if (len > 0 && buf[len-1] == '\n') {
                buf[len-1] = '\0';
                len--;
            }
            if (len > 0 && buf[len-1] == '\r') {
                buf[len-1] = '\0';
                len--;
            }
            SlpObjString *s = slp_vm_copy_string(vm, buf, (uint32_t)len);
            return SLP_OBJ_VAL(s);
        } else {
            h->is_eof = true;
            return SLP_NULL_VAL;
        }
    } else if (h->is_socket && h->socket_fd != -1) {
        size_t idx = 0;
        char c;
        while (idx < sizeof(buf) - 1) {
            ssize_t n = recv(h->socket_fd, &c, 1, 0);
            if (n <= 0) {
                h->is_eof = true;
                break;
            }
            if (c == '\n') break;
            if (c != '\r') buf[idx++] = c;
        }
        if (idx == 0 && h->is_eof) return SLP_NULL_VAL;
        buf[idx] = '\0';
        SlpObjString *s = slp_vm_copy_string(vm, buf, (uint32_t)idx);
        return SLP_OBJ_VAL(s);
    }
    return SLP_NULL_VAL;
}

static SlpValue builtin_readAll(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE) {
        return SLP_NULL_VAL;
    }
    SlpObjIOHandle *h = SLP_AS_IO_HANDLE(args[0]);
    if (h->is_eof) return SLP_NULL_VAL;
    
    PackBuffer pb;
    pb.data = NULL;
    pb.cap = 0;
    pb.len = 0;
    
    char buf[4096];
    if (h->file) {
        while (true) {
            size_t n = fread(buf, 1, sizeof(buf), h->file);
            if (n == 0) break;
            buf_push_bytes(vm->allocator, &pb, (const uint8_t*)buf, n);
        }
        h->is_eof = true;
    } else if (h->is_socket && h->socket_fd != -1) {
        while (true) {
            ssize_t n = recv(h->socket_fd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            buf_push_bytes(vm->allocator, &pb, (const uint8_t*)buf, (size_t)n);
        }
        h->is_eof = true;
    }
    
    SlpObjString *s = slp_vm_copy_string(vm, pb.data ? pb.data : "", (uint32_t)pb.len);
    if (pb.data) vm->allocator->reallocate(pb.data, 0, vm->allocator->user_data);
    return SLP_OBJ_VAL(s);
}

static SlpValue builtin_readc(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE) {
        return SLP_NULL_VAL;
    }
    SlpObjIOHandle *h = SLP_AS_IO_HANDLE(args[0]);
    if (h->is_eof) return SLP_NULL_VAL;
    
    char c = '\0';
    if (h->file) {
        int val = fgetc(h->file);
        if (val == EOF) {
            h->is_eof = true;
            return SLP_NULL_VAL;
        }
        c = (char)val;
    } else if (h->is_socket && h->socket_fd != -1) {
        ssize_t n = recv(h->socket_fd, &c, 1, 0);
        if (n <= 0) {
            h->is_eof = true;
            return SLP_NULL_VAL;
        }
    } else {
        return SLP_NULL_VAL;
    }
    char str[2] = {c, '\0'};
    SlpObjString *s = slp_vm_copy_string(vm, str, 1);
    return SLP_OBJ_VAL(s);
}

static SlpValue builtin_readb(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE) {
        return SLP_NULL_VAL;
    }
    SlpObjIOHandle *h = SLP_AS_IO_HANDLE(args[0]);
    int to_read = (int)SLP_AS_NUM(args[1]);
    if (h->is_eof) return SLP_NULL_VAL;
    
    if (to_read <= 0) {
        size_t capacity = 4096;
        size_t total = 0;
        uint8_t *buf = SLP_MALLOC(vm->allocator, capacity);
        if (!buf) return SLP_NULL_VAL;
        
        while (1) {
            if (total == capacity) {
                size_t new_cap = capacity * 2;
                uint8_t *new_buf = SLP_REALLOC(vm->allocator, buf, new_cap);
                if (!new_buf) {
                    SLP_FREE(vm->allocator, buf);
                    return SLP_NULL_VAL;
                }
                buf = new_buf;
                capacity = new_cap;
            }
            int actual = 0;
            if (h->file) {
                actual = (int)fread(buf + total, 1, capacity - total, h->file);
                if (actual == 0) {
                    h->is_eof = true;
                    break;
                }
            } else if (h->is_socket && h->socket_fd != -1) {
                actual = (int)recv(h->socket_fd, (char*)buf + total, capacity - total, 0);
                if (actual <= 0) {
                    h->is_eof = true;
                    break;
                }
            } else {
                break;
            }
            total += actual;
        }
        
        if (total == 0) {
            SLP_FREE(vm->allocator, buf);
            return SLP_NULL_VAL;
        }
        
        SlpObjString *s = slp_vm_copy_string(vm, (const char*)buf, (uint32_t)total);
        SLP_FREE(vm->allocator, buf);
        return SLP_OBJ_VAL(s);
    }
    
    uint8_t *buf = SLP_MALLOC(vm->allocator, (size_t)to_read);
    if (!buf) return SLP_NULL_VAL;
    
    int actual = 0;
    if (h->file) {
        actual = (int)fread(buf, 1, (size_t)to_read, h->file);
        if (actual == 0) h->is_eof = true;
    } else if (h->is_socket && h->socket_fd != -1) {
        actual = (int)recv(h->socket_fd, (char*)buf, (size_t)to_read, 0);
        if (actual <= 0) {
            actual = 0;
            h->is_eof = true;
        }
    }
    
    SlpObjString *s = slp_vm_copy_string(vm, (const char*)buf, (uint32_t)actual);
    SLP_FREE(vm->allocator, buf);
    return SLP_OBJ_VAL(s);
}

static SlpValue builtin_writeb(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING) {
        return SLP_BOOL_VAL(false);
    }
    SlpObjIOHandle *h = SLP_AS_IO_HANDLE(args[0]);
    SlpObjString *str = SLP_AS_STRING(args[1]);
    
    int written = 0;
    if (h->is_pipeline && h->socket_fd != -1) {
#ifdef _WIN32
        written = 0;
#else
        written = (int)write(h->socket_fd, str->chars, str->length);
#endif
    } else if (h->file) {
        written = (int)fwrite(str->chars, 1, str->length, h->file);
        fflush(h->file);
    } else if (h->is_socket && h->socket_fd != -1) {
#ifdef _WIN32
        written = (int)send(h->socket_fd, str->chars, (int)str->length, 0);
#else
        written = (int)send(h->socket_fd, str->chars, str->length, 0);
#endif
    }
    return SLP_BOOL_VAL(written > 0);
}

static SlpValue builtin_closef(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE) {
        return SLP_BOOL_VAL(false);
    }
    SlpObjIOHandle *h = SLP_AS_IO_HANDLE(args[0]);
    if (h->file) {
        slp_platform_close_file(h->file, h->is_pipeline);
        h->file = NULL;
    }
    if (h->socket_fd != -1) {
        slp_platform_close_socket(h->socket_fd);
        h->socket_fd = -1;
    }
    if (h->pid != -1) {
        int status;
        slp_platform_waitpid(h->pid, &status, 0);
        h->pid = -1;
    }
    return SLP_BOOL_VAL(true);
}

static int estimate_pack_size(const char *format) {
    int total = 0;
    int len = (int)strlen(format);
    int x = 0;
    while (x < len) {
        char c = format[x];
        if (c == '+' || c == '-' || c == '!') {
            x++;
            continue;
        }
        if (isalpha((unsigned char)c)) {
            x++;
            int count = 1;
            if (x < len && format[x] == '*') {
                count = 1024;
                x++;
            } else if (x < len && isdigit((unsigned char)format[x])) {
                count = 0;
                while (x < len && isdigit((unsigned char)format[x])) {
                    count = count * 10 + (format[x] - '0');
                    x++;
                }
            }
            int unit = 1;
            switch (c) {
                case 'c': case 's': case 'S': unit = 2; break;
                case 'i': case 'I': case 'f': unit = 4; break;
                case 'd': case 'l': unit = 8; break;
                default: unit = 1; break;
            }
            total += count * unit;
        } else {
            x++;
        }
    }
    return total;
}

static SlpValue builtin_bread(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE ||
        !SLP_IS_OBJ(args[1]) || SLP_OBJ_TYPE(args[1]) != SLP_OBJ_STRING) {
        return SLP_NULL_VAL;
    }
    const char *format = SLP_AS_STRING(args[1])->chars;
    int size = estimate_pack_size(format);
    if (size <= 0) size = 1024;
    
    SlpValue read_args[2] = {args[0], SLP_NUM_VAL((double)size)};
    SlpValue data = builtin_readb(vm, read_args, 2);
    if (SLP_IS_NULL(data)) return SLP_NULL_VAL;
    
    SlpValue unpack_args[2] = {args[1], data};
    return builtin_unpack(vm, unpack_args, 2);
}

static SlpValue builtin_bwrite(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 2 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE) {
        return SLP_BOOL_VAL(false);
    }
    SlpValue packed = builtin_pack(vm, args + 1, argc - 1);
    if (SLP_IS_NULL(packed)) return SLP_BOOL_VAL(false);
    
    SlpValue write_args[2] = {args[0], packed};
    return builtin_writeb(vm, write_args, 2);
}

static SlpValue builtin_available(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_IO_HANDLE) {
        return SLP_NUM_VAL(0);
    }
    SlpObjIOHandle *h = SLP_AS_IO_HANDLE(args[0]);
    if (h->is_eof) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(1);
}

static SlpValue builtin_sizeof(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    SlpValue arg = args[0];
    if (SLP_IS_OBJ(arg)) {
        SlpObj *obj = SLP_AS_OBJ(arg);
        if (obj->type == SLP_OBJ_STRING)
            return SLP_NUM_VAL((double)((SlpObjString*)obj)->length);
        if (obj->type == SLP_OBJ_ARRAY)
            return SLP_NUM_VAL((double)((SlpObjArray*)obj)->count);
        if (obj->type == SLP_OBJ_HASH)
            return SLP_NUM_VAL((double)((SlpObjHash*)obj)->count);
    }
    return SLP_NUM_VAL(0);
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void slp_stdlib_init(SlpVM *vm) {
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }

    slp_vm_register_native(vm, "iff",       builtin_iff);

    slp_vm_register_native(vm, "abs",       builtin_abs);
    slp_vm_register_native(vm, "ceil",      builtin_ceil);
    slp_vm_register_native(vm, "floor",     builtin_floor);
    slp_vm_register_native(vm, "sqrt",      builtin_sqrt);
    slp_vm_register_native(vm, "sin",       builtin_sin);
    slp_vm_register_native(vm, "cos",       builtin_cos);
    slp_vm_register_native(vm, "tan",       builtin_tan);
    slp_vm_register_native(vm, "asin",      builtin_asin);
    slp_vm_register_native(vm, "acos",      builtin_acos);
    slp_vm_register_native(vm, "atan",      builtin_atan);
    slp_vm_register_native(vm, "atan2",     builtin_atan2);
    slp_vm_register_native(vm, "exp",       builtin_exp);
    slp_vm_register_native(vm, "log",       builtin_log);
    slp_vm_register_native(vm, "round",     builtin_round);
    slp_vm_register_native(vm, "double",    builtin_double);
    slp_vm_register_native(vm, "degrees",   builtin_degrees);
    slp_vm_register_native(vm, "radians",   builtin_radians);
    slp_vm_register_native(vm, "min",       builtin_min);
    slp_vm_register_native(vm, "max",       builtin_max);

    slp_vm_register_native(vm, "strlen",    builtin_strlen);
    slp_vm_register_native(vm, "substr",    builtin_substr);
    slp_vm_register_native(vm, "lc",        builtin_lc);
    slp_vm_register_native(vm, "uc",        builtin_uc);
    slp_vm_register_native(vm, "replace",   builtin_replace);
    slp_vm_register_native(vm, "strrep",    builtin_replace);
    slp_vm_register_native(vm, "split",     builtin_split);
    slp_vm_register_native(vm, "join",      builtin_join);
    slp_vm_register_native(vm, "chr",       builtin_chr);
    slp_vm_register_native(vm, "asc",       builtin_asc);
    slp_vm_register_native(vm, "charAt",    builtin_charAt);
    slp_vm_register_native(vm, "left",      builtin_left);
    slp_vm_register_native(vm, "right",     builtin_right);
    slp_vm_register_native(vm, "mid",       builtin_mid);
    slp_vm_register_native(vm, "find",      builtin_find);
    slp_vm_register_native(vm, "indexOf",   builtin_indexOf);
    slp_vm_register_native(vm, "formatNumber", builtin_formatNumber);
    slp_vm_register_native(vm, "tr",        builtin_tr);
    slp_vm_register_native(vm, "replaceAt", builtin_replaceAt);
    slp_vm_register_native(vm, "lindexOf",  builtin_lindexOf);

    slp_vm_register_native(vm, "add",       builtin_add);
    slp_vm_register_native(vm, "copy",      builtin_copy);
    slp_vm_register_native(vm, "clear",     builtin_clear);
    slp_vm_register_native(vm, "sort",      builtin_sort);
    slp_vm_register_native(vm, "sorta",     builtin_sorta);
    slp_vm_register_native(vm, "sortd",     builtin_sortd);
    slp_vm_register_native(vm, "sortn",     builtin_sortn);
    slp_vm_register_native(vm, "reverse",   builtin_reverse);
    slp_vm_register_native(vm, "shift",     builtin_shift);
    slp_vm_register_native(vm, "flatten",   builtin_flatten);
    slp_vm_register_native(vm, "concat",    builtin_concat);
    slp_vm_register_native(vm, "sum",       builtin_sum);
    slp_vm_register_native(vm, "removeAt",  builtin_removeAt);
    slp_vm_register_native(vm, "contains",  builtin_contains);

    slp_vm_register_native(vm, "int",       builtin_int);
    slp_vm_register_native(vm, "long",      builtin_long);
    slp_vm_register_native(vm, "uint",      builtin_uint);
    slp_vm_register_native(vm, "local",     builtin_local);
    slp_vm_register_native(vm, "values",    builtin_values);
    slp_vm_register_native(vm, "sublist",   builtin_sublist);
    slp_vm_register_native(vm, "cast",      builtin_cast);

    slp_vm_register_native(vm, "typeOf",    builtin_typeOf);
    slp_vm_register_native(vm, "global",    builtin_global);
    slp_vm_register_native(vm, "byteAt",    builtin_byteAt);

    slp_vm_register_native(vm, "print",     builtin_print);
    slp_vm_register_native(vm, "println",   builtin_println);
    slp_vm_register_native(vm, "ticks",     builtin_ticks);
    slp_vm_register_native(vm, "rand",      builtin_rand);
    slp_vm_register_native(vm, "srand",     builtin_srand);
    slp_vm_register_native(vm, "tstamp",    builtin_tstamp);
    slp_vm_register_native(vm, "sleep",     builtin_sleep_ms);

    /* Expanded bridge builtins */
    slp_vm_register_native(vm, "matched",         builtin_matched);
    slp_vm_register_native(vm, "pack",            builtin_pack);
    slp_vm_register_native(vm, "unpack",          builtin_unpack);
    slp_vm_register_native(vm, "ls",              builtin_ls);
    slp_vm_register_native(vm, "mkdir",           builtin_mkdir);
    slp_vm_register_native(vm, "deleteFile",      builtin_deleteFile);
    slp_vm_register_native(vm, "rename",          builtin_rename);
    slp_vm_register_native(vm, "lof",             builtin_lof);
    slp_vm_register_native(vm, "lastModified",    builtin_lastModified);
    slp_vm_register_native(vm, "cwd",             builtin_cwd);
    slp_vm_register_native(vm, "chdir",           builtin_chdir);
    slp_vm_register_native(vm, "getFileName",     builtin_getFileName);
    slp_vm_register_native(vm, "getFileParent",   builtin_getFileParent);
    slp_vm_register_native(vm, "getFileProper",   builtin_getFileProper);
    slp_vm_register_native(vm, "formatDate",      builtin_formatDate);
    slp_vm_register_native(vm, "parseDate",       builtin_parseDate);

    /* Digests & Cryptography */
    slp_vm_register_native(vm, "checksum",        builtin_checksum);
    slp_vm_register_native(vm, "digest",          builtin_digest);

    /* Functional programming */
    slp_vm_register_native(vm, "map",             builtin_map);
    slp_vm_register_native(vm, "filter",          builtin_filter);
    slp_vm_register_native(vm, "reduce",          builtin_reduce);

    /* Array / list utilities */
    slp_vm_register_native(vm, "splice",          builtin_splice);
    slp_vm_register_native(vm, "subarray",        builtin_subarray);
    slp_vm_register_native(vm, "addAll",          builtin_addAll);
    slp_vm_register_native(vm, "removeAll",        builtin_removeAll);
    slp_vm_register_native(vm, "retainAll",        builtin_retainAll);
    slp_vm_register_native(vm, "pushl",            builtin_pushl);
    slp_vm_register_native(vm, "popl",             builtin_popl);
    slp_vm_register_native(vm, "search",           builtin_search);

    /* Number parsing */
    slp_vm_register_native(vm, "parseNumber",     builtin_parseNumber);

    /* Ordered hash */
    slp_vm_register_native(vm, "ohash",           builtin_ohash);
    slp_vm_register_native(vm, "ohasha",          builtin_ohash);

    /* Standard and network I/O, Subprocesses */
    slp_vm_register_native(vm, "openf",           builtin_openf);
    slp_vm_register_native(vm, "connect",         builtin_connect);
    slp_vm_register_native(vm, "listen",          builtin_listen);
    slp_vm_register_native(vm, "exec",            builtin_exec);
    slp_vm_register_native(vm, "readln",          builtin_readln);
    slp_vm_register_native(vm, "readAll",         builtin_readAll);
    slp_vm_register_native(vm, "readc",           builtin_readc);
    slp_vm_register_native(vm, "readb",           builtin_readb);
    slp_vm_register_native(vm, "writeb",          builtin_writeb);
    slp_vm_register_native(vm, "closef",          builtin_closef);
    slp_vm_register_native(vm, "bread",           builtin_bread);
    slp_vm_register_native(vm, "bwrite",          builtin_bwrite);
    slp_vm_register_native(vm, "available",       builtin_available);
    slp_vm_register_native(vm, "sizeof",          builtin_sizeof);
}
