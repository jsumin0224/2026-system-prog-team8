#include "monitoring.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utmp.h>

static int g_has_prev_stat = 0;
static off_t g_prev_size = 0;
static time_t g_prev_mtime = 0;

/* 1. stat()으로 거래 로그 파일 크기 및 수정시간 감시 */
int monitor_check_log(const char *filepath, log_stat_info_t *out) {
    if (filepath == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    struct stat st;
    if (stat(filepath, &st) == -1) {
        out->exists = 0;
        return -1;
    }

    out->exists = 1;
    out->size = st.st_size;
    out->mtime = st.st_mtime;

    if (g_has_prev_stat && (st.st_size != g_prev_size || st.st_mtime != g_prev_mtime)) {
        out->changed = 1;
    } else {
        out->changed = 0;
    }

    g_prev_size = st.st_size;
    g_prev_mtime = st.st_mtime;
    g_has_prev_stat = 1;
    return 0;
}

/* 2a. opendir / readdir (ls 원리)로 디렉토리 파일 목록 조회 */
int monitor_list_dir(const char *dirpath, char names[][128], int max_names) {
    if (dirpath == NULL || names == NULL || max_names <= 0) {
        return -1;
    }

    DIR *dir = opendir(dirpath);
    if (dir == NULL) {
        perror("opendir");
        return -1;
    }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < max_names) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        snprintf(names[count], 128, "%s", ent->d_name);
        count++;
    }

    closedir(dir);
    return count;
}

/* 2b. lseek / read로 파일 끝에서부터 거슬러 올라가며 최근 N줄 읽기 (tail 원리) */
int monitor_read_tail(const char *filepath, char lines[][MAX_LINE_LEN], int max_lines) {
    if (filepath == NULL || lines == NULL || max_lines <= 0) {
        return -1;
    }

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    off_t pos = lseek(fd, 0, SEEK_END);
    if (pos < 0) {
        close(fd);
        return -1;
    }

    char ch;
    int newline_seen = 0;

    if (pos > 0) {
        if (lseek(fd, pos - 1, SEEK_SET) >= 0 && read(fd, &ch, 1) == 1 && ch == '\n') {
            pos--;
        }
    }

    while (pos > 0 && newline_seen < max_lines) {
        pos--;
        if (lseek(fd, pos, SEEK_SET) < 0) {
            break;
        }
        if (read(fd, &ch, 1) == 1 && ch == '\n') {
            newline_seen++;
            if (newline_seen == max_lines) {
                pos++;
                break;
            }
        }
    }

    if (lseek(fd, pos, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }

    int count = 0;
    int line_len = 0;
    char raw_buf[512];
    ssize_t nread;

    while (count < max_lines && (nread = read(fd, raw_buf, sizeof(raw_buf))) > 0) {
        for (ssize_t i = 0; i < nread && count < max_lines; i++) {
            if (raw_buf[i] == '\n') {
                lines[count][line_len] = '\0';
                count++;
                line_len = 0;
            } else if (raw_buf[i] != '\r' && line_len < MAX_LINE_LEN - 1) {
                lines[count][line_len++] = raw_buf[i];
            }
        }
    }

    if (count < max_lines && line_len > 0) {
        lines[count][line_len] = '\0';
        count++;
    }

    close(fd);
    return count;
}

static int is_all_digits(const char *s) {
    if (s == NULL || *s == '\0') {
        return 0;
    }
    for (const char *p = s; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
    }
    return 1;
}

/* 3a. /proc 디렉토리 스캔으로 프로세스 이름과 PID 탐색 */
int monitor_find_procs_by_name(const char *target_name, proc_info_t *list, int max_list) {
    if (list == NULL || max_list <= 0) {
        return -1;
    }

    DIR *proc_dir = opendir("/proc");
    if (proc_dir == NULL) {
        perror("opendir(/proc)");
        return -1;
    }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(proc_dir)) != NULL && count < max_list) {
        if (!is_all_digits(ent->d_name)) {
            continue;
        }

        char comm_path[256];
        snprintf(comm_path, sizeof(comm_path), "/proc/%s/comm", ent->d_name);

        int fd = open(comm_path, O_RDONLY);
        if (fd < 0) {
            continue;
        }

        char comm[64];
        memset(comm, 0, sizeof(comm));
        ssize_t nread = read(fd, comm, sizeof(comm) - 1);
        close(fd);

        if (nread > 0) {
            for (ssize_t i = 0; i < nread; i++) {
                if (comm[i] == '\n' || comm[i] == '\r') {
                    comm[i] = '\0';
                    break;
                }
            }
            if (target_name == NULL || target_name[0] == '\0' ||
                strstr(comm, target_name) != NULL) {
                list[count].pid = (pid_t)atoi(ent->d_name);
                snprintf(list[count].comm, sizeof(list[count].comm), "%s", comm);
                count++;
            }
        }
    }

    closedir(proc_dir);
    return count;
}

/* 3b. utmp 파일 open / read (who 명령어 원리)로 현재 접속 사용자 조회 */
int monitor_get_logged_in_users(login_user_t *users, int max_users) {
    if (users == NULL || max_users <= 0) {
        return -1;
    }

    int fd = open(UTMP_FILE, O_RDONLY);
    if (fd < 0) {
        perror(UTMP_FILE);
        return -1;
    }

    struct utmp rec;
    int count = 0;

    while (count < max_users && read(fd, &rec, sizeof(rec)) == (ssize_t)sizeof(rec)) {
        if (rec.ut_type == USER_PROCESS && rec.ut_user[0] != '\0') {
            snprintf(users[count].user, sizeof(users[count].user), "%.*s",
                     (int)sizeof(rec.ut_user), rec.ut_user);
            snprintf(users[count].tty, sizeof(users[count].tty), "%.*s",
                     (int)sizeof(rec.ut_line), rec.ut_line);
            count++;
        }
    }

    close(fd);
    return count;
}