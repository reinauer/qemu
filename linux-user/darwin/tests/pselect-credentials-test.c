/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;
static void alarm_handler(int sig) { interrupted = 1; }

int main(void)
{
    int p[2];
    fd_set rfds, wfds;
    struct timespec t, start, end;
    assert(syscall(SYS_setresuid, -1, geteuid(), -1) == 0);
    assert(syscall(SYS_setresgid, -1, getegid(), -1) == 0);
    errno = 0;
    assert(syscall(SYS_setresuid, -1, geteuid() + 1, -1) == -1);
    assert(errno == EPERM);
    assert(pipe(p) == 0);
    /* Cross both the Darwin 32-bit and Linux 64-bit fd bitmap boundaries. */
    assert(dup2(p[0], 70) == 70);
    assert(write(p[1], "x", 1) == 1);
    FD_ZERO(&rfds); FD_SET(70, &rfds);
    FD_ZERO(&wfds); FD_SET(p[1], &wfds);
    t = (struct timespec){1, 0};
    assert(syscall(SYS_pselect6, 71, &rfds, &wfds, NULL, &t, NULL) == 2);
    assert(FD_ISSET(70, &rfds) && FD_ISSET(p[1], &wfds));
    assert(t.tv_sec == 0 && t.tv_nsec > 0 && t.tv_nsec < 1000000000);
    t = (struct timespec){0, 20000000};
    clock_gettime(CLOCK_MONOTONIC, &start);
    assert(syscall(SYS_pselect6, 0, NULL, NULL, NULL, &t, NULL) == 0);
    clock_gettime(CLOCK_MONOTONIC, &end);
    assert((end.tv_sec - start.tv_sec) * 1000000000LL +
           end.tv_nsec - start.tv_nsec >= 20000000);
    assert(t.tv_sec == 0 && t.tv_nsec == 0);
    t = (struct timespec){0, 1000000000};
    errno = 0;
    assert(syscall(SYS_pselect6, 0, NULL, NULL, NULL, &t, NULL) == -1);
    assert(errno == EINVAL);
    close(70);
    t = (struct timespec){0, 0};
    errno = 0;
    assert(syscall(SYS_pselect6, 71, &rfds, NULL, NULL, &t, NULL) == -1);
    assert(errno == EBADF);
    signal(SIGALRM, alarm_handler);
    pid_t parent = getpid(), child = fork();
    assert(child >= 0);
    if (child == 0) {
        usleep(100000);
        assert(kill(parent, SIGALRM) == 0);
        _exit(0);
    }
    t = (struct timespec){5, 0};
    errno = 0;
    assert(syscall(SYS_pselect6, 0, NULL, NULL, NULL, &t, NULL) == -1);
    assert(errno == EINTR && interrupted);
    assert(t.tv_sec >= 2 && t.tv_sec <= 4);
    assert(waitpid(child, NULL, 0) == child);
    puts("credential reset and pselect6 readiness/timeout/error/EINTR passed");
    return 0;
}
