#define _POSIX_C_SOURCE 200809L

#include "terminal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;
static sigset_t exit_signals;

static void on_signal(int signal_number)
{
    (void)signal_number;
    interrupted = 1;
}

int terminal_init(void)
{
    const int signals[] = { SIGINT, SIGTERM, SIGHUP };
    struct sigaction action = {0};

    sigemptyset(&exit_signals);
    sigemptyset(&action.sa_mask);
    action.sa_handler = on_signal;

    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; ++i) {
        sigaddset(&exit_signals, signals[i]);
        if (sigaction(signals[i], &action, NULL) == -1)
            return -1;
    }

    return 0;
}

int terminal_interrupted(void)
{
    return interrupted != 0;
}

/* 입력을 기다리는 중에 Ctrl+C가 들어오면 대기를 중단합니다. */
static InputResult read_byte(char *byte, const sigset_t *wait_mask)
{
    while (!interrupted) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(STDIN_FILENO, &readable);

        if (pselect(STDIN_FILENO + 1, &readable, NULL, NULL, NULL,
                    wait_mask) == -1) {
            if (errno == EINTR)
                continue;
            return INPUT_ERROR;
        }

        if (interrupted)
            return INPUT_END;

        ssize_t count = read(STDIN_FILENO, byte, 1);
        if (count == 1)
            return INPUT_OK;
        if (count == 0)
            return INPUT_END;
        if (errno != EINTR)
            return INPUT_ERROR;
    }

    return INPUT_END;
}

static InputResult read_input(const char *label, char *buffer,
                              size_t capacity, int single, int hidden)
{
    if (capacity == 0)
        return INPUT_ERROR;

    struct termios original;
    sigset_t blocked = exit_signals;
    sigset_t previous_mask;
    int changed = 0;
    int tty = isatty(STDIN_FILENO);
    size_t length = 0;
    int invalid = 0;
    InputResult result = INPUT_ERROR;

    /* 터미널 설정을 복구할 때까지 Ctrl+Z에 의한 중단을 미룹니다. */
    if (tty && (single || hidden))
        sigaddset(&blocked, SIGTSTP);

    if (sigprocmask(SIG_BLOCK, &blocked, &previous_mask) == -1)
        return INPUT_ERROR;

    if (tty && (single || hidden)) {
        if (tcgetattr(STDIN_FILENO, &original) == -1)
            goto done;

        struct termios mode = original;
        mode.c_lflag &= ~(ECHO | ECHONL);

        if (single) {
            mode.c_lflag &= ~ICANON;
            mode.c_cc[VMIN] = 1;
            mode.c_cc[VTIME] = 0;
        }

        if (tcsetattr(STDIN_FILENO, TCSANOW, &mode) == -1)
            goto done;
        changed = 1;
    }

    /* 비밀번호 프롬프트는 에코를 끈 뒤에 표시합니다. */
    fputs(label, stdout);
    fflush(stdout);

    sigset_t wait_mask = previous_mask;
    if (changed)
        sigaddset(&wait_mask, SIGTSTP);

    for (;;) {
        char byte;
        result = read_byte(&byte, &wait_mask);

        if (result != INPUT_OK) {
            if (!tty && !single && result == INPUT_END &&
                length > 0 && !interrupted) {
                result = invalid ? INPUT_INVALID : INPUT_OK;
            }
            break;
        }

        if (single) {
            if (byte == '\n' || byte == '\r' ||
                byte == ' ' || byte == '\t')
                continue;

            if (changed && (unsigned char)byte == original.c_cc[VEOF]) {
                result = INPUT_END;
                break;
            }

            *buffer = byte;
            break;
        }

        if (byte == '\n') {
            result = invalid ? INPUT_INVALID : INPUT_OK;
            break;
        }

        if (!tty && byte == '\r')
            continue;

        /* 입력이 너무 길어도 다음 입력에 섞이지 않도록 줄 끝까지 읽습니다. */
        if (byte == '\0' || length == capacity - 1)
            invalid = 1;
        else
            buffer[length++] = byte;
    }

    if (!single)
        buffer[length] = '\0';

done:
    if (changed && tcsetattr(STDIN_FILENO, TCSANOW, &original) == -1)
        result = INPUT_ERROR;
    if (sigprocmask(SIG_SETMASK, &previous_mask, NULL) == -1)
        result = INPUT_ERROR;

    if (hidden && changed) {
        putchar('\n');
        fflush(stdout);
    }

    if (interrupted && result != INPUT_ERROR)
        return INPUT_END;
    return result;
}

InputResult terminal_key(const char *label, char *key)
{
    return read_input(label, key, 1, 1, 0);
}

InputResult terminal_line(const char *label, char *buffer,
                          size_t capacity, int hidden)
{
    return read_input(label, buffer, capacity, 0, hidden);
}