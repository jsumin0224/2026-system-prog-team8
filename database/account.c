#include "account.h"

#include <limits.h>

/* 거래 내역 구조체를 채웁니다. */
static void fill_tx(Transaction *tx, const char *acc_num, const char *type,
                    long amount, long balance_after, const char *target_acc)
{
    memset(tx, 0, sizeof *tx);

    time_t t = time(NULL);
    char buf[32];
    ctime_r(&t, buf);

    snprintf(tx->timestamp, sizeof(tx->timestamp), "%.24s", buf);
    snprintf(tx->acc_num, sizeof(tx->acc_num), "%s", acc_num);
    snprintf(tx->type, sizeof(tx->type), "%s", type);
    tx->amount = amount;
    tx->balance_after = balance_after;
    snprintf(tx->target_acc, sizeof(tx->target_acc), "%s", target_acc ? target_acc : "");
}

/* 파일 처음부터 훑어서 활성 계좌를 찾고, 레코드 위치를 offset에 저장합니다. */
static int find_record(int fd, const char *acc_num, Account *out, off_t *offset)
{
    if (lseek(fd, 0, SEEK_SET) < 0)
        return -1;

    for (;;) {
        off_t pos = lseek(fd, 0, SEEK_CUR);
        if (pos < 0)
            return -1;

        ssize_t n = read(fd, out, sizeof(Account));
        if (n != (ssize_t)sizeof(Account))
            return -1;

        if (strcmp(out->acc_num, acc_num) == 0 && out->is_active == 1) {
            *offset = pos;
            return 0;
        }
    }
}

/* 지정한 위치에 계좌 레코드 하나를 덮어씁니다. */
static int write_record(int fd, off_t offset, const Account *acc)
{
    if (lseek(fd, offset, SEEK_SET) < 0)
        return -1;
    return write(fd, acc, sizeof(Account)) == (ssize_t)sizeof(Account) ? 0 : -1;
}

/* 계좌 파일을 열고 쓰기 잠금까지 잡습니다. 실패하면 -1을 반환합니다. */
static int open_locked_rw(const char *who)
{
    int fd = open(ACCOUNT_FILE, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "%s open error: %s\n", who, strerror(errno));
        return -1;
    }

    if (lock_file(fd, F_SETLKW, F_WRLCK, 0, SEEK_SET, 0) < 0) {
        fprintf(stderr, "%s write lock error: %s\n", who, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static void unlock_close(int fd)
{
    lock_file(fd, F_SETLKW, F_UNLCK, 0, SEEK_SET, 0);
    close(fd);
}

/**
 * 초기 테스트용 계좌 파일 생성 (최초 실행 시 호출)
 */
int init_account_file() {
    int fd = open(ACCOUNT_FILE, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        if (errno == EEXIST) {
            return 0; // 이미 파일이 존재하면 정상
        }
        perror("init_account_file open error");
        return -1;
    }

    //샘플 계좌 3개    계좌번호/비밀번호/예금주 이름/잔액/활성화 여부 (1:정상)
    Account samples[3] = {
        {"1001", "1234", "짱구", 500000, 1},
        {"1002", "5678", "철수", 300000, 1},
        {"1003", "0000", "맹구", 200000, 1}
    };

    if (write(fd, samples, sizeof samples) != (ssize_t)sizeof samples) {
        perror("init_account_file write error");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

/**
 * 특정 계좌 조회 (읽기 잠금 적용)
 */
int find_account(const char *acc_num, Account *out_acc) {
    int fd = open(ACCOUNT_FILE, O_RDONLY);
    if (fd < 0) {
        perror("find_account open error");
        return -1;
    }

    if (lock_file(fd, F_SETLKW, F_RDLCK, 0, SEEK_SET, 0) < 0) {
        perror("find_account read lock error");
        close(fd);
        return -1;
    }

    off_t offset;
    int found = (find_record(fd, acc_num, out_acc, &offset) == 0);

    unlock_close(fd);
    return found ? 0 : -1;
}

/**
 * 입금 처리 (쓰기 잠금 및 동시성 제어)
 * 반환: 0 성공, -1 계좌없음/오류, -5 잔액 한도 초과
 */
int process_deposit(const char *acc_num, long amount) {
    int fd = open_locked_rw("process_deposit");
    if (fd < 0)
        return -1;

    Account acc;
    off_t offset;
    int result = -1;

    if (find_record(fd, acc_num, &acc, &offset) == 0) {
        if (amount > LONG_MAX - acc.balance) {
            result = -5;
        } else {
            acc.balance += amount;
            if (write_record(fd, offset, &acc) == 0) {
                result = 0;

                Transaction tx;
                fill_tx(&tx, acc_num, "DEPOSIT", amount, acc.balance, NULL);
                log_transaction(&tx);
            }
        }
    }

    unlock_close(fd);
    return result;
}

/**
 * 출금 처리 (비밀번호 검증 및 잔액 확인 포함)
 * 반환: 0 성공, -1 계좌없음/오류, -2 비밀번호 오류, -3 잔액 부족
 */
int process_withdraw(const char *acc_num, long amount, const char *password) {
    int fd = open_locked_rw("process_withdraw");
    if (fd < 0)
        return -1;

    Account acc;
    off_t offset;
    int result = -1;

    if (find_record(fd, acc_num, &acc, &offset) == 0) {
        if (strcmp(acc.password, password) != 0) {
            result = -2;
        } else if (acc.balance < amount) {
            result = -3;
        } else {
            acc.balance -= amount;
            if (write_record(fd, offset, &acc) == 0) {
                result = 0;

                Transaction tx;
                fill_tx(&tx, acc_num, "WITHDRAW", amount, acc.balance, NULL);
                log_transaction(&tx);
            }
        }
    }

    unlock_close(fd);
    return result;
}

/**
 * 계좌 이체 처리: 하나의 잠금 안에서 출금과 입금을 함께 처리합니다.
 * 반환: 0 성공, -1 보내는 계좌 없음/오류, -2 비밀번호 오류,
 *       -3 잔액 부족, -4 받는 계좌 오류 또는 쓰기 실패
 */
int process_transfer(const char *from_acc, const char *to_acc, long amount, const char *password) {
    if (strcmp(from_acc, to_acc) == 0)
        return -4;

    int fd = open_locked_rw("process_transfer");
    if (fd < 0)
        return -1;

    Account from, to;
    off_t from_off, to_off;
    int result;

    if (find_record(fd, from_acc, &from, &from_off) != 0) {
        result = -1;
    } else if (strcmp(from.password, password) != 0) {
        result = -2;
    } else if (from.balance < amount) {
        result = -3;
    } else if (find_record(fd, to_acc, &to, &to_off) != 0) {
        result = -4;
    } else if (amount > LONG_MAX - to.balance) {
        result = -4;
    } else {
        Account from_before = from;

        from.balance -= amount;
        to.balance += amount;

        if (write_record(fd, from_off, &from) != 0) {
            result = -4;
        } else if (write_record(fd, to_off, &to) != 0) {
            /* 받는 계좌 기록에 실패하면 보내는 계좌를 원래대로 되돌립니다. */
            write_record(fd, from_off, &from_before);
            result = -4;
        } else {
            result = 0;

            Transaction tx;
            fill_tx(&tx, from_acc, "TRANSFER", amount, from.balance, to_acc);
            log_transaction(&tx);
        }
    }

    unlock_close(fd);
    return result;
}
