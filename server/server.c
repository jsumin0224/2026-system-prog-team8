#include "ipc.h"
#include "../database/account.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

_Static_assert(IPC_ACC_LEN == ACC_LEN && IPC_PASS_LEN == PASS_LEN,
               "ipc.h string lengths differ from account.h");

#define MAX_ATM 16

static void on_sigchld(int sig)
{
    (void)sig;
    int saved = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    errno = saved;
}

static const char *status_msg(int st)
{
    switch (st) {
    case ST_OK:            return "OK";
    case ST_NO_ACCOUNT:    return "account not found";
    case ST_BAD_PASSWORD:  return "wrong password";
    case ST_NO_FUNDS:      return "insufficient balance";
    case ST_TRANSFER_FAIL: return "transfer failed";
    default:               return "bad request";
    }
}

static void route(const Request *q, Response *r)
{
    Account acc;
    int st;

    memset(r, 0, sizeof *r);

    if (q->op < OP_LOGIN || q->op > OP_BALANCE)
        st = ST_BAD_REQUEST;
    else if ((q->op == OP_DEPOSIT || q->op == OP_WITHDRAW || q->op == OP_TRANSFER) && q->amount <= 0)
        st = ST_BAD_REQUEST;
    else if (q->op == OP_TRANSFER && strcmp(q->acc_num, q->target_acc) == 0)
        st = ST_BAD_REQUEST;
    else if (find_account(q->acc_num, &acc) != 0)
        st = ST_NO_ACCOUNT;
    else if (strcmp(acc.password, q->password) != 0)
        st = ST_BAD_PASSWORD;
    else {
        switch (q->op) {
        case OP_DEPOSIT:  st = process_deposit(q->acc_num, q->amount); break;
        case OP_WITHDRAW: st = process_withdraw(q->acc_num, q->amount, q->password); break;
        case OP_TRANSFER: st = process_transfer(q->acc_num, q->target_acc, q->amount, q->password); break;
        default:          st = ST_OK; break;
        }
        if (find_account(q->acc_num, &acc) == 0)
            r->balance = acc.balance;
    }

    r->status = st;
    snprintf(r->msg, sizeof r->msg, "%s", status_msg(st));
}

static void close_atm(struct pollfd *p, int *resp_w)
{
    close(p->fd);
    close(*resp_w);
    p->fd = -1;
}

/* ATM 자식 프로세스의 입출력을 지정한 터미널(tty)로 바꿉니다. */
static int attach_tty(const char *path)
{
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "server: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (!isatty(fd)) {
        fprintf(stderr, "server: %s is not a terminal\n", path);
        close(fd);
        return -1;
    }

    if (dup2(fd, STDIN_FILENO) < 0 ||
        dup2(fd, STDOUT_FILENO) < 0 ||
        dup2(fd, STDERR_FILENO) < 0) {
        perror("dup2");
        close(fd);
        return -1;
    }
    if (fd > STDERR_FILENO)
        close(fd);
    return 0;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [number of ATMs 1-%d]\n"
            "       %s /dev/pts/N [/dev/pts/M ...]   (ATM마다 별도 터미널 사용)\n",
            prog, MAX_ATM, prog);
}

int main(int argc, char **argv)
{
    const char *ttys[MAX_ATM] = { NULL };
    int n;

    /* 인자가 '/'로 시작하면 터미널 경로 목록, 아니면 ATM 개수입니다. */
    if (argc > 1 && argv[1][0] == '/') {
        n = argc - 1;
        if (n > MAX_ATM) {
            print_usage(argv[0]);
            return 1;
        }
        for (int i = 0; i < n; i++)
            ttys[i] = argv[i + 1];
    } else {
        n = argc > 1 ? atoi(argv[1]) : 3;
        if (n < 1 || n > MAX_ATM) {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (init_account_file() < 0)
        return 1;

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigchld;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    struct pollfd pfd[MAX_ATM];
    int resp_w[MAX_ATM];

    for (int i = 0; i < n; i++) {
        int req[2], resp[2];
        if (pipe(req) < 0 || pipe(resp) < 0) {
            perror("pipe");
            return 1;
        }
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid == 0) {
            for (int j = 0; j < i; j++) {
                close(pfd[j].fd);
                close(resp_w[j]);
            }
            close(req[0]);
            close(resp[1]);

            /* 터미널 경로가 주어졌으면 이 ATM의 입출력을 그 터미널로 옮깁니다. */
            if (ttys[i] != NULL && attach_tty(ttys[i]) < 0)
                _exit(1);

            atm_run(i + 1, req[1], resp[0]);
            close(req[1]);
            close(resp[0]);
            _exit(0);
        }
        close(req[1]);
        close(resp[0]);
        pfd[i].fd = req[0];
        pfd[i].events = POLLIN;
        resp_w[i] = resp[1];
    }

    int alive = n;
    while (alive > 0) {
        if (poll(pfd, n, -1) < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }
        for (int i = 0; i < n; i++) {
            if (pfd[i].fd < 0 || !(pfd[i].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;

            Request q;
            Response r;
            int rc = read_full(pfd[i].fd, &q, sizeof q);
            if (rc <= 0) {
                if (rc < 0)
                    fprintf(stderr, "server: ATM%d bad packet, closing connection\n", i + 1);
                close_atm(&pfd[i], &resp_w[i]);
                alive--;
                continue;
            }

            q.acc_num[IPC_ACC_LEN - 1] = '\0';
            q.password[IPC_PASS_LEN - 1] = '\0';
            q.target_acc[IPC_ACC_LEN - 1] = '\0';
            q.atm_id = i + 1;

            route(&q, &r);
            if (write_full(resp_w[i], &r, sizeof r) < 0) {
                fprintf(stderr, "server: ATM%d response failed, closing connection\n", i + 1);
                close_atm(&pfd[i], &resp_w[i]);
                alive--;
            }
        }
    }

    while (wait(NULL) > 0)
        ;
    return 0;
}
