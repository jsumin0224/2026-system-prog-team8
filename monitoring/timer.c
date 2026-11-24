#include "monitoring.h"

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static int g_timeout_flag = 0;
static int g_timeout_sec = DEFAULT_IDLE_TIMEOUT_SEC;

static void handle_sigalrm(int signo) {
    (void)signo;
    g_timeout_flag = 1;
    signal(SIGALRM, handle_sigalrm);
}

int timer_init(int seconds) {
    if (seconds > 0) {
        g_timeout_sec = seconds;
    } else {
        g_timeout_sec = DEFAULT_IDLE_TIMEOUT_SEC;
    }
    g_timeout_flag = 0;

    if (signal(SIGALRM, handle_sigalrm) == SIG_ERR) {
        perror("signal(SIGALRM)");
        return -1;
    }

    return 0;
}

// 사용자 입력이 있을 때마다 호출해서 타이머 리셋 
void timer_reset(void) {
    g_timeout_flag = 0;
    alarm((unsigned int)g_timeout_sec);
}

// 1이면 자동 로그아웃
int timer_is_timeout(void) {
    return g_timeout_flag ? 1 : 0;
}

// 자동 로그아웃 후 플래그 초기화
void timer_clear_timeout(void) {
    g_timeout_flag = 0;
}
