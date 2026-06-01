#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "slp_platform.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

void slp_platform_sleep_ms(unsigned int ms) {
    usleep(ms * 1000);
}

char *slp_platform_strptime(const char *buf, const char *format, struct tm *tm) {
    return strptime(buf, format, tm);
}

struct SlpPlatformDir {
    DIR *dir;
};

SlpPlatformDir *slp_platform_opendir(const char *path) {
    DIR *d = opendir(path);
    if (!d) return NULL;
    
    // We allocate directly using standard malloc since this is platform layer,
    // but to be consistent with AGENTS.md we can avoid standard malloc where possible,
    // though the platform abstraction layer itself handles standard OS handles.
    // Let's use standard malloc since it's an internal platform handle.
    SlpPlatformDir *pdir = (SlpPlatformDir *)malloc(sizeof(SlpPlatformDir));
    if (!pdir) {
        closedir(d);
        return NULL;
    }
    pdir->dir = d;
    return pdir;
}

const char *slp_platform_readdir(SlpPlatformDir *dir) {
    if (!dir || !dir->dir) return NULL;
    struct dirent *entry = readdir(dir->dir);
    if (!entry) return NULL;
    return entry->d_name;
}

void slp_platform_closedir(SlpPlatformDir *dir) {
    if (dir) {
        if (dir->dir) {
            closedir(dir->dir);
        }
        free(dir);
    }
}

int slp_platform_mkdir(const char *path) {
    return mkdir(path, 0777);
}

int slp_platform_chdir(const char *path) {
    return chdir(path);
}

char *slp_platform_getcwd(char *buf, size_t size) {
    return getcwd(buf, size);
}

char slp_platform_path_separator(void) {
    return '/';
}

int slp_platform_close_socket(int fd) {
    return close(fd);
}

int slp_platform_waitpid(int pid, int *status, int options) {
    // Translate standard options
    int flags = 0;
    if (options != 0) {
        flags = WNOHANG;
    }
    return waitpid(pid, status, flags);
}

int slp_platform_access(const char *path, int mode) {
    // Translate mode to standard POSIX access modes
    int amode = 0;
    if (mode == 0) { // F_OK
        amode = F_OK;
    } else {
        if (mode & 4) amode |= R_OK;
        if (mode & 2) amode |= W_OK;
        if (mode & 1) amode |= X_OK;
    }
    return access(path, amode);
}

FILE *slp_platform_fdopen(int fd, const char *mode) {
    return fdopen(fd, mode);
}

SlpPlatformProcess *slp_platform_exec(const char *cmd, SlpAllocator *allocator) {
    int pipe_in[2];
    int pipe_out[2];
    if (pipe(pipe_in) < 0) return NULL;
    if (pipe(pipe_out) < 0) {
        close(pipe_in[0]);
        close(pipe_in[1]);
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_in[0]); close(pipe_in[1]);
        close(pipe_out[0]); close(pipe_out[1]);
        return NULL;
    }

    if (pid == 0) {
        // Child process
        dup2(pipe_in[0], STDIN_FILENO);
        dup2(pipe_out[1], STDOUT_FILENO);
        dup2(pipe_out[1], STDERR_FILENO);

        close(pipe_in[0]); close(pipe_in[1]);
        close(pipe_out[0]); close(pipe_out[1]);

        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        exit(127);
    }

    // Parent process
    close(pipe_in[0]);
    close(pipe_out[1]);

    FILE *f = fdopen(pipe_out[0], "r");
    if (!f) {
        close(pipe_in[1]);
        close(pipe_out[0]);
        return NULL;
    }

    // Allocate process structure using VM allocator
    SlpPlatformProcess *proc = (SlpPlatformProcess *)SLP_ALLOCATE(allocator, SlpPlatformProcess);
    if (!proc) {
        fclose(f);
        close(pipe_in[1]);
        return NULL;
    }

    proc->read_stream = f;
    proc->write_fd = pipe_in[1];
    proc->pid = pid;

    return proc;
}

void slp_platform_close_process(SlpPlatformProcess *proc) {
    if (proc) {
        if (proc->read_stream) {
            fclose(proc->read_stream);
        }
        if (proc->write_fd != -1) {
            close(proc->write_fd);
        }
    }
}

void slp_platform_free_process(SlpPlatformProcess *proc, SlpAllocator *allocator) {
    if (proc) {
        SLP_FREE(allocator, proc);
    }
}

int slp_platform_close_file(FILE *f, bool is_pipeline) {
    (void)is_pipeline;
    return fclose(f);
}

double slp_platform_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return (double)st.st_size;
    }
    return 0.0;
}

double slp_platform_last_modified(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return (double)st.st_mtime * 1000.0;
    }
    return 0.0;
}
