#include "account.h"

/**
 * 거래 내역 파일 기록 함수 (텍스트 한 줄, 쓰기 잠금 적용)
 * 형식: 시간 | 계좌 | 종류 | 금액 | 거래 후 잔액 | 상대 계좌
 */
int log_transaction(const Transaction *tx) {
    int fd = open(TRANSACTION_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        perror("log_transaction open error");
        return -1;
    }

    if (lock_file(fd, F_SETLKW, F_WRLCK, 0, SEEK_SET, 0) < 0) {
        perror("log_transaction write lock error");
        close(fd);
        return -1;
    }

    char line[256];
    int len = snprintf(line, sizeof(line), "%s | %s | %s | %ld | %ld | %s\n",
                       tx->timestamp, tx->acc_num, tx->type,
                       tx->amount, tx->balance_after,
                       tx->target_acc[0] ? tx->target_acc : "-");

    int result = 0;
    if (len < 0 || len >= (int)sizeof(line)) {
        result = -1;
    } else if (write(fd, line, (size_t)len) != len) {
        perror("log_transaction write error");
        result = -1;
    }

    lock_file(fd, F_SETLKW, F_UNLCK, 0, SEEK_SET, 0);
    close(fd);
    return result;
}
