#include "ipc.h"

#include <stdio.h>

void atm_run(int atm_id, int req_fd, int resp_fd)
{
    Request script[] = {
        { 0, OP_LOGIN,    "1001", "1234", "",     0 },
        { 0, OP_DEPOSIT,  "1001", "1234", "",     1000 },
        { 0, OP_WITHDRAW, "1001", "1234", "",     500 },
        { 0, OP_WITHDRAW, "1001", "0000", "",     500 },
        { 0, OP_WITHDRAW, "1001", "1234", "",     999999999 },
        { 0, OP_TRANSFER, "1001", "1234", "1002", 100 },
        { 0, 99,          "1001", "1234", "",     0 },
        { 0, OP_BALANCE,  "1002", "5678", "",     0 },
    };

    for (size_t i = 0; i < sizeof script / sizeof script[0]; i++) {
        Response r;
        script[i].atm_id = atm_id;
        if (ipc_call(req_fd, resp_fd, &script[i], &r) < 0) {
            fprintf(stderr, "ATM%d: lost connection to server\n", atm_id);
            return;
        }
        printf("ATM%d op=%d status=%d balance=%ld %s\n",
               atm_id, script[i].op, r.status, r.balance, r.msg);
        fflush(stdout);
    }
}
