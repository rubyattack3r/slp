#include "bof.h"
#include "coff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* Mock Beacon APIs */
typedef struct {
    char *original;
    char *buffer;
    int length;
    int size;
} datap;

static void MockBeaconDataParse(datap *parser, char *buffer, int size) {
    parser->original = buffer;
    parser->buffer = buffer;
    parser->length = size;
    parser->size = size;
}

static int MockBeaconDataInt(datap *parser) {
    if (parser->length < 4) return 0;
    int v = *(int*)parser->buffer;
    parser->buffer += 4;
    parser->length -= 4;
    return v;
}

static short MockBeaconDataShort(datap *parser) {
    if (parser->length < 2) return 0;
    short v = *(short*)parser->buffer;
    parser->buffer += 2;
    parser->length -= 2;
    return v;
}

static int MockBeaconDataLength(datap *parser) {
    if (parser->length < 4) return 0;
    return *(int*)parser->buffer;
}

static char *MockBeaconDataExtract(datap *parser, int *size) {
    if (parser->length < 4) {
        if (size) *size = 0;
        return NULL;
    }
    int len = *(int*)parser->buffer;
    parser->buffer += 4;
    parser->length -= 4;

    if (len > parser->length) len = parser->length;
    char *out = parser->buffer;
    parser->buffer += len;
    parser->length -= len;
    if (size) *size = len;
    return out;
}

static void MockBeaconPrintf(int type, char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (type == 0x0d) { // ERROR
        printf("\x1b[31m[BOF Error]\x1b[0m ");
    } else {
        printf("[BOF Output] ");
    }
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

static void MockBeaconOutput(int type, char *data, int len) {
    if (type == 0x0d) {
        printf("\x1b[31m[BOF Error]\x1b[0m ");
    } else {
        printf("[BOF Output] ");
    }
    fwrite(data, 1, len, stdout);
    printf("\n");
}

/* Format APIs */
typedef struct {
    char *original;
    char *buffer;
    int length;
    int size;
} formatp;

static void MockBeaconFormatAlloc(formatp *format, int maxsz) {
    format->original = (char*)calloc(1, maxsz);
    format->buffer = format->original;
    format->size = maxsz;
    format->length = maxsz;
}

static void MockBeaconFormatReset(formatp *format) {
    memset(format->original, 0, format->size);
    format->buffer = format->original;
    format->length = format->size;
}

static void MockBeaconFormatAppend(formatp *format, char *text, int len) {
    if (len > format->length) len = format->length;
    memcpy(format->buffer, text, len);
    format->buffer += len;
    format->length -= len;
}

static void MockBeaconFormatPrintf(formatp *format, char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(format->buffer, format->length, fmt, args);
    if (written > 0) {
        if (written > format->length) written = format->length;
        format->buffer += written;
        format->length -= written;
    }
    va_end(args);
}

static char *MockBeaconFormatToString(formatp *format, int *size) {
    if (size) *size = (int)(format->buffer - format->original);
    return format->original;
}

static void MockBeaconFormatFree(formatp *format) {
    if (format->original) {
        free(format->original);
        format->original = NULL;
    }
}

static void MockBeaconFormatInt(formatp *format, int value) {
    if (format->length >= 4) {
        // BeaconFormatInt packs a 4-byte big-endian integer
        unsigned char *buf = (unsigned char *)format->buffer;
        buf[0] = (value >> 24) & 0xFF;
        buf[1] = (value >> 16) & 0xFF;
        buf[2] = (value >> 8) & 0xFF;
        buf[3] = value & 0xFF;
        format->buffer += 4;
        format->length -= 4;
    }
}

/* Symbol Resolution Callback */
static void *bof_resolver(const char *name, void *userdata) {
    (void)userdata;
    // Strip "__imp_" if present to normalize the name
    const char *func_name = name;
    if (strncmp(name, "__imp_", 6) == 0) {
        func_name = name + 6;
    }

    if (strcmp(func_name, "BeaconDataParse") == 0) return (void *)MockBeaconDataParse;
    if (strcmp(func_name, "BeaconDataInt") == 0) return (void *)MockBeaconDataInt;
    if (strcmp(func_name, "BeaconDataShort") == 0) return (void *)MockBeaconDataShort;
    if (strcmp(func_name, "BeaconDataLength") == 0) return (void *)MockBeaconDataLength;
    if (strcmp(func_name, "BeaconDataExtract") == 0) return (void *)MockBeaconDataExtract;
    if (strcmp(func_name, "BeaconPrintf") == 0) return (void *)MockBeaconPrintf;
    if (strcmp(func_name, "BeaconOutput") == 0) return (void *)MockBeaconOutput;
    
    if (strcmp(func_name, "BeaconFormatAlloc") == 0) return (void *)MockBeaconFormatAlloc;
    if (strcmp(func_name, "BeaconFormatReset") == 0) return (void *)MockBeaconFormatReset;
    if (strcmp(func_name, "BeaconFormatAppend") == 0) return (void *)MockBeaconFormatAppend;
    if (strcmp(func_name, "BeaconFormatPrintf") == 0) return (void *)MockBeaconFormatPrintf;
    if (strcmp(func_name, "BeaconFormatToString") == 0) return (void *)MockBeaconFormatToString;
    if (strcmp(func_name, "BeaconFormatFree") == 0) return (void *)MockBeaconFormatFree;
    if (strcmp(func_name, "BeaconFormatInt") == 0) return (void *)MockBeaconFormatInt;

#ifdef _WIN32
    if (strcmp(func_name, "__C_specific_handler") == 0) {
        return (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "__C_specific_handler");
    }

    // If it's a Windows API, like KERNEL32$GetCurrentProcessId
    char *dll_sep = strchr(func_name, '$');
    if (dll_sep) {
        char dll_name[256] = {0};
        strncpy(dll_name, func_name, dll_sep - func_name);
        HMODULE hModule = LoadLibraryA(dll_name);
        if (hModule) {
            void *proc = (void *)GetProcAddress(hModule, dll_sep + 1);
            if (proc) return proc;
        }
    } else {
        // Fallback for standard libraries if no $ delimiter was provided
        void *proc = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), func_name);
        if (proc) return proc;
        proc = (void *)GetProcAddress(GetModuleHandleA("msvcrt.dll"), func_name);
        if (proc) return proc;
        proc = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), func_name);
        if (proc) return proc;
    }
#else
    // On non-Windows platforms, we simply mock external API addresses with a dummy pointer
    // so the dry-run simulation doesn't fail on relocation calculations.
    char *dll_sep = strchr(func_name, '$');
    if (dll_sep) {
        return (void *)0xDEADBEEF; // dummy pointer for dry-run
    }
#endif

    printf("[BOF Warning] Unresolved symbol: %s\n", name);
    return NULL;
}

int bof_run(const uint8_t *bof_data, size_t bof_size, const char *entrypoint, const uint8_t *args, size_t args_size) {
    coff_module_t module;
    int res = coff_load(&module, bof_data, bof_size, entrypoint, bof_resolver, NULL);
    
    if (res != 0) {
        return res;
    }

    coff_execute(&module, args, args_size);

    coff_free(&module);
    return 0;
}
