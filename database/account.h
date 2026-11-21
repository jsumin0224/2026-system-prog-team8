#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define ACCOUNT_FILE "accounts.dat"
#define TRANSACTION_FILE "transactions.log"

#define ACC_LEN 20
#define PASS_LEN 20
#define NAME_LEN 30

// 계좌 정보 구조체
typedef struct {
    char acc_num[ACC_LEN];       // 계좌번호
    char password[PASS_LEN];     // 비밀번호
    char owner[NAME_LEN];        // 예금주 이름
    long balance;                // 잔액
    int is_active;               // 활성화 여부 (1: 정상, 0: 해지)
} Account;

// 거래 내역 구조체
typedef struct {
    char timestamp[30];          // 거래 일시
    char acc_num[ACC_LEN];       // 대상 계좌
    char type[20];               // 거래 종류 (DEPOSIT, WITHDRAW, TRANSFER)
    long amount;                 // 거래 금액
    long balance_after;          // 거래 후 잔액
    char target_acc[ACC_LEN];    // 이체 시 상대 계좌
} Transaction;

// 함수 프로토타입 선언
int lock_file(int fd, int cmd, int type, off_t offset, int whence, off_t len);
int init_account_file();
int find_account(const char *acc_num, Account *out_acc);
int process_deposit(const char *acc_num, long amount);
int process_withdraw(const char *acc_num, long amount, const char *password);
int process_transfer(const char *from_acc, const char *to_acc, long amount, const char *password);
int log_transaction(const Transaction *tx);

#endif
