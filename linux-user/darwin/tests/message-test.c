/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            perror(#x);                                                        \
            return 1;                                                          \
        }                                                                      \
    } while (0)
int main(void)
{
    int s[2], pipefd[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, s) == 0);
    CHECK(pipe2(pipefd, O_CLOEXEC) == 0);
    struct iovec out[2] = { {"hel", 3}, {"lo", 2} };
    char control[CMSG_SPACE(sizeof(int))] = {0};
    struct msghdr send = {.msg_iov = out,
                          .msg_iovlen = 2,
                          .msg_control = control,
                          .msg_controllen = sizeof(control)};
    struct cmsghdr *c = CMSG_FIRSTHDR(&send);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &pipefd[0], sizeof(int));
    CHECK(sendmsg(s[0], &send, MSG_NOSIGNAL) == 5);
    char a[2] = {0}, b[3] = {0}, received[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec in[2] = { {a, sizeof(a)}, {b, sizeof(b)} };
    struct msghdr recv = {.msg_iov = in,
                          .msg_iovlen = 2,
                          .msg_control = received,
                          .msg_controllen = sizeof(received)};
    CHECK(recvmsg(s[1], &recv, MSG_CMSG_CLOEXEC) == 5);
    CHECK(!memcmp(a, "he", 2) && !memcmp(b, "llo", 3));
    c = CMSG_FIRSTHDR(&recv);
    CHECK(c && c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS);
    CHECK(c->cmsg_len == CMSG_LEN(sizeof(int)) &&
          !(recv.msg_flags & MSG_CTRUNC));
    int fd;
    memcpy(&fd, CMSG_DATA(c), sizeof(fd));
    CHECK(fcntl(fd, F_GETFD) & FD_CLOEXEC);
    CHECK(write(pipefd[1], "descriptor", 10) == 10);
    char data[10];
    CHECK(read(fd, data, sizeof(data)) == 10 &&
          !memcmp(data, "descriptor", 10));
    close(fd);
    CHECK(sendmsg(s[0], &send, 0) == 5);
    recv.msg_control = NULL;
    recv.msg_controllen = 0;
    CHECK(recvmsg(s[1], &recv, 0) == 5 && (recv.msg_flags & MSG_CTRUNC));
    int lowest = dup(pipefd[0]);
    CHECK(lowest >= 0);
    close(lowest);
    for (int i = 0; i < 128; i++) {
        CHECK(sendmsg(s[0], &send, 0) == 5);
        CHECK(recvmsg(s[1], &recv, 0) == 5);
        CHECK(recv.msg_flags & MSG_CTRUNC);
    }
    fd = dup(pipefd[0]);
    CHECK(fd == lowest);
    close(fd);
    CMSG_FIRSTHDR(&send)->cmsg_len = 1;
    CHECK(sendmsg(s[0], &send, 0) == -1 && errno == EINVAL);
    close(pipefd[0]);
    close(pipefd[1]);
    close(s[0]);
    close(s[1]);
    puts("MESSAGE CHECKS PASSED");
    return 0;
}
