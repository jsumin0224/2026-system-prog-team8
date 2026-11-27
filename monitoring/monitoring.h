#ifndef MONITORING_H
#define MONITORING_H

#include <stddef.h>
#include <sys/types.h>
#include <time.h>

#define MAX_LOG_LINES 10
#define MAX_LINE_LEN 256
#define MAX_ITEMS 32
#define MAX_FDS 64
#define MAX_PTRS 64

typedef void (*cleanup_callback_t)(void);

typedef struct {
    int exists;
    off_t size;
    time_t mtime;
    int changed;
} log_stat_info_t;

typedef struct {
    pid_t pid;
    char comm[64];
} proc_info_t;

typedef struct {
    char user[32];
    char tty[32];
} login_user_t;

// signal_handler.c 
int sig_init(void);
int sig_should_exit(void);
int sig_add_fd(int fd);
void sig_close_fds(void);
int sig_add_ptr(void *ptr);
void sig_free_ptrs(void);
void sig_set_extra_cleanup(cleanup_callback_t cb);
void sig_cleanup_all(void);

// monitor.c 
int monitor_check_log(const char *filepath, log_stat_info_t *out);
int monitor_list_dir(const char *dirpath, char names[][128], int max_names);
int monitor_read_tail(const char *filepath, char lines[][MAX_LINE_LEN], int max_lines);
int monitor_find_procs_by_name(const char *target_name, proc_info_t *list, int max_list);
int monitor_get_logged_in_users(login_user_t *users, int max_users);

//timer.c
#define DEFAULT_IDLE_TIMEOUT_SEC 30

int timer_init(int seconds);
void timer_reset(void);
int timer_is_timeout(void);
void timer_clear_timeout(void);

// admin.c 
void admin_render_dashboard(const char *log_path, const char *proc_filter);
int admin_run_loop(const char *log_path, const char *proc_filter, int interval_sec);

#endif
