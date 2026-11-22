#include "monitoring.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int g_exit_flag = 0;
static int g_fds[MAX_FDS];
static void *g_ptrs[MAX_PTRS];
static cleanup_callback_t g_extra_cb = NULL;

static void handle_exit_signal(int signo) {
    (void)signo;
    g_exit_flag = 1;
    signal(SIGINT, handle_exit_signal);
    signal(SIGTERM, handle_exit_signal);
}

int sig_init(void) {
    g_exit_flag = 0;
    g_extra_cb = NULL;

    for (int i = 0; i < MAX_FDS; i++) {
        g_fds[i] = -1;
    }
    for (int i = 0; i < MAX_PTRS; i++) {
        g_ptrs[i] = NULL;
    }

    if (signal(SIGINT, handle_exit_signal) == SIG_ERR) {
        perror("signal(SIGINT)");
        return -1;
    }
    if (signal(SIGTERM, handle_exit_signal) == SIG_ERR) {
        perror("signal(SIGTERM)");
        return -1;
    }

    return 0;
}

int sig_should_exit(void) {
    return g_exit_flag ? 1 : 0;
}

int sig_add_fd(int fd) {
    if (fd < 0) {
        return -1;
    }
    for (int i = 0; i < MAX_FDS; i++) {
        if (g_fds[i] == fd) {
            return 0;
        }
    }
    for (int i = 0; i < MAX_FDS; i++) {
        if (g_fds[i] == -1) {
            g_fds[i] = fd;
            return 0;
        }
    }
    return -1;
}

void sig_close_fds(void) {
    for (int i = 0; i < MAX_FDS; i++) {
        if (g_fds[i] > STDERR_FILENO) {
            while (close(g_fds[i]) == -1 && errno == EINTR) {
            }
        }
        g_fds[i] = -1;
    }
}

int sig_add_ptr(void *ptr) {
    if (ptr == NULL) {
        return -1;
    }
    for (int i = 0; i < MAX_PTRS; i++) {
        if (g_ptrs[i] == ptr) {
            return 0;
        }
    }
    for (int i = 0; i < MAX_PTRS; i++) {
        if (g_ptrs[i] == NULL) {
            g_ptrs[i] = ptr;
            return 0;
        }
    }
    return -1;
}

void sig_free_ptrs(void) {
    for (int i = 0; i < MAX_PTRS; i++) {
        if (g_ptrs[i] != NULL) {
            free(g_ptrs[i]);
            g_ptrs[i] = NULL;
        }
    }
}

void sig_set_extra_cleanup(cleanup_callback_t cb) {
    g_extra_cb = cb;
}

void sig_cleanup_all(void) {
    sig_free_ptrs();
    sig_close_fds();
    if (g_extra_cb != NULL) {
        g_extra_cb();
    }
}