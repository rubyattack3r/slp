#ifndef SLP_PLATFORM_H
#define SLP_PLATFORM_H

#include "slp_common.h"
#include "slp_core.h"
#include <time.h>
#include <stdio.h>

// Platform Abstraction Layer (PAL) for Sleep VM and Stdlib

// Sleep for specified milliseconds
void slp_platform_sleep_ms(unsigned int ms);

// Custom date parser (mimics POSIX strptime, returns NULL if unsupported or fails)
char *slp_platform_strptime(const char *buf, const char *format, struct tm *tm);

// Directory Iteration
typedef struct SlpPlatformDir SlpPlatformDir;

SlpPlatformDir *slp_platform_opendir(const char *path);
const char *slp_platform_readdir(SlpPlatformDir *dir);
void slp_platform_closedir(SlpPlatformDir *dir);

// Directory/File operations
int slp_platform_mkdir(const char *path);
int slp_platform_chdir(const char *path);
char *slp_platform_getcwd(char *buf, size_t size);
double slp_platform_file_size(const char *path);
double slp_platform_last_modified(const char *path);

// Path Formatting helpers
char slp_platform_path_separator(void);

// File / Socket / Process utilities
int slp_platform_close_socket(int fd);
int slp_platform_waitpid(int pid, int *status, int options);
int slp_platform_access(const char *path, int mode);
FILE *slp_platform_fdopen(int fd, const char *mode);

// Process spawning
typedef struct SlpPlatformProcess SlpPlatformProcess;

struct SlpPlatformProcess {
    FILE *read_stream;
    int write_fd;
    int pid;
};

// Spawns a command under a shell, redirecting standard input/output/error streams
SlpPlatformProcess *slp_platform_exec(const char *cmd, SlpAllocator *allocator);
void slp_platform_close_process(SlpPlatformProcess *proc);
void slp_platform_free_process(SlpPlatformProcess *proc, SlpAllocator *allocator);
int slp_platform_close_file(FILE *f, bool is_pipeline);

#endif // SLP_PLATFORM_H
