#include "terminal.h"
#include "../server/ipc.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* prompt()가 입력 시간 초과일 때 반환하는 값입니다. */
#define PROMPT_TIMEOUT (-2)

static int login(int req_fd, int resp_fd, Request *request);
static int run_session(int req_fd, int resp_fd, Request *request);
static int session_timeout(void);
static int prompt(const char *label, char *buffer, size_t capacity, int hidden);
static int read_amount(long *amount);
static int call_server(int req_fd, int resp_fd,
                       const Request *request, Response *response);
static void show_transaction_result(const Request *request,
                                    const Response *response);

/* 서버가 ATM 자식 프로세스에서 호출하는 시작 함수입니다. */
void atm_run(int atm_id, int req_fd, int resp_fd)
{
    if (terminal_init() == -1) {
        perror("terminal_init");
        return;
    }

    while (!terminal_interrupted()) {
        Request request = { .atm_id = atm_id };

        /* 1. 서버에서 로그인이 성공해야 거래 메뉴를 엽니다. */
        int result = login(req_fd, resp_fd, &request);
        if (result == 1) {
            /* 로그인 후에만 자동 로그아웃 타이머를 켭니다. */
            terminal_idle_enable(1);
            result = run_session(req_fd, resp_fd, &request);
            terminal_idle_enable(0);

            if (result == 0)
                puts("로그아웃했습니다.");
        }

        /* 현재 IPC는 거래마다 비밀번호를 요구합니다. 세션이 끝나면 지웁니다. */
        volatile char *password = request.password;
        for (size_t i = 0; i < sizeof request.password; ++i)
            password[i] = 0;

        if (result < 0)
            break;
    }

    puts("\nATM을 종료합니다.");
    fflush(stdout);
}

/* 1은 로그인 성공, 0은 다시 입력, -1은 ATM 종료입니다. */
static int login(int req_fd, int resp_fd, Request *request)
{
    printf("\n=== ATM %d 로그인 ===\n", request->atm_id);

    int result = prompt("계좌번호 (0: 종료): ", request->acc_num,
                        sizeof request->acc_num, 0);
    if (result <= 0)
        return result == PROMPT_TIMEOUT ? 0 : result;
    if (strcmp(request->acc_num, "0") == 0)
        return -1;

    result = prompt("비밀번호: ", request->password,
                    sizeof request->password, 1);
    if (result <= 0)
        return result == PROMPT_TIMEOUT ? 0 : result;
    if (terminal_interrupted())
        return -1;

    request->op = OP_LOGIN;
    Response response;

    if (call_server(req_fd, resp_fd, request, &response) < 0)
        return -1;

    if (response.status != ST_OK) {
        puts("로그인 실패. 계좌번호와 비밀번호를 확인하세요.");
        return 0;
    }

    puts("로그인했습니다.");
    return 1;
}

/* 자동 로그아웃 안내를 출력하고 1을 반환합니다. */
static int session_timeout(void)
{
    puts("\n입력 시간이 초과되어 자동 로그아웃되었습니다.");
    return 1;
}

