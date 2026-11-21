#include "account.h"

/**
 * fcntl을 이용한 파일 레코드 잠금 함수 (Advisory Locking)
 * type: F_RDLCK (읽기 잠금), F_WRLCK (쓰기 잠금), F_UNLCK (잠금 해제)
 */
int lock_file(int fd, int cmd, int type, off_t offset, int whence, off_t len) {
    struct flock lock;
    lock.l_type = type;
    lock.l_whence = whence;
    lock.l_start = offset;
    lock.l_len = len;
    lock.l_pid = getpid();

    return fcntl(fd, cmd, &lock);
}