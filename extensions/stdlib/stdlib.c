#include "slp_stdlib.h"
#include "slp_value.h"
#include "slp_vm.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

/* Allocator wrappers using VM allocator */
static void *stdlib_alloc(SlpVM *vm, size_t size) {
    return vm->allocator->reallocate(NULL, size, vm->allocator->user_data);
}

static void stdlib_free(SlpVM *vm, void *ptr) {
    vm->allocator->reallocate(ptr, 0, vm->allocator->user_data);
}

/* -----------------------------------------------------------------------
 * Control Flow & Predicate Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_iff(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 3) return SLP_NULL_VAL;
    if (SLP_IS_TRUE(args[0])) return args[1];
    return args[2];
}

static SlpValue builtin_istrue(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL(SLP_IS_TRUE(args[0]) ? 1 : 0);
}

static SlpValue builtin_isadmin(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
    return SLP_NUM_VAL(1); /* Mock default: admin */
}

static SlpValue builtin_isnumber(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    if (SLP_IS_NUM(args[0])) return SLP_NUM_VAL(1);
    if (SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
        const char *s = SLP_AS_STRING(args[0])->chars;
        if (!s || *s == '\0') return SLP_NUM_VAL(0);
        char *endptr;
        strtod(s, &endptr);
        return SLP_NUM_VAL(*endptr == '\0' ? 1 : 0);
    }
    return SLP_NUM_VAL(0);
}

static SlpValue builtin_ismatch(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
    return SLP_NUM_VAL(1); /* Mock default: true */
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
        return SLP_OBJ_VAL(slp_obj_array_new(vm->allocator));
    }
    const char *pattern = SLP_AS_STRING(args[0])->chars;
    const char *str = SLP_AS_STRING(args[1])->chars;
    SlpObjArray *arr = slp_obj_array_new(vm->allocator);
    
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

/* -----------------------------------------------------------------------
 * Collection & Hash Builtins
 * ----------------------------------------------------------------------- */

static SlpValue builtin_size(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    if (SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_ARRAY) {
        return SLP_NUM_VAL((double)SLP_AS_ARRAY(args[0])->count);
    }
    if (SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
        return SLP_NUM_VAL((double)SLP_AS_STRING(args[0])->length);
    }
    return SLP_NUM_VAL(0);
}

static SlpValue builtin_int(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm;
    if (argc < 1) return SLP_NUM_VAL(0);
    if (!SLP_IS_NUM(args[0])) return SLP_NUM_VAL(0);
    return SLP_NUM_VAL((double)(int)SLP_AS_NUM(args[0]));
}

static SlpValue builtin_local(SlpVM *vm, SlpValue *args, int argc) {
    (void)vm; (void)args; (void)argc;
    return SLP_NULL_VAL;
}

static SlpValue builtin_keys(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_HASH) {
        return SLP_OBJ_VAL(slp_obj_array_new(vm->allocator));
    }
    SlpObjHash *hash = (SlpObjHash*)SLP_AS_OBJ(args[0]);
    SlpObjArray *arr = slp_obj_array_new(vm->allocator);
    for (int i = 0; i < hash->capacity; i++) {
        if (!SLP_IS_NULL(hash->entries[i].key)) {
            slp_obj_array_push(vm->allocator, arr, hash->entries[i].key);
        }
    }
    return SLP_OBJ_VAL(arr);
}

static SlpValue builtin_values(SlpVM *vm, SlpValue *args, int argc) {
    if (argc < 1 || !SLP_IS_OBJ(args[0]) || SLP_OBJ_TYPE(args[0]) != SLP_OBJ_HASH) {
        return SLP_OBJ_VAL(slp_obj_array_new(vm->allocator));
    }
    SlpObjHash *hash = (SlpObjHash*)SLP_AS_OBJ(args[0]);
    SlpObjArray *arr = slp_obj_array_new(vm->allocator);
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
        return SLP_OBJ_VAL(slp_obj_array_new(vm->allocator));
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
    
    SlpObjArray *res = slp_obj_array_new(vm->allocator);
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
    (void)vm;
    if (argc >= 1) {
        if (SLP_IS_OBJ(args[0]) && SLP_OBJ_TYPE(args[0]) == SLP_OBJ_STRING) {
            printf("%s\n", SLP_AS_STRING(args[0])->chars);
        } else if (SLP_IS_NUM(args[0])) {
            printf("%g\n", SLP_AS_NUM(args[0]));
        } else if (SLP_IS_BOOL(args[0])) {
            printf("%s\n", SLP_AS_BOOL(args[0]) ? "true" : "false");
        } else if (SLP_IS_NULL(args[0])) {
            printf("$null\n");
        }
    } else {
        printf("\n");
    }
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
 * Public API
 * ----------------------------------------------------------------------- */

void slp_stdlib_init(SlpVM *vm) {
    /* Initialize random seed if not done already */
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }

    slp_vm_register_native(vm, "iff",       builtin_iff);
    slp_vm_register_native(vm, "-istrue",   builtin_istrue);
    slp_vm_register_native(vm, "-isadmin",  builtin_isadmin);
    slp_vm_register_native(vm, "-isnumber", builtin_isnumber);
    slp_vm_register_native(vm, "-ismatch",  builtin_ismatch);

    slp_vm_register_native(vm, "strlen",    builtin_strlen);
    slp_vm_register_native(vm, "substr",    builtin_substr);
    slp_vm_register_native(vm, "lc",        builtin_lc);
    slp_vm_register_native(vm, "uc",        builtin_uc);
    slp_vm_register_native(vm, "replace",   builtin_replace);
    slp_vm_register_native(vm, "strrep",    builtin_replace);
    slp_vm_register_native(vm, "split",     builtin_split);
    slp_vm_register_native(vm, "join",      builtin_join);

    slp_vm_register_native(vm, "size",      builtin_size);
    slp_vm_register_native(vm, "int",       builtin_int);
    slp_vm_register_native(vm, "local",     builtin_local);
    slp_vm_register_native(vm, "keys",      builtin_keys);
    slp_vm_register_native(vm, "values",    builtin_values);
    slp_vm_register_native(vm, "sublist",   builtin_sublist);

    slp_vm_register_native(vm, "print",     builtin_print);
    slp_vm_register_native(vm, "println",   builtin_println);
    slp_vm_register_native(vm, "ticks",     builtin_ticks);
    slp_vm_register_native(vm, "rand",      builtin_rand);
    slp_vm_register_native(vm, "tstamp",    builtin_tstamp);
}
