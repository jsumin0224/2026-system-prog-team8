#include "monitoring.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// 백그라운드 전환
int admin_daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid > 0) {
        _exit(0);
    }

    pid = fork();
    if (pid < 0) {
        perror("second fork");
        return -1;
    }
    if (pid > 0) {
        _exit(0);
    }

    if (chdir("/") < 0) {
        perror("chdir");
        return -1;
    }

    int dev_null = open("/dev/null", O_RDWR);
    if (dev_null >= 0) {
        dup2(dev_null, STDIN_FILENO);
        dup2(dev_null, STDOUT_FILENO);
        dup2(dev_null, STDERR_FILENO);
        if (dev_null > STDERR_FILENO) {
            close(dev_null);
        }
    }

    return 0;
}

// 관리자 tui
void admin_render_dashboard(const char *log_path, const char *proc_filter) {
    printf("\033[2J\033[H");
    fflush(stdout);

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[64] = "N/A";
    if (tm_info != NULL) {
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    }

    printf("============================================================\n");
    printf("              ATM 관리자 모니터링 대시보드                \n");
    printf("  갱신 시각: %-25s  (Ctrl+C 종료)\n", time_buf);
    printf("============================================================\n\n");

    // 로그 파일 상태, 최근 내역
    log_stat_info_t st_info;
    printf("[1] 거래 로그 상태 (%s)\n", log_path ? log_path : "미지정");
    if (monitor_check_log(log_path, &st_info) == 0 && st_info.exists) {
        printf("    크기: %ld bytes | 최종수정: %ld | 변화감지: %s\n",
               (long)st_info.size, (long)st_info.mtime,
               st_info.changed ? "YES (증가/변경됨)" : "NO");
    } else {
        printf("    상태: 로그 파일을 찾을 수 없거나 읽을 수 없음\n");
    }

    char lines[MAX_LOG_LINES][MAX_LINE_LEN];
    int n_lines = monitor_read_tail(log_path, lines, 5);
    printf("    최근 거래 내역 (%d줄):\n", n_lines > 0 ? n_lines : 0);
    if (n_lines > 0) {
        for (int i = 0; i < n_lines; i++) {
            printf("      - %s\n", lines[i]);
        }
    } else {
        printf("      (표시할 로그 내역 없음)\n");
    }
    printf("\n");

    // 활성 프로세스 목록
    proc_info_t procs[16];
    const char *filter = (proc_filter != NULL) ? proc_filter : "";
    int n_procs = monitor_find_procs_by_name(filter, procs, 16);
    printf("[2] 프로세스 조회 (필터: \"%s\") -> %d개\n",
           filter[0] ? filter : "전체", n_procs > 0 ? n_procs : 0);
    if (n_procs > 0) {
        int show_cnt = n_procs < 8 ? n_procs : 8;
        for (int i = 0; i < show_cnt; i++) {
            printf("      PID %-6d : %s\n", (int)procs[i].pid, procs[i].comm);
        }
        if (n_procs > 8) {
            printf("      ... 외 %d개\n", n_procs - 8);
        }
    }
    printf("\n");

    // 현재 접속 사용자 정보
    login_user_t users[16];
    int n_users = monitor_get_logged_in_users(users, 16);
    printf("[3] 현재 접속 사용자 -> %d명\n", n_users > 0 ? n_users : 0);
    if (n_users > 0) {
        for (int i = 0; i < n_users; i++) {
            printf("      %-16s (%s)\n", users[i].user, users[i].tty);
        }
    }

    printf("\n============================================================\n");
    fflush(stdout);
}

// 주기 제어 루프 (기본 1초 주기, sig_should_exit() 감지 시 종료)
int admin_run_loop(const char *log_path, const char *proc_filter, int interval_sec, int daemon_mode) {
    int period = (interval_sec > 0) ? interval_sec : 1;

    if (daemon_mode) {
        if (admin_daemonize() != 0) {
            return -1;
        }
        if (sig_init() != 0) {
            return -1;
        }
    }

    while (!sig_should_exit()) {
        if (!daemon_mode) {
            admin_render_dashboard(log_path, proc_filter);
        } else {
            log_stat_info_t st_info;
            monitor_check_log(log_path, &st_info);
        }

        for (int s = 0; s < period && !sig_should_exit(); s++) {
            sleep(1);
        }
    }

    if (!daemon_mode) {
        printf("\n관리자 모니터링을 종료합니다.\n");
    }
    sig_cleanup_all();
    return 0;
}