/* 0은 로그아웃, 1은 자동 로그아웃, -1은 ATM 종료입니다. */
static int run_session(int req_fd, int resp_fd, Request *request)
{
    /* 5. 로그아웃이나 종료가 발생할 때까지 반복합니다. */
    while (!terminal_interrupted()) {
        /* 2. 거래 메뉴를 출력합니다. */
        puts("\n=== 거래 메뉴 ===");
        puts("1. 입금");
        puts("2. 출금");
        puts("3. 계좌이체");
        puts("4. 잔액 조회");
        puts("0. 로그아웃 (OUT)");

        /* 3. terminal_key()가 설정을 바꾸고 한 키를 읽은 후 복구합니다. */
        char key;
        InputResult input = terminal_key("메뉴 선택: ", &key);
        putchar('\n');

        if (input == INPUT_TIMEOUT)
            return session_timeout();
        if (input == INPUT_ERROR)
            fputs("터미널 입력을 처리할 수 없습니다.\n", stderr);
        if (input != INPUT_OK)
            return -1;

        /* 4. 선택한 메뉴에 필요한 거래 정보를 채웁니다. */
        request->amount = 0;
        request->target_acc[0] = '\0';

        switch (key) {
        case '1':
            request->op = OP_DEPOSIT;
            break;
        case '2':
            request->op = OP_WITHDRAW;
            break;
        case '3':
            request->op = OP_TRANSFER;
            break;
        case '4':
            request->op = OP_BALANCE;
            break;
        case '0':
            return 0;
        default:
            puts("0부터 4까지의 메뉴 번호를 누르세요.");
            continue;
        }

        if (request->op == OP_TRANSFER) {
            int result = prompt("받는 계좌번호: ", request->target_acc,
                                sizeof request->target_acc, 0);
            if (result == PROMPT_TIMEOUT)
                return session_timeout();
            if (result < 0)
                return -1;
            if (result == 0)
                continue;

            if (strcmp(request->acc_num, request->target_acc) == 0) {
                puts("본인 계좌로는 이체할 수 없습니다.");
                continue;
            }
        }

        if (request->op != OP_BALANCE) {
            int result = read_amount(&request->amount);
            if (result == PROMPT_TIMEOUT)
                return session_timeout();
            if (result < 0)
                return -1;
            if (result == 0)
                continue;
        }

        Response response;
        if (terminal_interrupted() ||
            call_server(req_fd, resp_fd, request, &response) < 0)
            return -1;

        show_transaction_result(request, &response);
    }

    return -1;
}

/* 1은 정상 입력, 0은 잘못된 입력, -1은 종료 또는 오류, PROMPT_TIMEOUT은 시간 초과입니다. */
static int prompt(const char *label, char *buffer, size_t capacity, int hidden)
{
    InputResult result = terminal_line(label, buffer, capacity, hidden);

    if (result == INPUT_TIMEOUT)
        return PROMPT_TIMEOUT;
    if (result == INPUT_ERROR)
        fputs("터미널 입력을 처리할 수 없습니다.\n", stderr);
    if (result == INPUT_END || result == INPUT_ERROR)
        return -1;

    if (result == INPUT_INVALID || buffer[0] == '\0') {
        printf("1~%zu바이트로 입력하세요.\n", capacity - 1);
        return 0;
    }
    return 1;
}

static int read_amount(long *amount)
{
    char input[32];
    int result = prompt("금액(원): ", input, sizeof input, 0);
    if (result <= 0)
        return result;

    errno = 0;
    long value = strtol(input, NULL, 10);

    if (input[strspn(input, "0123456789")] != '\0' ||
        errno == ERANGE || value <= 0 || value > INT_MAX) {
        printf("금액은 1~%d 사이의 정수로 입력하세요.\n", INT_MAX);
        return 0;
    }

    *amount = value;
    return 1;
}

static int call_server(int req_fd, int resp_fd,
                       const Request *request, Response *response)
{
    if (ipc_call(req_fd, resp_fd, request, response) == 0)
        return 0;

    fputs("서버와의 연결이 끊겼습니다.\n", stderr);
    return -1;
}

static void show_transaction_result(const Request *request,
                                    const Response *response)
{
    if (response->status == ST_NO_FUNDS) {
        puts("잔액이 부족합니다.");
        return;
    }

    if (response->status == ST_TRANSFER_FAIL) {
        puts("이체에 실패했습니다. 받는 계좌를 확인하세요.");
        return;
    }

    if (response->status != ST_OK) {
        printf("거래 실패: %.*s\n", IPC_MSG_LEN, response->msg);
        return;
    }

    switch (request->op) {
    case OP_DEPOSIT:
        printf("%ld원을 입금했습니다.\n", request->amount);
        break;
    case OP_WITHDRAW:
        printf("%ld원을 출금했습니다.\n", request->amount);
        break;
    case OP_TRANSFER:
        printf("%s 계좌에 %ld원을 이체했습니다.\n",
               request->target_acc, request->amount);
        break;
    }

    printf("현재 잔액: %ld원\n", response->balance);
}
