#include "ipc.h"

#include <errno.h>
#include <unistd.h>

int read_full(int fd, void *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, (char *)buf + got, n - got);
        if (r == 0)
            return got == 0 ? 0 : -1;
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        got += (size_t)r;
    }
    return 1;
}

int write_full(int fd, const void *buf, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        ssize_t w = write(fd, (const char *)buf + sent, n - sent);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        sent += (size_t)w;
    }
    return 0;
}

int ipc_call(int req_fd, int resp_fd, const Request *req, Response *resp)
{
    if (write_full(req_fd, req, sizeof *req) < 0)
        return -1;
    return read_full(resp_fd, resp, sizeof *resp) == 1 ? 0 : -1;
}
