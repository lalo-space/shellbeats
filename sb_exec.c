#define _POSIX_C_SOURCE 200809L

#include "sb_exec.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct sb_proc {
    FILE *fp;
    pid_t pid;
    struct sb_proc *next;
} sb_proc;

static sb_proc *g_procs = NULL;
static pthread_mutex_t g_procs_lock = PTHREAD_MUTEX_INITIALIZER;

static void redirect_to_null(int fd) {
    int nfd = open("/dev/null", O_RDWR);
    if (nfd < 0) return;
    if (nfd != fd) {
        dup2(nfd, fd);
        close(nfd);
    }
}

int sb_exec_status(char *const argv[], int silent) {
    if (!argv || !argv[0]) return -1;

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (silent) {
            redirect_to_null(STDOUT_FILENO);
            redirect_to_null(STDERR_FILENO);
        }
        execvp(argv[0], argv);
        _exit(127);
    }

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

FILE *sb_exec_popen(char *const argv[], int merge_stderr) {
    if (!argv || !argv[0]) return NULL;

    int pipefd[2];
    if (pipe(pipefd) < 0) return NULL;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }
    if (pid == 0) {
        close(pipefd[0]);
        if (pipefd[1] != STDOUT_FILENO) {
            dup2(pipefd[1], STDOUT_FILENO);
        }
        if (merge_stderr) {
            dup2(STDOUT_FILENO, STDERR_FILENO);
        } else {
            redirect_to_null(STDERR_FILENO);
        }
        if (pipefd[1] != STDOUT_FILENO) close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);
        int status;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        return NULL;
    }

    sb_proc *entry = malloc(sizeof(*entry));
    if (!entry) {
        fclose(fp);
        int status;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        return NULL;
    }
    entry->fp = fp;
    entry->pid = pid;

    pthread_mutex_lock(&g_procs_lock);
    entry->next = g_procs;
    g_procs = entry;
    pthread_mutex_unlock(&g_procs_lock);

    return fp;
}

int sb_exec_pclose(FILE *fp) {
    if (!fp) return -1;

    pid_t pid = -1;
    pthread_mutex_lock(&g_procs_lock);
    sb_proc **pp = &g_procs;
    while (*pp) {
        if ((*pp)->fp == fp) {
            sb_proc *entry = *pp;
            *pp = entry->next;
            pid = entry->pid;
            free(entry);
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&g_procs_lock);

    fclose(fp);

    if (pid < 0) return -1;

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

int sb_parse_cookie_args(const char *src, char **vec, int *argc, int vec_max) {
    if (!src || !src[0]) return 0;
    if (!vec || !argc) return -1;

    while (*src == ' ') src++;
    if (!*src) return 0;

    const char *flag_end = src;
    while (*flag_end && *flag_end != ' ') flag_end++;
    if (flag_end == src) return -1;

    size_t flag_len = (size_t)(flag_end - src);
    const char *p = flag_end;
    while (*p == ' ') p++;
    if (!*p) return -1;

    const char *val_start;
    const char *val_end;
    if (*p == '\'') {
        val_start = p + 1;
        val_end = strchr(val_start, '\'');
        if (!val_end) return -1;
    } else {
        val_start = p;
        val_end = val_start;
        while (*val_end && *val_end != ' ') val_end++;
    }
    size_t val_len = (size_t)(val_end - val_start);

    if (*argc + 2 > vec_max) return -1;

    char *flag = malloc(flag_len + 1);
    char *val = malloc(val_len + 1);
    if (!flag || !val) { free(flag); free(val); return -1; }
    memcpy(flag, src, flag_len); flag[flag_len] = '\0';
    memcpy(val, val_start, val_len); val[val_len] = '\0';

    vec[(*argc)++] = flag;
    vec[(*argc)++] = val;
    return 0;
}

void sb_free_cookie_args(char **vec, int from, int to) {
    if (!vec) return;
    for (int i = from; i < to; i++) {
        free(vec[i]);
        vec[i] = NULL;
    }
}
