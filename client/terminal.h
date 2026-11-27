#ifndef TERMINAL_H
#define TERMINAL_H

#include <stddef.h>

typedef enum {
    INPUT_OK,
    INPUT_INVALID,
    INPUT_END,
    INPUT_ERROR,
    INPUT_TIMEOUT
} InputResult;

/* 종료 신호와 자동 로그아웃 타이머를 설정합니다. */
int terminal_init(void);

/* 종료 신호가 들어왔으면 1을 반환합니다. */
int terminal_interrupted(void);

/* 1이면 입력 대기 중 일정 시간 입력이 없을 때 INPUT_TIMEOUT을 반환합니다. 0이면 끕니다. */
void terminal_idle_enable(int enable);

/* Enter 없이 한 글자를 읽고, 성공하면 *key에 저장합니다. */
InputResult terminal_key(const char *label, char *key);

/* Enter까지 읽습니다. hidden이 1이면 입력을 숨깁니다. */
InputResult terminal_line(const char *label, char *buffer,
                          size_t capacity, int hidden);

#endif
