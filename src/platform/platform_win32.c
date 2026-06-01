#include "slp_platform.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <sys/stat.h>

void slp_platform_sleep_ms(unsigned int ms) {
    Sleep(ms);
}

char *slp_platform_strptime(const char *buf, const char *format, struct tm *tm) {
    (void)buf; (void)format; (void)tm;
    return NULL; // strptime is not supported natively on Windows/MinGW without custom implementation
}

struct SlpPlatformDir {
    HANDLE hFind;
    WIN32_FIND_DATAA find_data;
    bool first;
    char next_name[MAX_PATH];
};

SlpPlatformDir *slp_platform_opendir(const char *path) {
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", path);
    
    SlpPlatformDir *pdir = (SlpPlatformDir *)malloc(sizeof(SlpPlatformDir));
    if (!pdir) return NULL;
    
    pdir->hFind = FindFirstFileA(search_path, &pdir->find_data);
    if (pdir->hFind == INVALID_HANDLE_VALUE) {
        free(pdir);
        return NULL;
    }
    pdir->first = true;
    return pdir;
}

const char *slp_platform_readdir(SlpPlatformDir *dir) {
    if (!dir || dir->hFind == INVALID_HANDLE_VALUE) return NULL;
    
    if (dir->first) {
        dir->first = false;
        return dir->find_data.cFileName;
    }
    
    if (FindNextFileA(dir->hFind, &dir->find_data)) {
        return dir->find_data.cFileName;
    }
    
    return NULL;
}

void slp_platform_closedir(SlpPlatformDir *dir) {
    if (dir) {
        if (dir->hFind != INVALID_HANDLE_VALUE) {
            FindClose(dir->hFind);
        }
        free(dir);
    }
}

int slp_platform_mkdir(const char *path) {
    return CreateDirectoryA(path, NULL) ? 0 : -1;
}

int slp_platform_chdir(const char *path) {
    return SetCurrentDirectoryA(path) ? 0 : -1;
}

char *slp_platform_getcwd(char *buf, size_t size) {
    if (GetCurrentDirectoryA((DWORD)size, buf) > 0) {
        return buf;
    }
    return NULL;
}

char slp_platform_path_separator(void) {
    return '\\';
}

int slp_platform_close_socket(int fd) {
    return closesocket((SOCKET)fd);
}

int slp_platform_waitpid(int pid, int *status, int options) {
    (void)pid; (void)status; (void)options;
    return 0; // waitpid is not directly applicable to arbitrary pids on Windows POPEN
}

int slp_platform_access(const char *path, int mode) {
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return -1;
    
    if (mode == 0) return 0; // F_OK
    
    if (mode & 2) { // W_OK
        if (attr & FILE_ATTRIBUTE_READONLY) return -1;
    }
    
    return 0;
}

FILE *slp_platform_fdopen(int fd, const char *mode) {
    return _fdopen(fd, mode);
}

SlpPlatformProcess *slp_platform_exec(const char *cmd, SlpAllocator *allocator) {
    FILE *f = _popen(cmd, "r");
    if (!f) return NULL;

    SlpPlatformProcess *proc = (SlpPlatformProcess *)SLP_ALLOCATE(allocator, SlpPlatformProcess);
    if (!proc) {
        _pclose(f);
        return NULL;
    }

    proc->read_stream = f;
    proc->write_fd = -1;
    proc->pid = -1;

    return proc;
}

void slp_platform_close_process(SlpPlatformProcess *proc) {
    if (proc) {
        if (proc->read_stream) {
            _pclose(proc->read_stream);
        }
    }
}

void slp_platform_free_process(SlpPlatformProcess *proc, SlpAllocator *allocator) {
    if (proc) {
        SLP_FREE(allocator, proc);
    }
}

int slp_platform_close_file(FILE *f, bool is_pipeline) {
    if (is_pipeline) {
        return _pclose(f);
    }
    return fclose(f);
}

double slp_platform_file_size(const char *path) {
    struct _stat st;
    if (_stat(path, &st) == 0) {
        return (double)st.st_size;
    }
    return 0.0;
}

double slp_platform_last_modified(const char *path) {
    struct _stat st;
    if (_stat(path, &st) == 0) {
        return (double)st.st_mtime * 1000.0;
    }
    return 0.0;
}
