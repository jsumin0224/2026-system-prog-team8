#ifndef IPC_H
#define IPC_H

#include <stddef.h>

#define IPC_ACC_LEN  20
#define IPC_PASS_LEN 20
#define IPC_MSG_LEN  64

typedef enum {
    OP_LOGIN = 1,
    OP_DEPOSIT,
    OP_WITHDRAW,
    OP_TRANSFER,
    OP_BALANCE
} OpType;

typedef enum {
    ST_OK            = 0,
    ST_NO_ACCOUNT    = -1,
    ST_BAD_PASSWORD  = -2,
    ST_NO_FUNDS      = -3,
    ST_TRANSFER_FAIL = -4,
    ST_BAD_REQUEST   = -10
} Status;

typedef struct {
    int  atm_id;
    int  op;
    char acc_num[IPC_ACC_LEN];
    char password[IPC_PASS_LEN];
    char target_acc[IPC_ACC_LEN];
    long amount;
} Request;

typedef struct {
    int  status;
    long balance;
    char msg[IPC_MSG_LEN];
} Response;

int read_full(int fd, void *buf, size_t n);
int write_full(int fd, const void *buf, size_t n);
int ipc_call(int req_fd, int resp_fd, const Request *req, Response *resp);
void atm_run(int atm_id, int req_fd, int resp_fd);

#endif
