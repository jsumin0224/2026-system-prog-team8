#include "account.h"

/**
 * 초기 테스트용 계좌 파일 생성 (최초 실행 시 호출)
 */
int init_account_file() {
    int fd = open(ACCOUNT_FILE, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        if (errno == EEXIST) {
            return 0; // 이미 파일이 존재하면 정상
        }
        perror("init_account_file open error");
        return -1;
    }

    // 샘플 계좌 2개 생성
    Account sample1 = {"1001", "1234", "홍길동", 500000, 1};
    Account sample2 = {"1002", "5678", "김철수", 300000, 1};

    write(fd, &sample1, sizeof(Account));
    write(fd, &sample2, sizeof(Account));

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

    Account acc;
    int found = 0;
    while (read(fd, &acc, sizeof(Account)) > 0) {
        if (strcmp(acc.acc_num, acc_num) == 0 && acc.is_active == 1) {
            *out_acc = acc;
            found = 1;
            break;
        }
    }

    lock_file(fd, F_SETLKW, F_UNLCK, 0, SEEK_SET, 0);
    close(fd);

    return found ? 0 : -1;
}

/**
 * 입금 처리 (쓰기 잠금 및 동시성 제어)
 */
int process_deposit(const char *acc_num, long amount) {
    int fd = open(ACCOUNT_FILE, O_RDWR);
    if (fd < 0) {
        perror("process_deposit open error");
        return -1;
    }

    if (lock_file(fd, F_SETLKW, F_WRLCK, 0, SEEK_SET, 0) < 0) {
        perror("process_deposit write lock error");
        close(fd);
        return -1;
    }

    Account acc;
    off_t offset;
    int found = 0;

    while ((offset = lseek(fd, 0, SEEK_CUR)) >= 0) {
        if (read(fd, &acc, sizeof(Account)) <= 0) break;

        if (strcmp(acc.acc_num, acc_num) == 0 && acc.is_active == 1) {
            acc.balance += amount;
            
            lseek(fd, offset, SEEK_SET);
            write(fd, &acc, sizeof(Account));
            found = 1;
            
            // 거래 내역 기록
            Transaction tx;
            time_t t = time(NULL);
            snprintf(tx.timestamp, sizeof(tx.timestamp), "%.24s", ctime(&t));
            strcpy(tx.acc_num, acc_num);
            strcpy(tx.type, "DEPOSIT");
            tx.amount = amount;
            tx.balance_after = acc.balance;
            strcpy(tx.target_acc, "");
            log_transaction(&tx);

            break;
        }
    }

    lock_file(fd, F_SETLKW, F_UNLCK, 0, SEEK_SET, 0);
    close(fd);

    return found ? 0 : -1;
}

/**
 * 출금 처리 (비밀번호 검증 및 잔액 확인 포함)
 */
int process_withdraw(const char *acc_num, long amount, const char *password) {
    int fd = open(ACCOUNT_FILE, O_RDWR);
    if (fd < 0) {
        perror("process_withdraw open error");
        return -1;
    }

    if (lock_file(fd, F_SETLKW, F_WRLCK, 0, SEEK_SET, 0) < 0) {
        perror("process_withdraw write lock error");
        close(fd);
        return -1;
    }

    Account acc;
    off_t offset;
    int result = -1; // -1: 계좌없음, -2: 비밀번호 오류, -3: 잔액 부족, 0: 성공

    while ((offset = lseek(fd, 0, SEEK_CUR)) >= 0) {
        if (read(fd, &acc, sizeof(Account)) <= 0) break;

        if (strcmp(acc.acc_num, acc_num) == 0 && acc.is_active == 1) {
            if (strcmp(acc.password, password) != 0) {
                result = -2;
                break;
            }
            if (acc.balance < amount) {
                result = -3;
                break;
            }

            acc.balance -= amount;
            lseek(fd, offset, SEEK_SET);
            write(fd, &acc, sizeof(Account));
            result = 0;

            // 거래 내역 기록
            Transaction tx;
            time_t t = time(NULL);
            snprintf(tx.timestamp, sizeof(tx.timestamp), "%.24s", ctime(&t));
            strcpy(tx.acc_num, acc_num);
            strcpy(tx.type, "WITHDRAW");
            tx.amount = amount;
            tx.balance_after = acc.balance;
            strcpy(tx.target_acc, "");
            log_transaction(&tx);

            break;
        }
    }

    lock_file(fd, F_SETLKW, F_UNLCK, 0, SEEK_SET, 0);
    close(fd);

    return result;
}

/**
 * 계좌 이체 처리
 */
int process_transfer(const char *from_acc, const char *to_acc, long amount, const char *password) {
    int res = process_withdraw(from_acc, amount, password);
    if (res != 0) {
        return res;
    }

    int dep_res = process_deposit(to_acc, amount);
    if (dep_res != 0) {
        process_deposit(from_acc, amount); // 복구(롤백)
        return -4;
    }

    return 0;
}