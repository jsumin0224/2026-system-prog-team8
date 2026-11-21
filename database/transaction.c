#include "account.h"

/**
 * 거래 내역 파일 기록 함수 (쓰기 잠금 적용)
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

    write(fd, tx, sizeof(Transaction));

    lock_file(fd, F_SETLKW, F_UNLCK, 0, SEEK_SET, 0);
    close(fd);
    return 0;
}